# FLEX 1.0 — Architecture and System Design

## 1. Executive Summary

FLEX 1.0 is an authoritative native desktop AI assistant engineered in strict **C11** and **C++17** using **Qt 6**. It combines a deterministic, memory-safe C core with a modular C++ desktop architecture, local Ollama LLM inference, persistent SQLite storage, and a fully local voice subsystem powered by WASAPI, whisper.cpp, and Windows SAPI.

The project contains **zero Python runtime dependencies**. All components—command parsing, state management, audio DSP, streaming token extraction, storage query generation, speech recognition, speech synthesis, and desktop rendering—run as native 64-bit machine code.

Crucially, FLEX 1.0 is a **genuinely C-focused architecture**:
- **50.22% of first-party production code** is written in C11 (7,286 C SLOC vs 7,223 C++ SLOC across 14,509 total executable SLOC).
- The C11 core is not an inert stub: it acts as the authoritative gatekeeper for input validation, FSM state progression, audio sample processing, stream parsing, rate limiting, and desktop stylesheet generation.

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
|  |  ProjectsView | SettingsView | RightSidebar (Telemetry)                     |  |
|  +-----------------------------------------------------------------------------+  |
|         │                                │ (Applies Theme CSS)                     |
|         │                                ▼                                         |
|         │                       +-----------------------------------------------+  |
|         │                       | C Theme Engine: flex_theme_generate_...()     |  |
|         │                       +-----------------------------------------------+  |
|         │                                                                          |
|         +-----------------------------+-----------------------------+              |
|         |                                                           |              |
|  +------v-----------------------------+   +-------------------------v-----------+  |
|  |   BackendCoordinator (flex_backend)|   |  Voice Conversation Controller      |  |
|  |  - Orchestrates lifecycle & chat   |   |  - Audio capture & STT review       |  |
|  |  - Calls C request validator       |   |  - Enforces C Voice FSM Invariant   |  |
|  |  - Streaming token dispatch        |   |  - Windows SAPI TTS playback        |  |
|  +------+-----------------------+-----+   +-------------+-----------------------+  |
|         |                       |                       |                          |
|  +------v----------+     +------v----------+     +------v-----------------------+   |
|  | SQLite Storage  |     | Ollama Client   |     | Voice Subsystem Submodules   |   |
|  | (flex_storage)  |     | (flex_ai)       |     | - AudioCapture (WASAPI + C)  |   |
|  | - Schema v1     |     | - HTTP /api/chat|     |   * C Resampler & RMS Meter  |   |
|  | - Thread-safe   |     | - NDJSON stream |     | - Whisper STT (whisper.cpp)  |   |
|  | - SQLite3 / Qt  |     | - Qt Network    |     | - TextToSpeech (Win SAPI + C)|   |
|  +-----------------+     +-----------------+     |   * C Text Normalizer        |   |
|                                                  +------------------------------+   |
|  +-----------------------------------------------------------------------------+  |
|  |                      C++ Core Wrapper (flex_core_cpp)                       |  |
|  |  - CoreSession RAII Lifecycle Guard                                         |  |
|  +-----------------------------------------------------------------------------+  |
|                                       │                                            |
|  +------------------------------------v-----------------------------------------+ |
|  |              Authoritative C11 Core Engine (flex_core_c - 50.2% SLOC)        | |
|  |  - command_parser.c & request_validator.c : Bounded input validation, UTF-8  | |
|  |  - state_manager.c & voice_fsm.c          : Strict FSM lifecycle gatekeepers| |
|  |  - orchestrator.c & command_dispatcher.c  : Concurrency control & dispatch   | |
|  |  - audio_dsp.c                            : Linear resampler, RMS, and VAD   | |
|  |  - ai_stream_parser.c & json_validator.c  : Zero-allocation NDJSON streaming | |
|  |  - operation_policy.c & tool_policy.c      : Rate limiting, policy gating     | |
|  |  - theme_engine.c & color_palette.c       : Complete desktop QSS generation  | |
|  |  - conversation_record.c & sqlite_codec.c  : Schema verification, SQL encode  | |
|  |  - diff_engine.c, lexical_analyzer.c, math_evaluator.c, bloom_filter.c, ...  | |
|  +------------------------------------------------------------------------------+ |
|                                                                                   |
+-----------------------------------------------------------------------------------+
```

---

## 3. Subsystem Breakdown

### 3.1 Authoritative C11 Core (`core-c/`)
The foundational core is written in portable C11 with zero dynamic heap allocations in performance-critical paths and strict memory bounding (`FLEX_MAX_INPUT_LEN = 1024` bytes). It encompasses 42 production modules organized into cohesive functional domains:

1. **Input Validation & Parsing**:
   - `command_parser.c`: Validates bounded inputs (<= 1024B), strips control characters, tokenizes verbs and argument vectors.
   - `request_validator.c`: RFC 3629 UTF-8 validator, control char sanitization, prompt injection heuristic screening, and BPE token count estimation.
   - `json_validator.c`: Zero-allocation recursive-descent JSON syntax validator, balanced brace verification, and string/integer extraction.
2. **Lifecycle & State Machines**:
   - `state_manager.c`: Monotonic 64-bit operation IDs and strict application lifecycle (`IDLE` -> `PROCESSING` -> `STOPPING`).
   - `voice_fsm.c`: Finite state machine enforcing the voice pipeline invariant: **at most one backend submission per confirmed transcript**.
   - `session_state.c`: Multi-turn session context tracker with FIFO context window trimming preserving system prompts.
3. **Audio DSP & Voice Pipelines**:
   - `audio_dsp.c`: Linear 16 kHz resampler, multi-channel to mono downmixer, PCM sample normalizer, RMS/dB level calculator, and energy-based VAD.
   - `whisper_pipeline.c`: Audio format validator, timestamp formatter, hallucination filter, and model RAM estimation.
   - `tts_pipeline.c`: Utterance queue, sentence splitter, and abbreviation normalizer (expanding `e.g.`, `i.e.`, `ms`, etc.).
4. **Streaming AI & Network Serialization**:
   - `ai_stream_parser.c`: Zero-allocation streaming parser for Ollama NDJSON chunks, string unescaping, and fragment buffering.
   - `ai_payload_builder.c`: Formats bounded Ollama `/api/chat` JSON payloads with system prompt substitution and options.
   - `url_parser.c`: RFC 3986 URL parsing, normalization, and query parameter extraction.
5. **Security, Policy & Registry**:
   - `operation_policy.c`: Token-bucket rate limiter, burst capacity control, and payload size bounds.
   - `tool_policy.c`: Native tool registry with JSON argument validation and confirmation gating for mutating operations.
   - `security_sandbox.c`: Path traversal detection, shell injection prevention, and security audit logging.
6. **Desktop Theme & Presentation**:
   - `theme_engine.c`: Complete desktop Qt stylesheet (QSS) generator for space-obsidian design system with WCAG contrast enforcement.
   - `color_palette.c`: RGBA/HSVA conversion, alpha blending, and contrast ratio calculation.
   - `response_formatter.c`: Markdown fence detection, HTML entity escaping, and preview snippet generation.
7. **Storage, Querying & Indexing**:
   - `conversation_record.c`: Schema validation, sequence checking, SQL query building, and JSON record serialization.
   - `sqlite_codec.c`: SQL string escaping and bounded query generator.
   - `memory_index.c`: In-memory conversation search index with tokenization, substring matching, and suffix-stemming.
8. **Systems, Math & Data Structures**:
   - `bloom_filter.c`: 512-bit probabilistic term filter with union, intersection, and false-positive estimation.
   - `kv_store.c`: Bounded key-value configuration file parser and serializer (`key=value\n`).
   - `cache_table.c`: Bounded LRU cache table with TTL expiration and hit/miss eviction metrics.
   - `hash_table.c`: Closed-addressing open-probing FNV-1a hash table for key-value symbols.
   - `math_evaluator.c`: Recursive descent PEMDAS arithmetic evaluator with sqrt, abs, pow, min, max, round, floor, ceil.
   - `diff_engine.c`: Line diff engine, unified diff formatter, and similarity ratio calculator.
   - `csv_table_formatter.c`: CSV parser, column width calculation, Markdown table and ASCII grid formatter.
   - `lexical_analyzer.c`: Language detection, bracket balancer, line numbering, and keyword checks (C, Python, SQL).
   - `base64_codec.c`, `crc32.c`, `ansi_escape.c`, `regex_matcher.c`, `sysinfo_collector.c`, `telemetry_collector.c`, `context_compressor.c`, `text_tokenizer.c`, `prompt_template.c`, `command_dispatcher.c`.

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
- **Audio Capture (`audio_capture_service.cpp`)**: Captures native Windows microphone input via WASAPI through `QAudioSource`. Formats audio into normalized 16 kHz Mono 32-bit floating-point samples stored in a bounded `AudioBuffer` (max 30 seconds). Delegates sample downsampling to `flex_audio_resample_linear()` and live amplitude metering to `flex_audio_calculate_rms()` in `audio_dsp.c`.
- **Speech-to-Text (`speech_to_text_service.cpp`)**: Bundles `whisper.cpp` (v1.7.4). Performs in-process transcription of PCM float audio using quantized models (`ggml-tiny.en.bin`).
- **Model Resolver (`whisper_model_resolver.cpp`)**: Resolves Whisper model files across standard search paths:
  1. `%USERPROFILE%\.flex\models\whisper\ggml-tiny.en.bin`
  2. `./models/whisper/ggml-tiny.en.bin`
  3. Custom path defined in settings.
- **Text-to-Speech (`text_to_speech_service.cpp`)**: Windows SAPI 5.4 COM integration. Queues spoken utterances in a thread-safe worker queue (`tts_queue.hpp`), supporting non-blocking audio playback, pause, resume, and cancellation. Normalizes text and expands common abbreviations before synthesis via `flex_tts_normalize_text()` in `tts_pipeline.c`.
- **Voice Conversation Controller (`voice_conversation_controller.cpp`)**: Coordinates the voice interaction loop and strictly validates the single-submission invariant via `flex_voice_fsm_authorize_submission()` in `voice_fsm.c`:
  1. User speaks -> WASAPI captures audio -> C DSP computes RMS & resamples.
  2. whisper.cpp transcribes speech to text.
  3. UI displays editable review deck (User can edit or confirm).
  4. C Voice FSM authorizes submission (rejecting duplicates).
  5. Confirmed prompt dispatched through C core and submitted to Ollama.
  6. Assistant response persisted in SQLite and optionally read aloud via SAPI TTS.

### 3.6 Backend Coordinator (`src-cpp/src/backend.cpp`)
- Connects `CoreSession`, `ConversationStorage`, and `OllamaClient` into a unified pipeline.
- Enforces front-door prompt validation using `flex_validate_chat_prompt()` from `request_validator.c` (bounds <= 1024B, UTF-8 checks, injection heuristics).
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
- **Theme (`theme.cpp`)**: High-contrast dark visual design system (`#0A0D14` background, `#00D2FF` electric cyan accents, `#3B82F6` cobalt secondary accents). Generated dynamically from C via `flex_theme_generate_desktop_stylesheet()` in `theme_engine.c`.

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
