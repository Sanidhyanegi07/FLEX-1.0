# FLEX 1.0 — Architecture and System Design

## 1. Executive Summary

FLEX 1.0 is a native desktop AI assistant built in strict **C11** and **C++17** using **Qt 6**. It combines a deterministic, memory-safe C core with a modular C++ desktop architecture, local Ollama LLM inference, persistent SQLite storage, and a fully local voice subsystem powered by WASAPI, whisper.cpp, and Windows SAPI.

The project contains **zero Python runtime dependencies**. All components—command parsing, state management, storage, networking, AI streaming, audio capture, speech recognition, speech synthesis, and desktop rendering—run as native 64-bit machine code.

---

## 2. High-Level Architecture

```
+-----------------------------------------------------------------------------------+
|                            FLEX 1.0 Desktop (flex_desktop)                         |
+-----------------------------------------------------------------------------------+
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  |                            Qt 6 Desktop UI (flex_ui)                        |  |
|  |  MainWindow | NavigationSidebar | ChatView | VoiceStudioView | MemoryView   |  |
|  |  ProjectsView | SettingsView | RightSidebar (Telemetry) | Theme System      |  |
|  +-----------------------------------------------------------------------------+  |
|                                       |                                           |
|         +-----------------------------+-----------------------------+             |
|         |                                                           |             |
|  +------v-----------------------------+   +-------------------------v----------+  |
|  |   BackendCoordinator (flex_backend)|   |  Voice Conversation Controller     |  |
|  |  - Orchestrates lifecycle & chat   |   |  - Audio capture & STT review      |  |
|  |  - Streaming token dispatch        |   |  - Windows SAPI TTS playback       |  |
|  |  - Monotonic error classification  |   |  (flex_voice)                      |  |
|  +------+-----------------------+-----+   +-------------+----------------------+  |
|         |                       |                       |                         |
|  +------v----------+     +------v----------+     +------v----------------------+  |
|  | SQLite Storage  |     | Ollama Client   |     | Voice Subsystem Submodules  |  |
|  | (flex_storage)  |     | (flex_ai)       |     | - AudioCapture (WASAPI)     |  |
|  | - Schema v1     |     | - HTTP /api/chat|     | - Whisper STT (whisper.cpp) |  |
|  | - Thread-safe   |     | - NDJSON stream |     | - TextToSpeech (Win SAPI)   |  |
|  | - SQLite3 / Qt  |     | - Qt Network    |     | - WhisperModelResolver      |  |
|  +-----------------+     +-----------------+     +-----------------------------+  |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  |                      C++ Core Wrapper (flex_core_cpp)                       |  |
|  |  - CoreSession RAII Lifecycle Guard                                         |  |
|  +-----------------------------------------------------------------------------+  |
|                                       |                                           |
|  +------------------------------------v----------------------------------------+  |
|  |                      Deterministic C11 Core (flex_core_c)                   |  |
|  |  - command_parser.c : Bounded 1024-byte input validation, zero allocations  |  |
|  |  - state_manager.c  : Strict FSM (IDLE -> PROCESSING -> STOPPING)          |  |
|  |  - orchestrator.c   : Atomic command dispatch & concurrency control         |  |
|  +-----------------------------------------------------------------------------+  |
|                                                                                   |
+-----------------------------------------------------------------------------------+
```

---

## 3. Subsystem Breakdown

### 3.1 Strict C11 Core (`core-c/`)
The foundational core is written in portable C11 with zero dynamic heap allocations:
- **`command_parser.c`**: Normalizes input strings, validates bounds against `FLEX_MAX_INPUT_LEN = 1024` bytes, strips control characters, and extracts command verbs and argument vectors.
- **`state_manager.c`**: Implements a deterministic finite state machine (`FLEX_STATE_IDLE`, `FLEX_STATE_PROCESSING`, `FLEX_STATE_STOPPING`). Issues monotonic 64-bit operation IDs and tracks active operation categories (`NONE`, `CHAT`, `VOICE`, `MEMORY`).
- **`orchestrator.c`**: Provides atomic command-to-lifecycle dispatch. Thread-safe mutex locking prevents concurrent command executions while the core is in `PROCESSING` state.
- **`bridge_main.c`**: Lightweight command-line interface executable (`flex_bridge.exe`) providing JSON-delimited line protocol access for scripting or external harnesses.

### 3.2 C++ Core Wrapper (`src-cpp/src/core_session.cpp`)
- Encapsulates the C core in an RAII C++ class `CoreSession`.
- Automatically calls `flex_orchestrator_init()` on construction and `flex_orchestrator_shutdown()` on destruction.
- Provides type-safe wrappers for `dispatch()`, `finish()`, and `reset()`.

### 3.3 Persistent SQLite Storage (`src-cpp/src/storage.cpp`)
- Built on `Qt6::Sql` utilizing the native SQLite3 driver.
- Stores user conversations, message turns, role annotations, timestamps, and monotonic sequence numbers.
- Default path: `%LOCALAPPDATA%\FLEX\conversations.db`.
- Schema Versioning: Automatically checks and migrates database schemas to ensure compatibility.
- Thread Safety: Employs SQLite busy timeout (5,000 ms) and connection-isolated transactions.

### 3.4 Native Ollama AI Client (`src-cpp/src/ollama_client.cpp`)
- Fully asynchronous client using `QNetworkAccessManager`.
- Connects to local Ollama daemon (`http://127.0.0.1:11434`).
- Parses chunked NDJSON responses from `/api/chat` and emits streaming tokens in real time.
- Validates model availability via `/api/tags` and handles graceful abort / cancellation.

### 3.5 Voice Subsystem (`src-cpp/src/voice/`)
- **Audio Capture (`audio_capture_service.cpp`)**: Captures native Windows microphone input via WASAPI through `QAudioSource`. Resamples and converts raw audio into normalized 16 kHz Mono 32-bit floating-point samples stored in an bounded `AudioBuffer` (max 30 seconds).
- **Speech-to-Text (`speech_to_text_service.cpp`)**: Bundles `whisper.cpp` (v1.7.4). Performs in-process transcription of PCM float audio using quantized models (`ggml-tiny.en.bin`).
- **Model Resolver (`whisper_model_resolver.cpp`)**: Resolves Whisper model files across standard search paths:
  1. `%USERPROFILE%\.flex\models\whisper\ggml-tiny.en.bin`
  2. `./models/whisper/ggml-tiny.en.bin`
  3. Custom path defined in settings.
- **Text-to-Speech (`text_to_speech_service.cpp`)**: Windows SAPI 5.4 COM integration. Queues spoken utterances in a thread-safe worker queue (`tts_queue.hpp`), supporting non-blocking audio playback, pause, resume, and cancellation.
- **Voice Conversation Controller (`voice_conversation_controller.cpp`)**: Coordinates the voice interaction loop:
  1. User speaks -> WASAPI captures audio.
  2. whisper.cpp transcribes speech to text.
  3. UI displays editable review deck (User can edit or confirm).
  4. Confirmed text dispatched through C core and submitted to Ollama.
  5. Assistant response persisted in SQLite and optionally read aloud via SAPI TTS.

### 3.6 Backend Coordinator (`src-cpp/src/backend.cpp`)
- Connects `CoreSession`, `ConversationStorage`, and `OllamaClient` into a unified pipeline.
- Handles user message submission, verifies C core dispatch, streams incoming LLM tokens, saves completed turns to SQLite, and transitions C core back to `IDLE`.
- Emits Qt signals for UI updates: `chatChunkReceived`, `chatFinished`, `chatError`.

### 3.7 Native Qt 6 Desktop UI (`src-cpp/src/ui/`)
- **MainWindow**: Three-pane responsive workstation:
  - Left Sidebar: Navigation between primary functional views and conversation history.
  - Central Workspace: Swappable view deck with smooth stack transitions.
  - Right Sidebar: Collapsible telemetry panel with live C core state badge, Ollama status, and active operation metrics.
- **Views**:
  - `ChatView`: AI conversation view with message bubbles, code block styling, and composer.
  - `VoiceStudioView`: Dedicated audio laboratory with live RMS level meter, waveform indicator, recording controls, transcript editor, and TTS playground.
  - `MemoryView`: Searchable database inspector for stored conversations.
  - `ProjectsView`: Dynamic directory tree view anchored to the active workspace.
  - `SettingsView`: User preference pane for AI models, Whisper paths, voice selection, and speech rates.
- **Theme (`theme.cpp`)**: High-contrast dark visual design system (`#0A0D14` background, `#00D2FF` electric cyan accents, `#3B82F6` cobalt secondary accents).

---

## 4. Concurrency and Threading Model

```
+---------------------------------------------------------------------------------+
|                                 MAIN THREAD (Qt Event Loop)                     |
|                                                                                 |
|  - UI Widgets, animations, user events                                          |
|  - OllamaClient async network callbacks                                         |
|  - BackendCoordinator signal emission & slots                                   |
+------------------------+------------------------------------+-------------------+
                         |                                    |
+------------------------v-----------+            +-----------v-------------------+
|     AUDIO WORKER THREAD            |            |     TTS WORKER THREAD         |
|  - WASAPI audio stream reader      |            |  - COM initialized thread     |
|  - Sample normalization & RMS      |            |  - SAPI ISpVoice playback     |
|  - whisper.cpp inference execution |            |  - Non-blocking queue         |
+------------------------------------+            +-------------------------------+
```

1. **Main UI Thread**: Runs the Qt event loop, renders all UI components, and handles non-blocking network I/O from Ollama via `QNetworkAccessManager`.
2. **Audio Worker Thread**: Handles audio sample acquisition and heavy Whisper neural network inference without blocking UI responsiveness.
3. **TTS Worker Thread**: Initializes COM (`CoInitializeEx`) and dispatches speech synthesis asynchronously to Windows audio output.
