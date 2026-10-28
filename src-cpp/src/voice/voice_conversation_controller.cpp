#include "flex/voice/voice_conversation_controller.hpp"
#include "flex/voice/whisper_model_resolver.hpp"

#include <QDir>
#include <QFileInfo>

#include <iostream>

namespace flex::voice {

const char* voiceSessionStateToString(VoiceSessionState state) noexcept
{
    switch (state) {
        case VoiceSessionState::Idle:         return "Idle";
        case VoiceSessionState::Recording:    return "Recording";
        case VoiceSessionState::Transcribing: return "Transcribing";
        case VoiceSessionState::Reviewing:    return "Reviewing";
        case VoiceSessionState::Thinking:     return "Thinking";
        case VoiceSessionState::Speaking:     return "Speaking";
        case VoiceSessionState::Error:        return "Error";
    }
    return "Unknown";
}

VoiceConversationController::VoiceConversationController(
    std::shared_ptr<BackendCoordinator> coordinator,
    std::shared_ptr<AudioCaptureService> audioCapture,
    std::shared_ptr<SpeechToTextService> speechToText,
    std::shared_ptr<TextToSpeechService> textToSpeech,
    QObject* parent)
    : QObject(parent)
    , m_coordinator(std::move(coordinator))
    , m_audioCapture(std::move(audioCapture))
    , m_stt(std::move(speechToText))
    , m_tts(std::move(textToSpeech))
{
    if (!m_audioCapture) {
        m_audioCapture = std::make_shared<AudioCaptureService>(nullptr, this);
    }
    if (!m_stt) {
        m_stt = std::make_shared<SpeechToTextService>(nullptr, this);
    }
    if (!m_tts) {
        m_tts = std::make_shared<TextToSpeechService>(nullptr, this);
    }

    // Connect AudioCapture signals
    connect(m_audioCapture.get(), &AudioCaptureService::captureStarted,
            this, &VoiceConversationController::onCaptureStarted);
    connect(m_audioCapture.get(), &AudioCaptureService::captureFinished,
            this, &VoiceConversationController::onCaptureFinished);
    connect(m_audioCapture.get(), &AudioCaptureService::captureCancelled,
            this, &VoiceConversationController::onCaptureCancelled);
    connect(m_audioCapture.get(), &AudioCaptureService::captureError,
            this, &VoiceConversationController::onCaptureError);

    // Connect SpeechToText signals
    connect(m_stt.get(), &SpeechToTextService::modelLoadFinished,
            this, &VoiceConversationController::onModelLoadFinished);
    connect(m_stt.get(), &SpeechToTextService::transcriptionFinished,
            this, &VoiceConversationController::onSttFinished);
    connect(m_stt.get(), &SpeechToTextService::transcriptionCancelled,
            this, &VoiceConversationController::onSttCancelled);
    connect(m_stt.get(), &SpeechToTextService::transcriptionError,
            this, &VoiceConversationController::onSttError);

    // Connect TextToSpeech signals
    connect(m_tts.get(), &TextToSpeechService::speechStarted,
            this, &VoiceConversationController::onTtsStarted);
    connect(m_tts.get(), &TextToSpeechService::speechFinished,
            this, &VoiceConversationController::onTtsFinished);
    connect(m_tts.get(), &TextToSpeechService::speechCancelled,
            this, &VoiceConversationController::onTtsCancelled);
    connect(m_tts.get(), &TextToSpeechService::speechError,
            this, &VoiceConversationController::onTtsError);

    // Connect BackendCoordinator signals if coordinator provided
    if (m_coordinator) {
        connect(m_coordinator.get(), &BackendCoordinator::chatFinished,
                this, &VoiceConversationController::onChatFinished);
        connect(m_coordinator.get(), &BackendCoordinator::chatError,
                this, &VoiceConversationController::onChatError);
    }

    // Attempt to preload local model if file is already present on disk (no network access)
    if (m_stt && !m_stt->isModelLoaded() && !m_stt->isModelLoading()) {
        auto resolution = WhisperModelResolver::resolveModelPath();
        if (resolution.isFound()) {
            m_stt->loadModel(resolution.resolvedPath);
        }
    }
}

VoiceConversationController::~VoiceConversationController()
{
    cancelAll();
}

bool VoiceConversationController::isVoiceOutputEnabled() const noexcept
{
    return m_voiceOutputEnabled;
}

void VoiceConversationController::setVoiceOutputEnabled(bool enabled)
{
    if (m_voiceOutputEnabled != enabled) {
        m_voiceOutputEnabled = enabled;
        if (!m_voiceOutputEnabled && m_tts && m_tts->isSpeaking()) {
            m_tts->stop();
        }
        emit voiceOutputToggled(m_voiceOutputEnabled);
    }
}

bool VoiceConversationController::isAutoSubmitEnabled() const noexcept
{
    return m_autoSubmit;
}

void VoiceConversationController::setAutoSubmitEnabled(bool enabled)
{
    m_autoSubmit = enabled;
}

VoiceSessionState VoiceConversationController::state() const noexcept
{
    return m_state;
}

bool VoiceConversationController::isBusy() const noexcept
{
    return m_state == VoiceSessionState::Recording ||
           m_state == VoiceSessionState::Transcribing ||
           m_state == VoiceSessionState::Thinking ||
           m_state == VoiceSessionState::Speaking;
}

QString VoiceConversationController::lastTranscript() const
{
    return m_lastTranscript;
}

QString VoiceConversationController::lastErrorMessage() const
{
    return m_lastErrorMessage;
}

QString VoiceConversationController::lastAssistantResponse() const
{
    return m_lastAssistantResponse;
}

std::shared_ptr<AudioCaptureService> VoiceConversationController::audioCaptureService() const noexcept
{
    return m_audioCapture;
}

std::shared_ptr<SpeechToTextService> VoiceConversationController::speechToTextService() const noexcept
{
    return m_stt;
}

std::shared_ptr<TextToSpeechService> VoiceConversationController::textToSpeechService() const noexcept
{
    return m_tts;
}

std::shared_ptr<BackendCoordinator> VoiceConversationController::coordinator() const noexcept
{
    return m_coordinator;
}

bool VoiceConversationController::startRecording()
{
    if (m_state == VoiceSessionState::Recording ||
        m_state == VoiceSessionState::Transcribing ||
        m_state == VoiceSessionState::Thinking) {
        m_lastErrorMessage = "Voice subsystem is currently busy.";
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    if (!m_coordinator) {
        m_lastErrorMessage = "Backend coordinator is not available.";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    // Check if C core is already processing an active operation
    if (m_coordinator->coreSession().isProcessing()) {
        m_lastErrorMessage = "System is currently processing another operation. Please wait.";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    // Stop TTS if speaking before starting capture
    if (m_tts && m_tts->isSpeaking()) {
        m_tts->stop();
    }

    // 1. Authorize with C Core lifecycle: dispatch "voice" command
    auto dispatchRes = m_coordinator->coreSession().dispatch("voice");
    if (!dispatchRes.isOk() || !dispatchRes.operationStarted()) {
        m_lastErrorMessage = "C core rejected voice operation: " +
            QString::fromStdString(CoreSession::stateStatusToString(dispatchRes.stateStatus()));
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }
    m_voiceCoreOpActive = true;

    // 2. Start audio capture
    if (!m_audioCapture) {
        finishVoiceCoreOperation();
        m_lastErrorMessage = "Audio capture service is not available.";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    bool started = m_audioCapture->startCapture();
    if (!started) {
        finishVoiceCoreOperation();
        m_lastErrorMessage = "Failed to start microphone audio capture.";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    setState(VoiceSessionState::Recording);
    emit recordingStarted();
    return true;
}

void VoiceConversationController::stopRecording()
{
    if (m_state != VoiceSessionState::Recording) {
        return;
    }
    if (m_audioCapture && m_audioCapture->isRecording()) {
        m_audioCapture->stopCapture();
    }
}

void VoiceConversationController::cancelVoiceInput()
{
    m_waitingForModelLoad = false;
    m_pendingAudioBuffer.clear();
    if (m_audioCapture && m_audioCapture->isRecording()) {
        m_audioCapture->cancelCapture();
    }
    if (m_stt && m_stt->isBusy()) {
        m_stt->cancel();
    }
    finishVoiceCoreOperation();
    setState(VoiceSessionState::Idle);
}

bool VoiceConversationController::submitTranscript(const QString& transcriptText)
{
    QString trimmed = transcriptText.trimmed();
    if (trimmed.isEmpty()) {
        trimmed = m_lastTranscript.trimmed();
    }
    if (trimmed.isEmpty()) {
        m_lastErrorMessage = "Cannot submit empty transcript.";
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    if (m_state == VoiceSessionState::Thinking) {
        m_lastErrorMessage = "Assistant is already generating a response.";
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    if (!m_coordinator) {
        m_lastErrorMessage = "Backend coordinator is unavailable.";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    if (m_coordinator->coreSession().isProcessing()) {
        m_lastErrorMessage = "System is currently busy with another operation.";
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    // Stop TTS if speaking
    if (m_tts && m_tts->isSpeaking()) {
        m_tts->stop();
    }

    m_lastTranscript = trimmed;
    setState(VoiceSessionState::Thinking);
    emit chatTurnStarted(trimmed);

    ChatRequest req;
    req.message = trimmed.toStdString();
    req.streaming = false;

    m_currentChatRequestId = m_coordinator->sendChatAsync(req);
    if (m_currentChatRequestId == 0) {
        setState(VoiceSessionState::Error);
        m_lastErrorMessage = "Failed to dispatch chat request to backend coordinator.";
        emit errorOccurred(m_lastErrorMessage);
        return false;
    }

    return true;
}

void VoiceConversationController::stopSpeaking()
{
    if (m_tts && m_tts->isSpeaking()) {
        m_tts->stop();
    }
    if (m_state == VoiceSessionState::Speaking) {
        setState(VoiceSessionState::Idle);
    }
}

void VoiceConversationController::cancelAll()
{
    m_waitingForModelLoad = false;
    m_pendingAudioBuffer.clear();
    if (m_audioCapture && m_audioCapture->isRecording()) {
        m_audioCapture->cancelCapture();
    }
    if (m_stt && m_stt->isBusy()) {
        m_stt->cancel();
    }
    if (m_coordinator && m_currentChatRequestId != 0) {
        m_coordinator->cancelRequest(m_currentChatRequestId);
        m_currentChatRequestId = 0;
    }
    if (m_tts && m_tts->isSpeaking()) {
        m_tts->stop();
    }
    finishVoiceCoreOperation();
    setState(VoiceSessionState::Idle);
}

void VoiceConversationController::onCaptureStarted()
{
    if (m_state != VoiceSessionState::Recording) {
        setState(VoiceSessionState::Recording);
    }
}

void VoiceConversationController::onCaptureFinished(const AudioBuffer& buffer)
{
    if (m_state != VoiceSessionState::Recording) {
        return;
    }

    emit recordingFinished(buffer);

    // 1. Validate audio samples
    if (buffer.isEmpty() || buffer.sampleCount() == 0) {
        finishVoiceCoreOperation();
        m_lastErrorMessage = "No audio recorded (capture buffer was empty).";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return;
    }

    // 2. Validate STT service
    if (!m_stt) {
        finishVoiceCoreOperation();
        m_lastErrorMessage = "Speech-to-text service is not available.";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return;
    }

    // 3. Ensure Whisper model is loaded
    if (!m_stt->isModelLoaded()) {
        if (m_stt->isModelLoading()) {
            m_pendingAudioBuffer = buffer;
            m_waitingForModelLoad = true;
            setState(VoiceSessionState::Transcribing);
            emit transcriptionStarted();
            return;
        }

        auto resolution = WhisperModelResolver::resolveModelPath();
        if (!resolution.isFound()) {
            finishVoiceCoreOperation();
            m_lastErrorMessage = resolution.errorMessage;
            setState(VoiceSessionState::Error);
            emit errorOccurred(m_lastErrorMessage);
            return;
        }

        m_pendingAudioBuffer = buffer;
        m_waitingForModelLoad = true;
        setState(VoiceSessionState::Transcribing);
        emit transcriptionStarted();
        m_stt->loadModel(resolution.resolvedPath);
        return;
    }

    // 4. Model is already loaded: dispatch transcription immediately
    setState(VoiceSessionState::Transcribing);
    emit transcriptionStarted();

    bool ok = m_stt->transcribeAsync(buffer);
    if (!ok) {
        finishVoiceCoreOperation();
        m_lastErrorMessage = m_stt->lastErrorMessage();
        if (m_lastErrorMessage.isEmpty()) {
            m_lastErrorMessage = "Failed to dispatch speech recognition.";
        }
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
    }
}

void VoiceConversationController::onModelLoadFinished(bool success, const QString& message)
{
    if (!m_waitingForModelLoad) {
        return;
    }
    m_waitingForModelLoad = false;

    if (!success) {
        finishVoiceCoreOperation();
        m_pendingAudioBuffer.clear();
        m_lastErrorMessage = QString("Whisper speech model failed to initialize: %1").arg(message);
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return;
    }

    if (m_pendingAudioBuffer.isEmpty()) {
        finishVoiceCoreOperation();
        setState(VoiceSessionState::Idle);
        return;
    }

    AudioBuffer buf = std::move(m_pendingAudioBuffer);
    m_pendingAudioBuffer.clear();

    bool ok = m_stt->transcribeAsync(buf);
    if (!ok) {
        finishVoiceCoreOperation();
        m_lastErrorMessage = m_stt->lastErrorMessage();
        if (m_lastErrorMessage.isEmpty()) {
            m_lastErrorMessage = "Failed to dispatch speech recognition after model load.";
        }
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
    }
}

void VoiceConversationController::onCaptureCancelled()
{
    finishVoiceCoreOperation();
    setState(VoiceSessionState::Idle);
}

void VoiceConversationController::onCaptureError(AudioCaptureError /*error*/, const QString& message)
{
    finishVoiceCoreOperation();
    m_lastErrorMessage = message;
    setState(VoiceSessionState::Error);
    emit errorOccurred(message);
}

void VoiceConversationController::onSttFinished(const QString& text, int64_t /*durationMs*/)
{
    if (m_state != VoiceSessionState::Transcribing) {
        return;
    }

    // Complete the C core voice operation
    finishVoiceCoreOperation();

    QString trimmed = text.trimmed();
    trimmed.remove("[BLANK_AUDIO]", Qt::CaseInsensitive);
    trimmed.remove("[_BEG_]", Qt::CaseInsensitive);
    trimmed.remove("[MUSIC]", Qt::CaseInsensitive);
    trimmed = trimmed.trimmed();
    m_lastTranscript = trimmed;

    if (trimmed.isEmpty()) {
        m_lastErrorMessage = "No speech recognized (transcription was empty).";
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
        return;
    }

    emit transcriptReadyForReview(trimmed);

    if (m_autoSubmit) {
        submitTranscript(trimmed);
    } else {
        setState(VoiceSessionState::Reviewing);
    }
}

void VoiceConversationController::onSttCancelled()
{
    finishVoiceCoreOperation();
    setState(VoiceSessionState::Idle);
}

void VoiceConversationController::onSttError(SpeechToTextError /*error*/, const QString& message)
{
    finishVoiceCoreOperation();
    m_lastErrorMessage = message;
    setState(VoiceSessionState::Error);
    emit errorOccurred(message);
}

void VoiceConversationController::onChatFinished(uint64_t requestId, const ChatResult& result)
{
    // If we tracked this request or if we are in Thinking state
    if (m_currentChatRequestId != 0 && requestId != m_currentChatRequestId) {
        return;
    }
    m_currentChatRequestId = 0;

    if (result.success) {
        m_lastAssistantResponse = QString::fromStdString(result.responseText);
        emit chatTurnFinished(m_lastAssistantResponse);

        if (m_voiceOutputEnabled && m_tts) {
            setState(VoiceSessionState::Speaking);
            m_tts->speak(m_lastAssistantResponse, true);
        } else {
            setState(VoiceSessionState::Idle);
        }
    } else {
        m_lastErrorMessage = QString::fromStdString(result.error.message);
        if (m_lastErrorMessage.isEmpty()) {
            m_lastErrorMessage = "Inference failed.";
        }
        setState(VoiceSessionState::Error);
        emit errorOccurred(m_lastErrorMessage);
    }
}

void VoiceConversationController::onChatError(uint64_t requestId, const BackendError& error)
{
    if (m_currentChatRequestId != 0 && requestId != m_currentChatRequestId) {
        return;
    }
    m_currentChatRequestId = 0;
    m_lastErrorMessage = QString::fromStdString(error.message);
    setState(VoiceSessionState::Error);
    emit errorOccurred(m_lastErrorMessage);
}

void VoiceConversationController::onTtsStarted()
{
    if (m_state != VoiceSessionState::Speaking) {
        setState(VoiceSessionState::Speaking);
    }
    emit speechStarted();
}

void VoiceConversationController::onTtsFinished()
{
    if (m_state == VoiceSessionState::Speaking) {
        setState(VoiceSessionState::Idle);
    }
    emit speechFinished();
}

void VoiceConversationController::onTtsCancelled()
{
    if (m_state == VoiceSessionState::Speaking) {
        setState(VoiceSessionState::Idle);
    }
}

void VoiceConversationController::onTtsError(TtsError /*error*/, const QString& message)
{
    // TTS failure does not destroy or break chat completion
    if (m_state == VoiceSessionState::Speaking) {
        setState(VoiceSessionState::Idle);
    }
    emit errorOccurred(QString("TTS output error: %1").arg(message));
}

void VoiceConversationController::finishVoiceCoreOperation()
{
    if (m_voiceCoreOpActive && m_coordinator) {
        m_coordinator->coreSession().finishOperation();
        m_voiceCoreOpActive = false;
    }
}

void VoiceConversationController::setState(VoiceSessionState newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(m_state);
    }
}

} // namespace flex::voice

