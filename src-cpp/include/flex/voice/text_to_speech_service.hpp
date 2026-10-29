#ifndef FLEX_VOICE_TEXT_TO_SPEECH_SERVICE_HPP
#define FLEX_VOICE_TEXT_TO_SPEECH_SERVICE_HPP

#include <QObject>
#include <QString>

#include <functional>
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
    virtual void setRate(int rate) { (void)rate; }
    virtual void setVolume(int volume) { (void)volume; }
    [[nodiscard]] virtual int rate() const { return 0; }
    [[nodiscard]] virtual int volume() const { return 100; }

    void setCallbacks(
        std::function<void()> onStarted,
        std::function<void()> onFinished,
        std::function<void()> onStopped,
        std::function<void(const std::string&)> onError)
    {
        m_onStarted = std::move(onStarted);
        m_onFinished = std::move(onFinished);
        m_onStopped = std::move(onStopped);
        m_onError = std::move(onError);
    }

    void notifyStarted() { if (m_onStarted) m_onStarted(); }
    void notifyFinished() { if (m_onFinished) m_onFinished(); }
    void notifyStopped() { if (m_onStopped) m_onStopped(); }
    void notifyError(const std::string& msg) { if (m_onError) m_onError(msg); }

protected:
    std::function<void()> m_onStarted;
    std::function<void()> m_onFinished;
    std::function<void()> m_onStopped;
    std::function<void(const std::string&)> m_onError;
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
    void setRate(int rate);
    [[nodiscard]] int rate() const;
    void setVolume(int volume);
    [[nodiscard]] int volume() const;

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

