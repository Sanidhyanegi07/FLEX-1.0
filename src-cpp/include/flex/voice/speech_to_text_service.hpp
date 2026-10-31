#ifndef FLEX_VOICE_SPEECH_TO_TEXT_SERVICE_HPP
#define FLEX_VOICE_SPEECH_TO_TEXT_SERVICE_HPP

#include "flex/voice/audio_buffer.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace flex::voice {

enum class SpeechToTextState {
    Idle = 0,
    LoadingModel,
    Transcribing,
    Error
};

enum class SpeechToTextError {
    None = 0,
    ModelNotFound,
    ModelLoadFailed,
    InvalidAudioInput,
    TranscriptionFailed,
    Cancelled,
    Busy
};

struct TranscriptionResult {
    bool success{false};
    QString text;
    int64_t durationMs{0};
    int64_t inferenceDurationMs{0};
    SpeechToTextError error{SpeechToTextError::None};
    QString errorMessage;
    QString warningMessage;
};

/**
 * Abstract interface for Speech-to-Text inference engines.
 * Enables deterministic mock testing without GGML model files or neural inference.
 */
class ISpeechToTextEngine {
public:
    virtual ~ISpeechToTextEngine() = default;
    virtual bool loadModel(const std::string& modelPath) = 0;
    [[nodiscard]] virtual bool isModelLoaded() const = 0;
    virtual TranscriptionResult transcribe(const std::vector<float>& samples, int nThreads = 4) = 0;
    virtual void cancel() = 0;
    virtual void setLanguage(const std::string& lang) { (void)lang; }
    [[nodiscard]] virtual std::string language() const { return "en"; }
    virtual void setInitialPrompt(const std::string& prompt) { (void)prompt; }
    [[nodiscard]] virtual std::string initialPrompt() const { return ""; }
    [[nodiscard]] virtual bool isMultilingual() const { return false; }
};

class SpeechToTextServiceImpl;

/**
 * Native Speech-to-Text Service for FLEX 1.0 backed by whisper.cpp.
 *
 * Responsibilities:
 * - Loads local GGML Whisper model off the GUI thread.
 * - Accepts 16 kHz 16-bit mono normalized float samples from AudioBuffer.
 * - Executes neural speech recognition on a dedicated worker QThread.
 * - Provides thread-safe cancellation via upstream abort_callback.
 * - Rejects concurrent / overlapping requests (enforcing state integrity).
 * - Safe shutdown ensuring whisper_context is freed on its owning thread.
 * - Mockable interface for fast CI unit tests.
 */
class SpeechToTextService : public QObject {
    Q_OBJECT

public:
    explicit SpeechToTextService(
        std::shared_ptr<ISpeechToTextEngine> customEngine = nullptr,
        QObject* parent = nullptr);

    ~SpeechToTextService() override;

    // Model management
    bool loadModel(const QString& modelPath = QString());
    bool loadModelSync(const QString& modelPath = QString(), int timeoutMs = 10000);
    [[nodiscard]] bool isModelLoaded() const;
    [[nodiscard]] bool isModelLoading() const;
    [[nodiscard]] QString loadedModelPath() const;

    // Transcription
    bool transcribeAsync(const AudioBuffer& audioBuffer);
    void cancel();

    // Language & Vocabulary Configuration
    void setLanguage(const QString& language);
    [[nodiscard]] QString language() const;
    void setInitialPrompt(const QString& prompt);
    [[nodiscard]] QString initialPrompt() const;
    [[nodiscard]] bool isMultilingual() const;

    // State Inspection
    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] SpeechToTextState state() const;
    [[nodiscard]] SpeechToTextError lastError() const;
    [[nodiscard]] QString lastErrorMessage() const;

    // Engine injection for unit testing
    void setEngine(std::shared_ptr<ISpeechToTextEngine> engine);
    using EngineFactory = std::function<std::shared_ptr<ISpeechToTextEngine>()>;
    static void setDefaultEngineFactory(EngineFactory factory);
    static void resetDefaultEngineFactory();

signals:
    void stateChanged(flex::voice::SpeechToTextState newState);
    void modelLoadStarted(const QString& path);
    void modelLoadFinished(bool success, const QString& message);
    void transcriptionStarted();
    void transcriptionFinished(const QString& text, int64_t durationMs);
    void transcriptionCancelled();
    void transcriptionError(flex::voice::SpeechToTextError error, const QString& message);

private slots:
    void onWorkerFinished(const flex::voice::TranscriptionResult& result);
    void onWorkerError(flex::voice::SpeechToTextError error, const QString& message);

private:
    std::unique_ptr<SpeechToTextServiceImpl> m_impl;
};

// String helpers
const char* speechToTextStateToString(SpeechToTextState state) noexcept;
const char* speechToTextErrorToString(SpeechToTextError error) noexcept;

} // namespace flex::voice

#endif // FLEX_VOICE_SPEECH_TO_TEXT_SERVICE_HPP

