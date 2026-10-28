#ifndef FLEX_VOICE_VOICE_CONVERSATION_CONTROLLER_HPP
#define FLEX_VOICE_VOICE_CONVERSATION_CONTROLLER_HPP

#include "flex/backend.hpp"
#include "flex/voice/audio_buffer.hpp"
#include "flex/voice/audio_capture_service.hpp"
#include "flex/voice/speech_to_text_service.hpp"
#include "flex/voice/text_to_speech_service.hpp"

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <string>

namespace flex::voice {

/**
 * High-level state of the native voice conversation pipeline.
 */
enum class VoiceSessionState {
    Idle = 0,       // Ready to record or chat
    Recording,      // Microphone actively recording audio
    Transcribing,   // AudioBuffer being recognized by whisper.cpp
    Reviewing,      // Transcript ready in UI; awaiting user review/confirmation
    Thinking,       // Confirmed prompt submitted to BackendCoordinator & Ollama
    Speaking,       // Assistant response being spoken via TextToSpeechService
    Error           // An error occurred during capture, STT, chat, or TTS
};

/**
 * Converts VoiceSessionState enum to a readable C string.
 */
const char* voiceSessionStateToString(VoiceSessionState state) noexcept;

/**
 * Native Voice Conversation Controller for FLEX 1.0.
 *
 * Responsibilities:
 * - Orchestrates AudioCaptureService, SpeechToTextService, TextToSpeechService,
 *   and BackendCoordinator into a unified voice conversation pipeline.
 * - Enforces C core lifecycle authority by dispatching "voice" operations for
 *   recording/transcription and "chat" operations for AI inference.
 * - Prevents overlapping incompatible operations (e.g. recording while chatting).
 * - Enforces review/editing before sending by default, while supporting safe hands-free mode.
 * - Streams assistant responses to persistent SQLite storage via BackendCoordinator.
 * - Handles optional Text-to-Speech playback of assistant responses.
 * - Fully asynchronous: zero blocking operations on the GUI thread.
 * - Supports dependency injection of mocks for 100% deterministic testing.
 */
class VoiceConversationController : public QObject {
    Q_OBJECT

public:
    explicit VoiceConversationController(
        std::shared_ptr<BackendCoordinator> coordinator,
        std::shared_ptr<AudioCaptureService> audioCapture = nullptr,
        std::shared_ptr<SpeechToTextService> speechToText = nullptr,
        std::shared_ptr<TextToSpeechService> textToSpeech = nullptr,
        QObject* parent = nullptr);

    ~VoiceConversationController() override;

    // Non-copyable, non-movable QObject
    VoiceConversationController(const VoiceConversationController&) = delete;
    VoiceConversationController& operator=(const VoiceConversationController&) = delete;
    VoiceConversationController(VoiceConversationController&&) = delete;
    VoiceConversationController& operator=(VoiceConversationController&&) = delete;

    // Configuration
    [[nodiscard]] bool isVoiceOutputEnabled() const noexcept;
    void setVoiceOutputEnabled(bool enabled);

    [[nodiscard]] bool isAutoSubmitEnabled() const noexcept;
    void setAutoSubmitEnabled(bool enabled);

    // State inspection
    [[nodiscard]] VoiceSessionState state() const noexcept;
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] QString lastTranscript() const;
    [[nodiscard]] QString lastErrorMessage() const;
    [[nodiscard]] QString lastAssistantResponse() const;

    // Subsystem accessors
    [[nodiscard]] std::shared_ptr<AudioCaptureService> audioCaptureService() const noexcept;
    [[nodiscard]] std::shared_ptr<SpeechToTextService> speechToTextService() const noexcept;
    [[nodiscard]] std::shared_ptr<TextToSpeechService> textToSpeechService() const noexcept;
    [[nodiscard]] std::shared_ptr<BackendCoordinator> coordinator() const noexcept;

    // Voice lifecycle actions
    bool startRecording();
    void stopRecording();
    void cancelVoiceInput();
    bool submitTranscript(const QString& transcriptText = QString{});
    void stopSpeaking();
    void cancelAll();

signals:
    void stateChanged(flex::voice::VoiceSessionState newState);
    void recordingStarted();
    void recordingFinished(const flex::voice::AudioBuffer& buffer);
    void transcriptionStarted();
    void transcriptReadyForReview(const QString& text);
    void chatTurnStarted(const QString& prompt);
    void chatTurnFinished(const QString& assistantResponse);
    void speechStarted();
    void speechFinished();
    void errorOccurred(const QString& errorMessage);
    void voiceOutputToggled(bool enabled);

private slots:
    void onCaptureStarted();
    void onCaptureFinished(const flex::voice::AudioBuffer& buffer);
    void onCaptureCancelled();
    void onCaptureError(flex::voice::AudioCaptureError error, const QString& message);

    void onModelLoadFinished(bool success, const QString& message);
    void onSttFinished(const QString& text, int64_t durationMs);
    void onSttCancelled();
    void onSttError(flex::voice::SpeechToTextError error, const QString& message);

    void onChatFinished(uint64_t requestId, const flex::ChatResult& result);
    void onChatError(uint64_t requestId, const flex::BackendError& error);

    void onTtsStarted();
    void onTtsFinished();
    void onTtsCancelled();
    void onTtsError(flex::voice::TtsError error, const QString& message);

private:
    void finishVoiceCoreOperation();
    void setState(VoiceSessionState newState);

    std::shared_ptr<BackendCoordinator> m_coordinator;
    std::shared_ptr<AudioCaptureService> m_audioCapture;
    std::shared_ptr<SpeechToTextService> m_stt;
    std::shared_ptr<TextToSpeechService> m_tts;

    VoiceSessionState m_state{VoiceSessionState::Idle};
    bool m_voiceOutputEnabled{true};
    bool m_autoSubmit{false}; // Default: user reviews/edits transcript

    bool m_voiceCoreOpActive{false};
    uint64_t m_currentChatRequestId{0};
    QString m_lastTranscript;
    QString m_lastErrorMessage;
    QString m_lastAssistantResponse;

    AudioBuffer m_pendingAudioBuffer;
    bool m_waitingForModelLoad{false};
};

} // namespace flex::voice

#endif // FLEX_VOICE_VOICE_CONVERSATION_CONTROLLER_HPP

