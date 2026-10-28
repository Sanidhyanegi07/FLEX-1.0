#ifndef FLEX_VOICE_TEXT_TO_SPEECH_SERVICE_HPP
#define FLEX_VOICE_TEXT_TO_SPEECH_SERVICE_HPP

#include <QObject>
#include <QString>

#include <memory>
#include <string>
#include <vector>

namespace flex::voice {

enum class TtsState {
    Idle = 0,
    Speaking,
    Error
};

enum class TtsError {
    None = 0,
    InitializationFailed,
    SynthesisFailed,
    Cancelled
};

/**
 * Abstract interface for speech synthesis engines.
 * Enables deterministic mock testing without COM or audio hardware in CI.
 */
class ITextToSpeechEngine {
public:
    virtual ~ITextToSpeechEngine() = default;
    virtual bool initialize() = 0;
    virtual bool speak(const std::string& text, bool purgePrevious) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isSpeaking() const = 0;
};

class TextToSpeechServiceImpl;

/**
 * Native Text-to-Speech Service for FLEX 1.0.
 *
 * Responsibilities:
 * - Synthesizes spoken audio from text using native Windows SAPI (ISpVoice).
 * - Enforces strict COM apartment threading on a dedicated background thread.
 * - Supports instant cancellation and speech queue purging via SPF_PURGEBEFORESPEAK.
 * - Exposes mockable engine interface for CI testing.
 * - Safe lifecycle handling on application shutdown.
 */
class TextToSpeechService : public QObject {
    Q_OBJECT

public:
    explicit TextToSpeechService(
        std::shared_ptr<ITextToSpeechEngine> customEngine = nullptr,
        QObject* parent = nullptr);

    ~TextToSpeechService() override;

    // Speech Controls
    bool speak(const QString& text, bool purgePrevious = true);
    void stop();

    // State Inspection
    [[nodiscard]] bool isSpeaking() const;
    [[nodiscard]] TtsState state() const;
    [[nodiscard]] TtsError lastError() const;
    [[nodiscard]] QString lastErrorMessage() const;

    // Engine Injection
    void setEngine(std::shared_ptr<ITextToSpeechEngine> engine);

signals:
    void stateChanged(flex::voice::TtsState newState);
    void speechStarted();
    void speechFinished();
    void speechCancelled();
    void speechError(flex::voice::TtsError error, const QString& message);

private:
    std::unique_ptr<TextToSpeechServiceImpl> m_impl;
};

// String helpers
const char* ttsStateToString(TtsState state) noexcept;
const char* ttsErrorToString(TtsError error) noexcept;

} // namespace flex::voice

#endif // FLEX_VOICE_TEXT_TO_SPEECH_SERVICE_HPP

