#include "flex/voice/text_to_speech_service.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <sapi.h>
#endif

#include <iostream>
#include <memory>
#include <string>

namespace flex::voice {

namespace {

#ifdef _WIN32

/**
 * Worker object living on a dedicated STA COM thread for SAPI operations.
 */
class SapiWorker : public QObject {
    Q_OBJECT

public:
    explicit SapiWorker(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~SapiWorker() override
    {
        cleanup();
    }

    bool initCom()
    {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            return false;
        }
        m_comInitialized = true;

        hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_voice);
        return SUCCEEDED(hr) && (m_voice != nullptr);
    }

    void speakText(const QString& text, bool purgePrevious)
    {
        if (!m_voice) {
            emit speakFailed("SAPI voice interface is not initialized.");
            return;
        }

        std::wstring wText = text.toStdWString();
        ULONG flags = SPF_ASYNC;
        if (purgePrevious) {
            flags |= SPF_PURGEBEFORESPEAK;
        }

        HRESULT hr = m_voice->Speak(wText.c_str(), flags, NULL);
        if (FAILED(hr)) {
            emit speakFailed(QString("SAPI Speak failed with HRESULT 0x%1").arg(hr, 8, 16, QChar('0')));
            return;
        }

        emit speakStarted();
        startPollTimer();
    }

    void stopSpeech()
    {
        if (m_voice) {
            m_voice->Speak(NULL, SPF_PURGEBEFORESPEAK, NULL);
        }
        stopPollTimer();
        emit speakStopped();
    }

    [[nodiscard]] bool checkIsSpeaking() const
    {
        if (!m_voice) {
            return false;
        }
        SPVOICESTATUS status;
        HRESULT hr = m_voice->GetStatus(&status, NULL);
        if (SUCCEEDED(hr)) {
            return (status.dwRunningState == SPRS_IS_SPEAKING);
        }
        return false;
    }

signals:
    void speakStarted();
    void speakFinished();
    void speakStopped();
    void speakFailed(const QString& errorMsg);

private slots:
    void onPollTimer()
    {
        if (!checkIsSpeaking()) {
            stopPollTimer();
            emit speakFinished();
        }
    }

private:
    void startPollTimer()
    {
        if (!m_pollTimer) {
            m_pollTimer = new QTimer(this);
            connect(m_pollTimer, &QTimer::timeout, this, &SapiWorker::onPollTimer);
        }
        m_pollTimer->start(50); // Poll every 50ms while speaking
    }

    void stopPollTimer()
    {
        if (m_pollTimer && m_pollTimer->isActive()) {
            m_pollTimer->stop();
        }
    }

    void cleanup()
    {
        stopPollTimer();
        if (m_voice) {
            m_voice->Speak(NULL, SPF_PURGEBEFORESPEAK, NULL);
            m_voice->Release();
            m_voice = nullptr;
        }
        if (m_comInitialized) {
            CoUninitialize();
            m_comInitialized = false;
        }
    }

    ISpVoice* m_voice{nullptr};
    bool m_comInitialized{false};
    QTimer* m_pollTimer{nullptr};
};

/**
 * Windows SAPI Text-to-Speech Engine Implementation.
 */
class SapiTtsEngine : public QObject, public ITextToSpeechEngine {
public:
    explicit SapiTtsEngine(QObject* parent = nullptr)
        : QObject(parent)
    {
        m_thread = new QThread(this);
        m_worker = new SapiWorker();
        m_worker->moveToThread(m_thread);

        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        m_thread->start();
    }

    ~SapiTtsEngine() override
    {
        stop();
        if (m_thread && m_thread->isRunning()) {
            m_thread->quit();
            m_thread->wait(1000);
        }
    }

    bool initialize() override
    {
        bool ok = false;
        QMetaObject::invokeMethod(m_worker, "initCom", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ok));
        m_initialized = ok;
        return ok;
    }

    bool speak(const std::string& text, bool purgePrevious) override
    {
        if (!m_initialized) {
            if (!initialize()) {
                return false;
            }
        }

        QString qText = QString::fromStdString(text);
        QMetaObject::invokeMethod(m_worker, "speakText", Qt::QueuedConnection,
                                  Q_ARG(QString, qText),
                                  Q_ARG(bool, purgePrevious));
        return true;
    }

    void stop() override
    {
        if (m_worker && m_thread && m_thread->isRunning()) {
            QMetaObject::invokeMethod(m_worker, "stopSpeech", Qt::BlockingQueuedConnection);
        }
    }

    [[nodiscard]] bool isSpeaking() const override
    {
        if (!m_worker || !m_thread || !m_thread->isRunning()) {
            return false;
        }
        bool speaking = false;
        QMetaObject::invokeMethod(m_worker, "checkIsSpeaking", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, speaking));
        return speaking;
    }

    SapiWorker* worker() const { return m_worker; }

private:
    QThread* m_thread{nullptr};
    SapiWorker* m_worker{nullptr};
    bool m_initialized{false};
};

#else

// Fallback dummy for non-Windows platforms
class SapiTtsEngine : public ITextToSpeechEngine {
public:
    bool initialize() override { return true; }
    bool speak(const std::string&, bool) override { return true; }
    void stop() override {}
    [[nodiscard]] bool isSpeaking() const override { return false; }
};

#endif

} // anonymous namespace

// -----------------------------------------------------------------------------
// TextToSpeechServiceImpl
// -----------------------------------------------------------------------------

class TextToSpeechServiceImpl {
public:
    explicit TextToSpeechServiceImpl(std::shared_ptr<ITextToSpeechEngine> engine)
        : m_engine(std::move(engine))
    {
        if (!m_engine) {
            m_engine = std::make_shared<SapiTtsEngine>();
        }
    }

    std::shared_ptr<ITextToSpeechEngine> m_engine;
    TtsState m_state{TtsState::Idle};
    TtsError m_lastError{TtsError::None};
    QString m_lastErrorMessage;
};

// -----------------------------------------------------------------------------
// TextToSpeechService
// -----------------------------------------------------------------------------

TextToSpeechService::TextToSpeechService(
    std::shared_ptr<ITextToSpeechEngine> customEngine,
    QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<TextToSpeechServiceImpl>(std::move(customEngine)))
{
}

TextToSpeechService::~TextToSpeechService()
{
    stop();
}

bool TextToSpeechService::speak(const QString& text, bool purgePrevious)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        m_impl->m_lastError = TtsError::None;
        return false;
    }

    if (!m_impl->m_engine) {
        m_impl->m_state = TtsState::Error;
        m_impl->m_lastError = TtsError::InitializationFailed;
        m_impl->m_lastErrorMessage = "Speech synthesis engine is not available.";
        emit stateChanged(m_impl->m_state);
        emit speechError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    bool ok = m_impl->m_engine->speak(trimmed.toStdString(), purgePrevious);
    if (!ok) {
        m_impl->m_state = TtsState::Error;
        m_impl->m_lastError = TtsError::SynthesisFailed;
        m_impl->m_lastErrorMessage = "Failed to synthesize speech for the requested text.";
        emit stateChanged(m_impl->m_state);
        emit speechError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    m_impl->m_state = TtsState::Speaking;
    emit stateChanged(m_impl->m_state);
    emit speechStarted();
    return true;
}

void TextToSpeechService::stop()
{
    if (m_impl->m_engine) {
        m_impl->m_engine->stop();
    }

    if (m_impl->m_state == TtsState::Speaking) {
        m_impl->m_state = TtsState::Idle;
        emit stateChanged(m_impl->m_state);
        emit speechCancelled();
    }
}

bool TextToSpeechService::isSpeaking() const
{
    if (m_impl->m_engine) {
        return m_impl->m_engine->isSpeaking();
    }
    return false;
}

TtsState TextToSpeechService::state() const
{
    return m_impl->m_state;
}

TtsError TextToSpeechService::lastError() const
{
    return m_impl->m_lastError;
}

QString TextToSpeechService::lastErrorMessage() const
{
    return m_impl->m_lastErrorMessage;
}

void TextToSpeechService::setEngine(std::shared_ptr<ITextToSpeechEngine> engine)
{
    stop();
    m_impl->m_engine = std::move(engine);
}

const char* ttsStateToString(TtsState state) noexcept
{
    switch (state) {
        case TtsState::Idle:     return "Idle";
        case TtsState::Speaking: return "Speaking";
        case TtsState::Error:    return "Error";
    }
    return "Unknown";
}

const char* ttsErrorToString(TtsError error) noexcept
{
    switch (error) {
        case TtsError::None:                 return "None";
        case TtsError::InitializationFailed: return "InitializationFailed";
        case TtsError::SynthesisFailed:      return "SynthesisFailed";
        case TtsError::Cancelled:            return "Cancelled";
    }
    return "Unknown";
}

} // namespace flex::voice

#include "text_to_speech_service.moc"

