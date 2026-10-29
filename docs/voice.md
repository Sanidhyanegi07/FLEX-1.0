# FLEX 1.0 — Voice Subsystem Architecture and Guide

The FLEX 1.0 Voice Subsystem (`flex_voice`) delivers a completely local, offline voice interaction pipeline on Windows without relying on any cloud APIs or external Python processes.

---

## 1. Subsystem Architecture

```
                                 VOICE PIPELINE
                                 
   [Microphone]
        │
        ▼ (WASAPI)
  ┌─────────────────────────────────────────────────────────┐
  │ AudioCaptureService                                     │
  │ - Captures native Windows microphone audio via WASAPI    │
  │ - Converts to 16 kHz Mono 32-bit floating point PCM     │
  │ - Computes real-time RMS metering for visual feedback    │
  │ - Enforces strict 30-second bounded AudioBuffer         │
  └─────────────────────────────┬───────────────────────────┘
                                │ 16 kHz Mono PCM Float
                                ▼
  ┌─────────────────────────────────────────────────────────┐
  │ SpeechToTextService (whisper.cpp v1.7.4)                │
  │ - Resolves ggml-tiny.en.bin model file                  │
  │ - Runs local neural network inference                   │
  │ - Emits transcribed text                                │
  └─────────────────────────────┬───────────────────────────┘
                                │ Transcribed Text String
                                ▼
  ┌─────────────────────────────────────────────────────────┐
  │ Transcript Review Deck (ChatView / VoiceStudioView)     │
  │ - User reviews recognized text                          │
  │ - User can edit, confirm, or discard                    │
  └─────────────────────────────┬───────────────────────────┘
                                │ Confirmed Prompt (<= 1024 bytes)
                                ▼
  ┌─────────────────────────────────────────────────────────┐
  │ BackendCoordinator & Strict C11 Core                    │
  │ - Validates bounds (FLEX_MAX_INPUT_LEN = 1024)          │
  │ - Dispatches operation to IDLE -> PROCESSING            │
  │ - Generates LLM reply via Ollama                        │
  └─────────────────────────────┬───────────────────────────┘
                                │ Assistant Text Reply
                                ▼
  ┌─────────────────────────────────────────────────────────┐
  │ TextToSpeechService (Windows SAPI 5.4)                  │
  │ - Non-blocking TTS worker thread                        │
  │ - Thread-safe utterance queue                           │
  │ - Plays audio via native Windows audio devices          │
  └─────────────────────────────────────────────────────────┘
```

---

## 2. Core Components

### 2.1 Audio Capture Service (`AudioCaptureService`)
- **Backend**: Native Windows Audio Session API (WASAPI) accessed via Qt 6 `QAudioSource`.
- **Target Format**: 16,000 Hz sample rate, 1 channel (Mono), 32-bit signed float normalized to $[-1.0, 1.0]$.
- **C Audio DSP Acceleration**:
  - Sample downsampling from hardware rate (e.g. 48 kHz / 44.1 kHz) to 16 kHz is performed by `flex_audio_resample_linear()` in `core-c/src/audio_dsp.c`.
  - Dynamic Root-Mean-Square (RMS) amplitude is calculated by `flex_audio_calculate_rms()` in `audio_dsp.c` and emitted via `levelChanged(float level)` for the visualizer.
- **Buffer Safety**: Enforces a strict 30-second cap (`AudioBuffer::MAX_DURATION_MS = 30000`). If recording exceeds this threshold, audio capture stops cleanly to prevent memory exhaustion.

### 2.2 Local Speech-to-Text (`SpeechToTextService`)
- **Engine**: Vendored `whisper.cpp` (v1.7.4) statically compiled into `flex_voice`.
- **Model**: `ggml-tiny.en.bin` (~77.7 MB) English-optimized quantized model.
- **Performance**: Near-instant offline transcription on standard desktop CPUs without requiring GPU acceleration.
- **Model Discovery**: Multi-tier search strategy:
  1. `%USERPROFILE%\.flex\models\whisper\ggml-tiny.en.bin`
  2. Relative path `./models/whisper/ggml-tiny.en.bin`
  3. User-configured override in `settings.json`.

### 2.3 Transcript Review Deck & Voice FSM
Voice interactions adhere to a "human-in-the-loop" design with an authoritative C State Machine:
- When Whisper completes transcription, the result is placed in an editable input box rather than immediately submitted.
- The user can review, edit typos, or cancel before submitting.
- **C Voice FSM Invariant**: The submission must be authorized by `flex_voice_fsm_authorize_submission()` in `core-c/src/voice_fsm.c`, mathematically preventing duplicate or out-of-order submissions from reaching the backend coordinator.

### 2.4 Text-to-Speech Engine (`TextToSpeechService`)
- **Backend**: Native Microsoft Speech API (SAPI 5.4 COM interface).
- **C Normalization**: Before synthesis, text is passed through `flex_tts_normalize_text()` in `core-c/src/tts_pipeline.c`, expanding common abbreviations (`e.g.`, `i.e.`, `ms`, etc.) to enhance speech clarity.
- **Threading**: Uses a dedicated background COM thread (`COINIT_MULTITHREADED`) to eliminate UI stuttering during speech playback.
- **Utterance Queue (`tts_queue.hpp`)**: Thread-safe FIFO queue supporting sequential playback, pause, resume, and immediate flush/cancel.

---

## 3. Strict Safety Guarantees

1. **Isolation from C Core**:
   Raw audio PCM samples and audio buffers are **strictly quarantined** within `flex_voice`. The C Core (`core-c/`) accepts only validated UTF-8 text commands bounded to 1024 bytes.
2. **Deterministic Submission**:
   The `VoiceConversationController` and `core-c/src/voice_fsm.c` ensure strictly one backend submission occurs per confirmed user transcript.
3. **Graceful Degradation**:
   If the Whisper model is missing or the microphone is unavailable, the UI presents an informative diagnostic alert and falls back cleanly to typed chat without crashing.

