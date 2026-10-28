#include "flex/voice/speech_to_text_service.hpp"
#include "flex/voice/whisper_model_resolver.hpp"

#include <whisper.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

Q_DECLARE_METATYPE(flex::voice::TranscriptionResult)
Q_DECLARE_METATYPE(flex::voice::SpeechToTextError)

namespace flex::voice {

namespace {

/**
 * Native whisper.cpp engine implementation.
 */
class WhisperCppEngine : public ISpeechToTextEngine {
public:
    WhisperCppEngine()
        : m_ctx(nullptr)
        , m_abortFlag(false)
    {
    }

    ~WhisperCppEngine() override
    {
        cleanup();
    }

    bool loadModel(const std::string& modelPath) override
    {
        cleanup();

        QFileInfo info(QString::fromStdString(modelPath));
        if (!info.exists() || info.size() < 1024 * 1024) { // Must exist and be > 1 MB
            return false;
        }

        struct whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false; // Pure local CPU inference for universal compatibility

        m_ctx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
        m_modelPath = modelPath;
        return (m_ctx != nullptr);
    }

    [[nodiscard]] bool isModelLoaded() const override
    {
        return (m_ctx != nullptr);
    }

    TranscriptionResult transcribe(const std::vector<float>& samples, int nThreads) override
    {
        TranscriptionResult result;
        if (!m_ctx) {
            result.error = SpeechToTextError::ModelNotFound;
            result.errorMessage = "Whisper context is not initialized.";
            return result;
        }

        if (samples.empty()) {
            result.error = SpeechToTextError::InvalidAudioInput;
            result.errorMessage = "Audio sample buffer is empty.";
            return result;
        }

        m_abortFlag.store(false);

        auto startTime = std::chrono::steady_clock::now();

        whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.print_progress = false;
        params.print_special = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        params.translate = false;
        params.language = "en";
        params.n_threads = std::max(1, nThreads);

        // Connect abort callback for responsive mid-inference cancellation
        params.abort_callback = [](void* userData) -> bool {
            auto* flag = static_cast<std::atomic<bool>*>(userData);
            return flag ? flag->load() : false;
        };
        params.abort_callback_user_data = &m_abortFlag;

        int ret = whisper_full(m_ctx, params, samples.data(), static_cast<int>(samples.size()));

        auto endTime = std::chrono::steady_clock::now();
        result.inferenceDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        if (m_abortFlag.load()) {
            result.error = SpeechToTextError::Cancelled;
            result.errorMessage = "Transcription was cancelled by user.";
            return result;
        }

        if (ret != 0) {
            result.error = SpeechToTextError::TranscriptionFailed;
            result.errorMessage = QString("whisper_full failed with return code %1").arg(ret);
            return result;
        }

        int nSegments = whisper_full_n_segments(m_ctx);
        QString fullText;
        for (int i = 0; i < nSegments; ++i) {
            const char* segmentText = whisper_full_get_segment_text(m_ctx, i);
            if (segmentText) {
                QString seg = QString::fromUtf8(segmentText);
                seg.remove("[BLANK_AUDIO]", Qt::CaseInsensitive);
                seg.remove("[_BEG_]", Qt::CaseInsensitive);
                seg.remove("[MUSIC]", Qt::CaseInsensitive);
                fullText.append(seg);
            }
        }

        result.success = true;
        result.text = fullText.trimmed();
        return result;
    }

    void cancel() override
    {
        m_abortFlag.store(true);
    }

private:
    void cleanup()
    {
        m_abortFlag.store(true);
        if (m_ctx) {
            whisper_free(m_ctx);
            m_ctx = nullptr;
        }
        m_modelPath.clear();
    }

    whisper_context* m_ctx{nullptr};
    std::string m_modelPath;
    std::atomic<bool> m_abortFlag{false};
};

/**
 * Worker object living on a dedicated background QThread for asynchronous inference.
 */
class SpeechToTextWorker : public QObject {
    Q_OBJECT

public:
    explicit SpeechToTextWorker(std::shared_ptr<ISpeechToTextEngine> engine, QObject* parent = nullptr)
        : QObject(parent)
        , m_engine(std::move(engine))
    {
    }

    ~SpeechToTextWorker() override = default;

public slots:
    void doLoadModel(const QString& modelPath)
    {
        if (!m_engine) {
            emit modelLoadResult(false, "Speech-to-text engine is not initialized.");
            return;
        }

        bool ok = m_engine->loadModel(modelPath.toStdString());
        if (ok) {
            emit modelLoadResult(true, "Model loaded successfully.");
        } else {
            emit modelLoadResult(false, QString("Failed to load Whisper model from path: %1").arg(modelPath));
        }
    }

    void doTranscribe(const std::vector<float>& samples, int64_t audioDurationMs)
    {
        if (!m_engine) {
            TranscriptionResult res;
            res.error = SpeechToTextError::ModelNotFound;
            res.errorMessage = "Engine is not initialized.";
            emit transcriptionResult(res);
            return;
        }

        int threads = std::max(1, QThread::idealThreadCount() / 2);
        auto result = m_engine->transcribe(samples, threads);
        result.durationMs = audioDurationMs;
        emit transcriptionResult(result);
    }

    void cancel()
    {
        if (m_engine) {
            m_engine->cancel();
        }
    }

    void cleanup()
    {
        if (m_engine) {
            m_engine->cancel();
        }
    }

signals:
    void modelLoadResult(bool success, const QString& message);
    void transcriptionResult(const flex::voice::TranscriptionResult& result);

private:
    std::shared_ptr<ISpeechToTextEngine> m_engine;
};

} // anonymous namespace

// -----------------------------------------------------------------------------
// SpeechToTextServiceImpl
// -----------------------------------------------------------------------------

class SpeechToTextServiceImpl {
public:
    explicit SpeechToTextServiceImpl(std::shared_ptr<ISpeechToTextEngine> engine)
        : m_engine(std::move(engine))
    {
        if (!m_engine) {
            m_engine = std::make_shared<WhisperCppEngine>();
        }
    }

    std::shared_ptr<ISpeechToTextEngine> m_engine;
    SpeechToTextState m_state{SpeechToTextState::Idle};
    SpeechToTextError m_lastError{SpeechToTextError::None};
    QString m_lastErrorMessage;
    QString m_loadedModelPath;

    QThread* m_workerThread{nullptr};
    SpeechToTextWorker* m_worker{nullptr};
};

// -----------------------------------------------------------------------------
// SpeechToTextService
// -----------------------------------------------------------------------------

SpeechToTextService::SpeechToTextService(
    std::shared_ptr<ISpeechToTextEngine> customEngine,
    QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<SpeechToTextServiceImpl>(std::move(customEngine)))
{
    qRegisterMetaType<flex::voice::TranscriptionResult>("flex::voice::TranscriptionResult");
    qRegisterMetaType<flex::voice::SpeechToTextError>("flex::voice::SpeechToTextError");

    m_impl->m_workerThread = new QThread(this);
    m_impl->m_worker = new SpeechToTextWorker(m_impl->m_engine);
    m_impl->m_worker->moveToThread(m_impl->m_workerThread);

    connect(m_impl->m_workerThread, &QThread::finished, m_impl->m_worker, &QObject::deleteLater);

    connect(m_impl->m_worker, &SpeechToTextWorker::modelLoadResult,
            this, [this](bool ok, const QString& msg) {
        m_impl->m_state = ok ? SpeechToTextState::Idle : SpeechToTextState::Error;
        if (!ok) {
            m_impl->m_lastError = SpeechToTextError::ModelLoadFailed;
            m_impl->m_lastErrorMessage = msg;
        } else {
            m_impl->m_lastError = SpeechToTextError::None;
            m_impl->m_lastErrorMessage.clear();
        }
        emit stateChanged(m_impl->m_state);
        emit modelLoadFinished(ok, msg);
    });

    connect(m_impl->m_worker, &SpeechToTextWorker::transcriptionResult,
            this, &SpeechToTextService::onWorkerFinished);

    m_impl->m_workerThread->start();
}

SpeechToTextService::~SpeechToTextService()
{
    cancel();

    if (m_impl->m_worker && m_impl->m_workerThread && m_impl->m_workerThread->isRunning()) {
        QMetaObject::invokeMethod(m_impl->m_worker, "cleanup", Qt::BlockingQueuedConnection);
        m_impl->m_workerThread->quit();
        m_impl->m_workerThread->wait(2000);
    }
}

bool SpeechToTextService::loadModel(const QString& modelPath)
{
    if (m_impl->m_state == SpeechToTextState::Transcribing ||
        m_impl->m_state == SpeechToTextState::LoadingModel) {
        m_impl->m_lastError = SpeechToTextError::Busy;
        m_impl->m_lastErrorMessage = "Speech-to-text service is currently busy.";
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    auto resolution = WhisperModelResolver::resolveModelPath(modelPath);
    if (!resolution.isFound()) {
        m_impl->m_state = SpeechToTextState::Error;
        if (resolution.status == ModelResolutionStatus::Missing) {
            m_impl->m_lastError = SpeechToTextError::ModelNotFound;
        } else {
            m_impl->m_lastError = SpeechToTextError::ModelLoadFailed;
        }
        m_impl->m_lastErrorMessage = resolution.errorMessage;
        emit stateChanged(m_impl->m_state);
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    QString targetPath = resolution.resolvedPath;
    m_impl->m_state = SpeechToTextState::LoadingModel;
    m_impl->m_loadedModelPath = targetPath;
    m_impl->m_lastError = SpeechToTextError::None;
    m_impl->m_lastErrorMessage.clear();

    emit stateChanged(m_impl->m_state);
    emit modelLoadStarted(targetPath);

    QMetaObject::invokeMethod(m_impl->m_worker, "doLoadModel", Qt::QueuedConnection,
                              Q_ARG(QString, targetPath));
    return true;
}

bool SpeechToTextService::loadModelSync(const QString& modelPath, int timeoutMs)
{
    if (isModelLoaded()) {
        if (modelPath.isEmpty() || modelPath == loadedModelPath()) {
            return true;
        }
    }

    bool success = false;
    bool finished = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    auto conn = connect(this, &SpeechToTextService::modelLoadFinished,
                        [&](bool ok, const QString&) {
        success = ok;
        finished = true;
        loop.quit();
    });

    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    if (!loadModel(modelPath)) {
        disconnect(conn);
        return false;
    }

    timer.start(timeoutMs);
    loop.exec();
    disconnect(conn);

    return finished && success;
}

bool SpeechToTextService::isModelLoaded() const
{
    return m_impl->m_engine && m_impl->m_engine->isModelLoaded();
}

bool SpeechToTextService::isModelLoading() const
{
    return m_impl->m_state == SpeechToTextState::LoadingModel;
}

QString SpeechToTextService::loadedModelPath() const
{
    return m_impl->m_loadedModelPath;
}

bool SpeechToTextService::transcribeAsync(const AudioBuffer& audioBuffer)
{
    if (m_impl->m_state == SpeechToTextState::Transcribing ||
        m_impl->m_state == SpeechToTextState::LoadingModel) {
        m_impl->m_lastError = SpeechToTextError::Busy;
        m_impl->m_lastErrorMessage = "Speech-to-text service is currently transcribing or loading a model.";
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    // Input Validation
    if (audioBuffer.isEmpty()) {
        m_impl->m_state = SpeechToTextState::Error;
        m_impl->m_lastError = SpeechToTextError::InvalidAudioInput;
        m_impl->m_lastErrorMessage = "Audio buffer contains no samples.";
        emit stateChanged(m_impl->m_state);
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    if (audioBuffer.sampleRate != 16000) {
        m_impl->m_state = SpeechToTextState::Error;
        m_impl->m_lastError = SpeechToTextError::InvalidAudioInput;
        m_impl->m_lastErrorMessage = QString("Sample rate must be 16000 Hz (got %1 Hz).").arg(audioBuffer.sampleRate);
        emit stateChanged(m_impl->m_state);
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    if (audioBuffer.channelCount != 1) {
        m_impl->m_state = SpeechToTextState::Error;
        m_impl->m_lastError = SpeechToTextError::InvalidAudioInput;
        m_impl->m_lastErrorMessage = QString("Channel count must be 1 (mono, got %1).").arg(audioBuffer.channelCount);
        emit stateChanged(m_impl->m_state);
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    for (float s : audioBuffer.samples) {
        if (std::isnan(s) || std::isinf(s) || s < -10.0f || s > 10.0f) {
            m_impl->m_state = SpeechToTextState::Error;
            m_impl->m_lastError = SpeechToTextError::InvalidAudioInput;
            m_impl->m_lastErrorMessage = "Audio buffer contains non-finite or out-of-range samples.";
            emit stateChanged(m_impl->m_state);
            emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
            return false;
        }
    }

    if (!isModelLoaded()) {
        m_impl->m_state = SpeechToTextState::Error;
        m_impl->m_lastError = SpeechToTextError::ModelNotFound;
        m_impl->m_lastErrorMessage = "Whisper speech model has not been loaded. Please download and load a model.";
        emit stateChanged(m_impl->m_state);
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    m_impl->m_state = SpeechToTextState::Transcribing;
    m_impl->m_lastError = SpeechToTextError::None;
    m_impl->m_lastErrorMessage.clear();

    emit stateChanged(m_impl->m_state);
    emit transcriptionStarted();

    // Pass safe value-copy of normalized samples into worker thread
    QMetaObject::invokeMethod(m_impl->m_worker, "doTranscribe", Qt::QueuedConnection,
                              Q_ARG(std::vector<float>, audioBuffer.samples),
                              Q_ARG(int64_t, audioBuffer.durationMs));
    return true;
}

void SpeechToTextService::cancel()
{
    if (m_impl->m_worker) {
        m_impl->m_worker->cancel();
    }

    if (m_impl->m_state == SpeechToTextState::Transcribing) {
        m_impl->m_state = SpeechToTextState::Idle;
        emit stateChanged(m_impl->m_state);
        emit transcriptionCancelled();
    }
}

void SpeechToTextService::onWorkerFinished(const TranscriptionResult& result)
{
    if (m_impl->m_state != SpeechToTextState::Transcribing) {
        return; // Stale or already cancelled
    }

    if (result.error == SpeechToTextError::Cancelled) {
        m_impl->m_state = SpeechToTextState::Idle;
        emit stateChanged(m_impl->m_state);
        emit transcriptionCancelled();
        return;
    }

    if (!result.success) {
        m_impl->m_state = SpeechToTextState::Error;
        m_impl->m_lastError = result.error;
        m_impl->m_lastErrorMessage = result.errorMessage;
        emit stateChanged(m_impl->m_state);
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return;
    }

    m_impl->m_state = SpeechToTextState::Idle;
    emit stateChanged(m_impl->m_state);
    emit transcriptionFinished(result.text, result.durationMs);
}

void SpeechToTextService::onWorkerError(SpeechToTextError error, const QString& message)
{
    m_impl->m_state = SpeechToTextState::Error;
    m_impl->m_lastError = error;
    m_impl->m_lastErrorMessage = message;
    emit stateChanged(m_impl->m_state);
    emit transcriptionError(error, message);
}

bool SpeechToTextService::isBusy() const
{
    return m_impl->m_state == SpeechToTextState::Transcribing ||
           m_impl->m_state == SpeechToTextState::LoadingModel;
}

SpeechToTextState SpeechToTextService::state() const
{
    return m_impl->m_state;
}

SpeechToTextError SpeechToTextService::lastError() const
{
    return m_impl->m_lastError;
}

QString SpeechToTextService::lastErrorMessage() const
{
    return m_impl->m_lastErrorMessage;
}

void SpeechToTextService::setEngine(std::shared_ptr<ISpeechToTextEngine> engine)
{
    cancel();
    m_impl->m_engine = std::move(engine);

    if (m_impl->m_worker && m_impl->m_workerThread && m_impl->m_workerThread->isRunning()) {
        QMetaObject::invokeMethod(m_impl->m_worker, "cleanup", Qt::BlockingQueuedConnection);
        m_impl->m_workerThread->quit();
        m_impl->m_workerThread->wait(2000);
    }

    m_impl->m_workerThread = new QThread(this);
    m_impl->m_worker = new SpeechToTextWorker(m_impl->m_engine);
    m_impl->m_worker->moveToThread(m_impl->m_workerThread);

    connect(m_impl->m_workerThread, &QThread::finished, m_impl->m_worker, &QObject::deleteLater);

    connect(m_impl->m_worker, &SpeechToTextWorker::modelLoadResult,
            this, [this](bool ok, const QString& msg) {
        m_impl->m_state = ok ? SpeechToTextState::Idle : SpeechToTextState::Error;
        if (!ok) {
            m_impl->m_lastError = SpeechToTextError::ModelLoadFailed;
            m_impl->m_lastErrorMessage = msg;
        } else {
            m_impl->m_lastError = SpeechToTextError::None;
            m_impl->m_lastErrorMessage.clear();
        }
        emit stateChanged(m_impl->m_state);
        emit modelLoadFinished(ok, msg);
    });

    connect(m_impl->m_worker, &SpeechToTextWorker::transcriptionResult,
            this, &SpeechToTextService::onWorkerFinished);

    m_impl->m_workerThread->start();
}

const char* speechToTextStateToString(SpeechToTextState state) noexcept
{
    switch (state) {
        case SpeechToTextState::Idle:         return "Idle";
        case SpeechToTextState::LoadingModel: return "LoadingModel";
        case SpeechToTextState::Transcribing: return "Transcribing";
        case SpeechToTextState::Error:        return "Error";
    }
    return "Unknown";
}

const char* speechToTextErrorToString(SpeechToTextError error) noexcept
{
    switch (error) {
        case SpeechToTextError::None:                return "None";
        case SpeechToTextError::ModelNotFound:       return "ModelNotFound";
        case SpeechToTextError::ModelLoadFailed:     return "ModelLoadFailed";
        case SpeechToTextError::InvalidAudioInput:   return "InvalidAudioInput";
        case SpeechToTextError::TranscriptionFailed: return "TranscriptionFailed";
        case SpeechToTextError::Cancelled:           return "Cancelled";
        case SpeechToTextError::Busy:                return "Busy";
    }
    return "Unknown";
}

} // namespace flex::voice

#include "speech_to_text_service.moc"

