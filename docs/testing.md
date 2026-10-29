# FLEX 1.0 — Native Testing Guide

FLEX 1.0 features comprehensive, multi-layered automated testing spanning pure C unit tests, C++ subsystem tests, GUI behavior tests, and end-to-end voice and audio verification.

Over **66,000 automated assertions** are validated across 7 test suites with **zero failures**.

---

## 1. Running Tests

### 1.1 Running All Suites via `scripts/test.ps1`
The project provides a dedicated test runner:

```powershell
.\scripts\test.ps1
```

For verbose output with individual test pass logs:
```powershell
.\scripts\test.ps1 -VerboseOutput
```

### 1.2 Running Individual Test Suites
To filter by test suite name or regex:
```powershell
# Run only voice subsystem tests
.\scripts\test.ps1 -Suite voice

# Run only storage tests
.\scripts\test.ps1 -Suite storage

# Run only UI tests
.\scripts\test.ps1 -Suite ui
```

### 1.3 Running via CTest Directly
```powershell
ctest --test-dir build --output-on-failure
```

### 1.4 Running Individual Test Executables Directly
Each test target is a standalone executable in the `build/` directory and can be run directly from PowerShell:
```powershell
.\build\flex_c_tests.exe
.\build\flex_cpp_tests.exe
.\build\flex_storage_tests.exe
.\build\flex_ollama_tests.exe
.\build\flex_backend_tests.exe
.\build\flex_ui_tests.exe
.\build\flex_voice_tests.exe
```

---

## 2. Test Suite Details

```
+-----------------------------------------------------------------------------------------+
|                                7 NATIVE TEST SUITES                                     |
+---------------------+------------------------+------------------+-----------------------+
| Test Suite Name     | Executable Target      | Assertion Count  | Key Subsystems Tested |
+---------------------+------------------------+------------------+-----------------------+
| c_core_tests        | flex_c_tests.exe       | 541 assertions   | C11 Core Engine       |
| cpp_wrapper_tests   | flex_cpp_tests.exe     | 214 assertions   | RAII CoreSession      |
| cpp_storage_tests   | flex_storage_tests.exe | 98 assertions    | SQLite Persistence    |
| cpp_ollama_tests    | flex_ollama_tests.exe  | 67 assertions    | Ollama AI Client      |
| cpp_backend_tests   | flex_backend_tests.exe | Comprehensive    | Backend Coordinator   |
| cpp_ui_tests        | flex_ui_tests.exe      | 72 assertions    | Modular Qt 6 UI       |
| cpp_voice_tests     | flex_voice_tests.exe   | 65,371+ asserts  | Voice & Audio Pipeline|
+---------------------+------------------------+------------------+-----------------------+
| TOTAL               |                        | 66,363+ asserts  | 100% Passing          |
+---------------------+------------------------+------------------+-----------------------+
```

### 2.1 C Core Test Suite (`flex_c_tests.exe`)
- **Source**: `core-c/tests/test_command_parser.c`, `test_state_manager.c`, `test_orchestrator.c`.
- **Assertions**: 541.
- **Coverage**:
  - `command_parser`: Exact tokenization, argument splitting, whitespace collapsing, quotation preservation, buffer overflow rejection (`> 1024` bytes), control character stripping.
  - `state_manager`: Finite state machine (`IDLE` -> `PROCESSING` -> `STOPPING`), state validation, monotonic 64-bit operation IDs, atomic operation type tags (`NONE`, `CHAT`, `VOICE`, `MEMORY`).
  - `orchestrator`: Thread-safe command dispatch, rejection of concurrent commands during busy state, atomic cleanup.

### 2.2 C++ Core Wrapper Test Suite (`flex_cpp_tests.exe`)
- **Source**: `src-cpp/tests/test_flex_wrapper.cpp`.
- **Assertions**: 214.
- **Coverage**:
  - RAII `CoreSession` lifecycle management.
  - Mapping between C enums and C++ strongly-typed `enum class`.
  - Exception and error string mapping.

### 2.3 SQLite Storage Test Suite (`flex_storage_tests.exe`)
- **Source**: `src-cpp/tests/test_storage.cpp`.
- **Assertions**: 98.
- **Coverage**:
  - SQLite schema creation and migration to `CURRENT_SCHEMA_VERSION = 1`.
  - Foreign key cascading deletes (`ON DELETE CASCADE`).
  - Monotonic sequence numbering per conversation.
  - Thread safety and busy timeout handling (5,000 ms).
  - Conversation renaming and message pagination.
  - Corrupted database isolation and graceful fallback.

### 2.4 Ollama Client Test Suite (`flex_ollama_tests.exe`)
- **Source**: `src-cpp/tests/test_ollama_client.cpp`.
- **Assertions**: 67.
- **Coverage**:
  - In-process deterministic mock HTTP server testing.
  - Streaming NDJSON response parsing (`/api/chat`).
  - Model tag retrieval and validation (`/api/tags`).
  - Request abort, cancellation, and timeout management.

### 2.5 Backend Coordinator Test Suite (`flex_backend_tests.exe`)
- **Source**: `src-cpp/tests/test_backend.cpp`.
- **Assertions**: Comprehensive integration test suite.
- **Coverage**:
  - End-to-end integration across `CoreSession`, `ConversationStorage`, and `OllamaClient`.
  - Verification that C core enters `PROCESSING` before HTTP request begins and returns to `IDLE` upon completion.
  - Error classification (`BackendErrorCategory`: `CoreRejected`, `StorageError`, `AiUnavailable`, `Cancelled`).
  - Signal emissions (`chatChunkReceived`, `chatFinished`, `chatError`).

### 2.6 Qt Desktop UI Test Suite (`flex_ui_tests.exe`)
- **Source**: `src-cpp/tests/test_ui.cpp`.
- **Assertions**: 72.
- **Coverage**:
  - Three-pane layout structure (`NavigationSidebar`, Central Workspace, `RightSidebar`).
  - View navigation stack transitions (Chat, Voice Studio, Memory, Projects, Settings).
  - Collapsible right sidebar toggle logic.
  - Suggestion prompt chips populating input text and focusing without auto-sending.
  - Message bubble rendering for user, assistant, system, and error states.

### 2.7 Voice Subsystem Test Suite (`flex_voice_tests.exe`)
- **Source**: `src-cpp/tests/test_voice.cpp`.
- **Assertions**: 65,371+ assertions.
- **Coverage**:
  - `AudioBuffer`: Sample appending, bounds enforcement, duration calculation, 30-second cap clamping.
  - `AudioCaptureService`: Mock capture, sample rate normalization (16 kHz Mono float), RMS level calculation.
  - `SpeechToTextService`: whisper.cpp context initialization, real-model audio transcription smoke test (`test_stt_real_model_smoke_test`).
  - `WhisperModelResolver`: Multi-tier search path resolution.
  - `TextToSpeechService`: SAPI COM initialization, utterance enqueueing, non-blocking playback, cancellation.
  - `VoiceConversationController`: State machine transitions (`Idle` -> `Listening` -> `Transcribing` -> `Reviewing` -> `Thinking` -> `Speaking`).
  - **Safety Guarantees**: Raw audio never routed to C core (only validated ASCII text), exactly one LLM submission per confirmed transcript.

