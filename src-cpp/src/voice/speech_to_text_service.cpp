#include "flex/voice/speech_to_text_service.hpp"
#include "flex/voice/whisper_model_resolver.hpp"
#include "whisper_pipeline.h"
#include <mutex>
#include <atomic>
#include <cstring>

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
        std::cout << "[SpeechToText] Attempting to load Whisper model from: " << modelPath << "\n";
        QFileInfo info(QString::fromStdString(modelPath));
        if (!info.exists() || info.size() < 1024 * 1024) { // Must exist and be > 1 MB
            std::cout << "[SpeechToText] Model file does not exist or is too small (<1MB): " << modelPath << "\n";
            return false;
        }

        struct whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false; // Pure local CPU inference for universal compatibility

        whisper_context* newCtx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
        if (!newCtx) {
            std::cout << "[SpeechToText] Model initialization: FAILED (existing model preserved)\n";
            return false;
        }

        // Two-phase commit: free old context only after new context successfully loads
        {
            std::lock_guard<std::mutex> lock(m_engineMutex);
            if (m_ctx) {
                whisper_free(m_ctx);
                m_ctx = nullptr;
            }
            m_ctx = newCtx;
            m_modelPath = modelPath;
            m_abortFlag.store(false);
        }
        std::cout << "[SpeechToText] Model initialization: SUCCESS\n";
        return true;
    }

    [[nodiscard]] bool isModelLoaded() const override
    {
        std::lock_guard<std::mutex> lock(m_engineMutex);
        return (m_ctx != nullptr);
    }

    void setLanguage(const std::string& lang) override
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_language = lang.empty() ? "en" : lang;
    }

    [[nodiscard]] std::string language() const override
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        return m_language;
    }

    void setInitialPrompt(const std::string& prompt) override
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_initialPrompt = prompt;
    }

    [[nodiscard]] std::string initialPrompt() const override
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        return m_initialPrompt;
    }

    [[nodiscard]] bool isMultilingual() const override
    {
        std::lock_guard<std::mutex> lock(m_engineMutex);
        return (m_ctx != nullptr && whisper_is_multilingual(m_ctx) != 0);
    }

    TranscriptionResult transcribe(const std::vector<float>& samples, int nThreads) override
    {
        std::lock_guard<std::mutex> lock(m_engineMutex);
        TranscriptionResult result;
        if (!m_ctx) {
            result.error = SpeechToTextError::ModelNotFound;
            result.errorMessage = "Whisper context is not initialized.";
            std::cout << "[SpeechToText] Transcribe error: Whisper context is null.\n";
            return result;
        }

        if (samples.empty()) {
            result.error = SpeechToTextError::InvalidAudioInput;
            result.errorMessage = "Audio sample buffer is empty.";
            std::cout << "[SpeechToText] Transcribe error: audio samples vector is empty.\n";
            return result;
        }

        // Padding to at least 16,000 samples (1 second) to prevent Whisper encoder short input warning
        std::vector<float> audioData;
        const float* audioPtr = samples.data();
        int sampleCount = static_cast<int>(samples.size());
        if (sampleCount < 16000) {
            audioData = samples;
            audioData.resize(16000, 0.0f);
            audioPtr = audioData.data();
            sampleCount = 16000;
        }

        m_abortFlag.store(false);

        std::string currentLang;
        std::string currentPrompt;
        {
            std::lock_guard<std::mutex> configLock(m_configMutex);
            currentLang = m_language;
            currentPrompt = m_initialPrompt;
        }

        bool isMulti = (m_ctx != nullptr && whisper_is_multilingual(m_ctx) != 0);
        flex_whisper_lang_config_t langCfg;
        flex_whisper_resolve_language(currentLang.c_str(), isMulti, &langCfg);

        if (langCfg.fallback_applied) {
            std::cout << "[SpeechToText] WARNING: " << langCfg.warning_message << "\n";
            result.warningMessage = QString::fromUtf8(langCfg.warning_message);
        }

        int threads = std::max(1, nThreads);
        std::cout << "[SpeechToText] Starting Whisper inference on " << sampleCount << " samples ("
                  << (sampleCount * 1000 / 16000) << " ms) using " << threads << " CPU threads, language: '"
                  << langCfg.effective_language << "'...\n";

        auto startTime = std::chrono::steady_clock::now();

        whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.print_progress = false;
        params.print_special = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        params.translate = false;

        // Configure language and automatic detection correctly
        bool isAuto = (std::strcmp(langCfg.effective_language, "auto") == 0);
        if (isAuto) {
            params.language = "auto";
            params.detect_language = true;
        } else {
            params.language = langCfg.effective_language;
            params.detect_language = false;
        }
        params.n_threads = threads;

        char promptBuf[512];
        flex_whisper_build_default_prompt(currentPrompt.c_str(), promptBuf, sizeof(promptBuf));
        std::string effectivePrompt = promptBuf;
        if (!effectivePrompt.empty()) {
            params.initial_prompt = effectivePrompt.c_str();
        }

        // Connect abort callback for responsive mid-inference cancellation
        params.abort_callback = [](void* userData) -> bool {
            auto* flag = static_cast<std::atomic<bool>*>(userData);
            return flag ? flag->load() : false;
        };
        params.abort_callback_user_data = &m_abortFlag;

        int ret = whisper_full(m_ctx, params, audioPtr, sampleCount);

        auto endTime = std::chrono::steady_clock::now();
        result.inferenceDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        if (m_abortFlag.load()) {
            result.error = SpeechToTextError::Cancelled;
            result.errorMessage = "Transcription was cancelled by user.";
            std::cout << "[SpeechToText] Inference cancelled by user after " << result.inferenceDurationMs << " ms.\n";
            return result;
        }

        if (ret != 0) {
            result.error = SpeechToTextError::TranscriptionFailed;
            result.errorMessage = QString("whisper_full failed with return code %1").arg(ret);
            std::cout << "[SpeechToText] whisper_full failed with return code " << ret << "\n";
            return result;
        }

        int nSegments = whisper_full_n_segments(m_ctx);
        std::cout << "[SpeechToText] Inference completed in " << result.inferenceDurationMs
                  << " ms, segments extracted: " << nSegments << "\n";

        QString rawFullText;
        QString cleanedFullText;

        for (int i = 0; i < nSegments; ++i) {
            const char* segmentText = whisper_full_get_segment_text(m_ctx, i);
            if (segmentText) {
                QString rawSeg = QString::fromUtf8(segmentText);
                rawFullText.append(rawSeg);

                QString cleanedSeg = rawSeg;
                cleanedSeg.remove("[BLANK_AUDIO]", Qt::CaseInsensitive);
                cleanedSeg.remove("[_BEG_]", Qt::CaseInsensitive);
                cleanedSeg.remove("[MUSIC]", Qt::CaseInsensitive);
                cleanedFullText.append(cleanedSeg);

                std::cout << "[SpeechToText]   Segment [" << i << "]: raw=\""
                          << rawSeg.toStdString() << "\", cleaned=\"" << cleanedSeg.toStdString() << "\"\n";
            }
        }

        result.success = true;
        result.text = cleanedFullText.trimmed();
        std::cout << "[SpeechToText] Raw transcription: \"" << rawFullText.trimmed().toStdString() << "\"\n";
        std::cout << "[SpeechToText] Final cleaned transcription: \"" << result.text.toStdString() << "\"\n";
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
        std::lock_guard<std::mutex> lock(m_engineMutex);
        if (m_ctx) {
            whisper_free(m_ctx);
            m_ctx = nullptr;
        }
        m_modelPath.clear();
    }

    whisper_context* m_ctx{nullptr};
    std::string m_modelPath;
    std::atomic<bool> m_abortFlag{false};
    mutable std::mutex m_engineMutex;
    mutable std::mutex m_configMutex;
    std::string m_language{"en"};
    std::string m_initialPrompt{""};
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

    void setLatestRequestId(quint64 reqId)
    {
        m_latestQueuedRequestId.store(reqId);
    }

    void setShuttingDown(bool shuttingDown)
    {
        m_isShuttingDown.store(shuttingDown);
        if (m_engine) {
            m_engine->cancel();
        }
    }

    [[nodiscard]] bool isShuttingDown() const
    {
        return m_isShuttingDown.load();
    }

public slots:
    void doLoadModel(const QString& modelPath, quint64 requestId)
    {
        if (m_isShuttingDown.load()) {
            return;
        }

        if (!m_engine) {
            emit modelLoadResult(false, "Speech-to-text engine is not initialized.", requestId, modelPath);
            return;
        }

        // Check if request is already obsolete before starting expensive load
        if (m_isShuttingDown.load() || requestId != m_latestQueuedRequestId.load()) {
            std::cout << "[SpeechToText] Skipping obsolete model load request #" << requestId
                      << " (current active request: #" << m_latestQueuedRequestId.load() << ")\n";
            if (!m_isShuttingDown.load()) {
                emit modelLoadResult(false, "Model load request was superseded by a newer request.", requestId, modelPath);
            }
            return;
        }

        bool ok = m_engine->loadModel(modelPath.toStdString());

        // Check if request was superseded or service is shutting down while loading
        if (m_isShuttingDown.load() || requestId != m_latestQueuedRequestId.load()) {
            std::cout << "[SpeechToText] Discarding obsolete model load result for request #" << requestId
                      << " (current active request: #" << m_latestQueuedRequestId.load() << ")\n";
            if (!m_isShuttingDown.load()) {
                emit modelLoadResult(false, "Model load request was superseded by a newer request.", requestId, modelPath);
            }
            return;
        }

        if (ok) {
            emit modelLoadResult(true, "Model loaded successfully.", requestId, modelPath);
        } else {
            emit modelLoadResult(false, QString("Failed to load Whisper model from path: %1").arg(modelPath), requestId, modelPath);
        }
    }

    void doTranscribe(const std::vector<float>& samples, int64_t audioDurationMs)
    {
        if (m_isShuttingDown.load()) {
            return;
        }

        if (!m_engine) {
            TranscriptionResult res;
            res.error = SpeechToTextError::ModelNotFound;
            res.errorMessage = "Engine is not initialized.";
            emit transcriptionResult(res);
            return;
        }

        int threads = std::max(1, QThread::idealThreadCount() / 2);
        auto result = m_engine->transcribe(samples, threads);
        if (m_isShuttingDown.load()) {
            return;
        }
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

    void setLanguage(const QString& lang)
    {
        if (m_engine) {
            m_engine->setLanguage(lang.toStdString());
        }
    }

    void setInitialPrompt(const QString& prompt)
    {
        if (m_engine) {
            m_engine->setInitialPrompt(prompt.toStdString());
        }
    }

signals:
    void modelLoadResult(bool success, const QString& message, quint64 requestId, const QString& attemptedPath);
    void transcriptionResult(const flex::voice::TranscriptionResult& result);

private:
    std::shared_ptr<ISpeechToTextEngine> m_engine;
    std::atomic<quint64> m_latestQueuedRequestId{0};
    std::atomic<bool> m_isShuttingDown{false};
};

static SpeechToTextService::EngineFactory s_defaultEngineFactory = nullptr;

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
            if (s_defaultEngineFactory) {
                m_engine = s_defaultEngineFactory();
            } else {
                m_engine = std::make_shared<WhisperCppEngine>();
            }
        }
    }

    std::shared_ptr<ISpeechToTextEngine> m_engine;
    SpeechToTextState m_state{SpeechToTextState::Idle};
    SpeechToTextError m_lastError{SpeechToTextError::None};
    QString m_lastErrorMessage;
    QString m_loadedModelPath;
    quint64 m_currentLoadRequestId{0};
    mutable std::mutex m_stateMutex;

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
            this, [this](bool ok, const QString& msg, quint64 requestId, const QString& attemptedPath) {
        std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
        if (requestId != m_impl->m_currentLoadRequestId) {
            // Discard stale completion signal from superseded request
            return;
        }

        if (ok) {
            m_impl->m_loadedModelPath = attemptedPath;
            m_impl->m_state = SpeechToTextState::Idle;
            m_impl->m_lastError = SpeechToTextError::None;
            m_impl->m_lastErrorMessage.clear();
        } else {
            // If the engine already had a model loaded, it survived the failed replacement
            m_impl->m_state = (m_impl->m_engine && m_impl->m_engine->isModelLoaded())
                                  ? SpeechToTextState::Idle
                                  : SpeechToTextState::Error;
            m_impl->m_lastError = SpeechToTextError::ModelLoadFailed;
            m_impl->m_lastErrorMessage = msg;
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

    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
        m_impl->m_currentLoadRequestId = 0;
        if (m_impl->m_worker) {
            m_impl->m_worker->setShuttingDown(true);
            disconnect(m_impl->m_worker, nullptr, this, nullptr);
        }
    }

    if (m_impl && m_impl->m_workerThread && m_impl->m_workerThread->isRunning()) {
        m_impl->m_workerThread->quit();
        if (!m_impl->m_workerThread->wait(2000)) {
            m_impl->m_workerThread->terminate();
            m_impl->m_workerThread->wait(1000);
        }
    }
}

bool SpeechToTextService::loadModel(const QString& modelPath)
{
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    if (m_impl->m_state == SpeechToTextState::Transcribing ||
        m_impl->m_state == SpeechToTextState::LoadingModel) {
        m_impl->m_lastError = SpeechToTextError::Busy;
        m_impl->m_lastErrorMessage = "Speech-to-text service is currently busy.";
        emit transcriptionError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    auto resolution = WhisperModelResolver::resolveModelPath(modelPath);
    if (!resolution.isFound()) {
        m_impl->m_state = (m_impl->m_engine && m_impl->m_engine->isModelLoaded())
                              ? SpeechToTextState::Idle
                              : SpeechToTextState::Error;
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
    quint64 reqId = ++m_impl->m_currentLoadRequestId;
    if (reqId == 0) reqId = ++m_impl->m_currentLoadRequestId; // Prevent 0 on wraparound
    if (m_impl->m_worker) {
        m_impl->m_worker->setLatestRequestId(reqId);
    }

    m_impl->m_state = SpeechToTextState::LoadingModel;
    m_impl->m_lastError = SpeechToTextError::None;
    m_impl->m_lastErrorMessage.clear();

    emit stateChanged(m_impl->m_state);
    emit modelLoadStarted(targetPath);

    QMetaObject::invokeMethod(m_impl->m_worker, "doLoadModel", Qt::QueuedConnection,
                              Q_ARG(QString, targetPath),
                              Q_ARG(quint64, reqId));
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
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    return m_impl->m_state == SpeechToTextState::LoadingModel;
}

QString SpeechToTextService::loadedModelPath() const
{
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    return m_impl->m_loadedModelPath;
}

bool SpeechToTextService::transcribeAsync(const AudioBuffer& audioBuffer)
{
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
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
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
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
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
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
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    m_impl->m_state = SpeechToTextState::Error;
    m_impl->m_lastError = error;
    m_impl->m_lastErrorMessage = message;
    emit stateChanged(m_impl->m_state);
    emit transcriptionError(error, message);
}

bool SpeechToTextService::isBusy() const
{
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    return m_impl->m_state == SpeechToTextState::Transcribing ||
           m_impl->m_state == SpeechToTextState::LoadingModel;
}

SpeechToTextState SpeechToTextService::state() const
{
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    return m_impl->m_state;
}

SpeechToTextError SpeechToTextService::lastError() const
{
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    return m_impl->m_lastError;
}

QString SpeechToTextService::lastErrorMessage() const
{
    std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
    return m_impl->m_lastErrorMessage;
}

void SpeechToTextService::setLanguage(const QString& language)
{
    if (m_impl->m_engine) {
        m_impl->m_engine->setLanguage(language.toStdString());
    }
    if (m_impl->m_worker) {
        QMetaObject::invokeMethod(m_impl->m_worker, "setLanguage", Qt::QueuedConnection,
                                   Q_ARG(QString, language));
    }
}

QString SpeechToTextService::language() const
{
    if (m_impl->m_engine) {
        return QString::fromStdString(m_impl->m_engine->language());
    }
    return QStringLiteral("en");
}

void SpeechToTextService::setInitialPrompt(const QString& prompt)
{
    if (m_impl->m_engine) {
        m_impl->m_engine->setInitialPrompt(prompt.toStdString());
    }
    if (m_impl->m_worker) {
        QMetaObject::invokeMethod(m_impl->m_worker, "setInitialPrompt", Qt::QueuedConnection,
                                   Q_ARG(QString, prompt));
    }
}

QString SpeechToTextService::initialPrompt() const
{
    if (m_impl->m_engine) {
        return QString::fromStdString(m_impl->m_engine->initialPrompt());
    }
    return QString();
}

bool SpeechToTextService::isMultilingual() const
{
    if (m_impl->m_engine) {
        return m_impl->m_engine->isMultilingual();
    }
    return false;
}

void SpeechToTextService::setDefaultEngineFactory(EngineFactory factory)
{
    s_defaultEngineFactory = std::move(factory);
}

void SpeechToTextService::resetDefaultEngineFactory()
{
    s_defaultEngineFactory = nullptr;
}

void SpeechToTextService::setEngine(std::shared_ptr<ISpeechToTextEngine> engine)
{
    std::string currentLang = m_impl->m_engine ? m_impl->m_engine->language() : "en";
    std::string currentPrompt = m_impl->m_engine ? m_impl->m_engine->initialPrompt() : "";

    cancel();
    m_impl->m_engine = std::move(engine);
    if (m_impl->m_engine) {
        m_impl->m_engine->setLanguage(currentLang);
        m_impl->m_engine->setInitialPrompt(currentPrompt);
    }

    if (m_impl->m_worker) {
        m_impl->m_worker->setShuttingDown(true);
        disconnect(m_impl->m_worker, nullptr, this, nullptr);
    }

    if (m_impl->m_workerThread && m_impl->m_workerThread->isRunning()) {
        m_impl->m_workerThread->quit();
        if (!m_impl->m_workerThread->wait(2000)) {
            m_impl->m_workerThread->terminate();
            m_impl->m_workerThread->wait(1000);
        }
    }

    m_impl->m_workerThread = new QThread(this);
    m_impl->m_worker = new SpeechToTextWorker(m_impl->m_engine);
    m_impl->m_worker->setLatestRequestId(m_impl->m_currentLoadRequestId);
    m_impl->m_worker->moveToThread(m_impl->m_workerThread);

    connect(m_impl->m_workerThread, &QThread::finished, m_impl->m_worker, &QObject::deleteLater);

    connect(m_impl->m_worker, &SpeechToTextWorker::modelLoadResult,
            this, [this](bool ok, const QString& msg, quint64 requestId, const QString& attemptedPath) {
        std::lock_guard<std::mutex> lock(m_impl->m_stateMutex);
        if (requestId != m_impl->m_currentLoadRequestId) {
            // Discard stale completion signal from superseded request
            return;
        }

        if (ok) {
            m_impl->m_loadedModelPath = attemptedPath;
            m_impl->m_state = SpeechToTextState::Idle;
            m_impl->m_lastError = SpeechToTextError::None;
            m_impl->m_lastErrorMessage.clear();
        } else {
            // If the engine already had a model loaded, it survived the failed replacement
            m_impl->m_state = (m_impl->m_engine && m_impl->m_engine->isModelLoaded())
                                  ? SpeechToTextState::Idle
                                  : SpeechToTextState::Error;
            m_impl->m_lastError = SpeechToTextError::ModelLoadFailed;
            m_impl->m_lastErrorMessage = msg;
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
