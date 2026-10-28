#include "flex/voice/audio_capture_service.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QBuffer>
#include <QMediaDevices>
#include <QPointer>

#include <cmath>
#include <cstring>
#include <vector>

namespace flex::voice {

namespace {

/**
 * Default Qt 6 Multimedia hardware audio capture source.
 */
class QtAudioCaptureSource : public QObject, public IAudioCaptureSource {
public:
    explicit QtAudioCaptureSource(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~QtAudioCaptureSource() override
    {
        stopInternal();
    }

    [[nodiscard]] std::vector<AudioDeviceInfo> availableDevices() const override
    {
        std::vector<AudioDeviceInfo> list;
        const auto devices = QMediaDevices::audioInputs();
        const auto defaultDev = QMediaDevices::defaultAudioInput();

        for (const auto& dev : devices) {
            AudioDeviceInfo info;
            info.id = dev.id().toStdString();
            info.name = dev.description().toStdString();
            info.isDefault = (dev.id() == defaultDev.id());
            list.push_back(info);
        }
        return list;
    }

    [[nodiscard]] AudioDeviceInfo defaultDevice() const override
    {
        const auto dev = QMediaDevices::defaultAudioInput();
        AudioDeviceInfo info;
        info.id = dev.id().toStdString();
        info.name = dev.description().toStdString();
        info.isDefault = true;
        return info;
    }

    bool start() override
    {
        stopInternal();

        const auto dev = QMediaDevices::defaultAudioInput();
        if (dev.isNull()) {
            return false;
        }

        // Target: 16 kHz, Mono, 16-bit signed PCM
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        if (!dev.isFormatSupported(format)) {
            // Pick preferred format if 16kHz mono is not natively accepted
            format = dev.preferredFormat();
            format.setChannelCount(1);
        }

        m_activeFormat = format;
        m_audioSource = std::make_unique<QAudioSource>(dev, format);

        m_pcmBuffer.clear();
        m_ioBuffer.setBuffer(&m_pcmBuffer);
        if (!m_ioBuffer.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }

        m_audioSource->start(&m_ioBuffer);
        m_recording = (m_audioSource->error() == QAudio::NoError);
        return m_recording;
    }

    AudioBuffer stop() override
    {
        if (!m_recording && m_pcmBuffer.isEmpty()) {
            return AudioBuffer{};
        }

        stopInternal();

        AudioBuffer result;
        result.sampleRate = m_activeFormat.sampleRate() > 0 ? m_activeFormat.sampleRate() : 16000;
        result.channelCount = m_activeFormat.channelCount() > 0 ? m_activeFormat.channelCount() : 1;
        result.sampleSizeBits = 16;
        result.rawPcmBytes = m_pcmBuffer;

        // Convert raw 16-bit PCM bytes to normalized float samples [-1.0f, 1.0f]
        const int bytesPerSample = sizeof(int16_t);
        const int totalSamples = m_pcmBuffer.size() / bytesPerSample;
        result.samples.reserve(totalSamples);

        const auto* rawPtr = reinterpret_cast<const int16_t*>(m_pcmBuffer.constData());
        for (int i = 0; i < totalSamples; ++i) {
            float normalized = static_cast<float>(rawPtr[i]) / 32768.0f;
            if (normalized < -1.0f) normalized = -1.0f;
            if (normalized > 1.0f) normalized = 1.0f;
            result.samples.push_back(normalized);
        }

        if (result.sampleRate > 0) {
            result.durationMs = static_cast<int64_t>(
                (static_cast<double>(totalSamples) / static_cast<double>(result.sampleRate)) * 1000.0
            );
        }

        m_pcmBuffer.clear();
        return result;
    }

    void cancel() override
    {
        stopInternal();
        m_pcmBuffer.clear();
    }

    [[nodiscard]] bool isRecording() const override
    {
        return m_recording;
    }

private:
    void stopInternal()
    {
        if (m_audioSource) {
            m_audioSource->stop();
            m_audioSource.reset();
        }
        if (m_ioBuffer.isOpen()) {
            m_ioBuffer.close();
        }
        m_recording = false;
    }

    bool m_recording{false};
    QAudioFormat m_activeFormat;
    std::unique_ptr<QAudioSource> m_audioSource;
    QByteArray m_pcmBuffer;
    QBuffer m_ioBuffer;
};

} // anonymous namespace

// -----------------------------------------------------------------------------
// AudioCaptureServiceImpl
// -----------------------------------------------------------------------------

class AudioCaptureServiceImpl {
public:
    explicit AudioCaptureServiceImpl(std::shared_ptr<IAudioCaptureSource> source)
        : m_source(std::move(source))
    {
        if (!m_source) {
            m_source = std::make_shared<QtAudioCaptureSource>();
        }
    }

    std::shared_ptr<IAudioCaptureSource> m_source;
    AudioCaptureState m_state{AudioCaptureState::Idle};
    AudioCaptureError m_lastError{AudioCaptureError::None};
    QString m_lastErrorMessage;
};

// -----------------------------------------------------------------------------
// AudioCaptureService
// -----------------------------------------------------------------------------

AudioCaptureService::AudioCaptureService(
    std::shared_ptr<IAudioCaptureSource> customSource,
    QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<AudioCaptureServiceImpl>(std::move(customSource)))
{
}

AudioCaptureService::~AudioCaptureService()
{
    cancelCapture();
}

std::vector<AudioDeviceInfo> AudioCaptureService::availableInputDevices() const
{
    if (m_impl->m_source) {
        return m_impl->m_source->availableDevices();
    }
    return {};
}

AudioDeviceInfo AudioCaptureService::defaultInputDevice() const
{
    if (m_impl->m_source) {
        return m_impl->m_source->defaultDevice();
    }
    return {};
}

bool AudioCaptureService::startCapture()
{
    if (m_impl->m_state == AudioCaptureState::Recording) {
        // Already recording; repeated start call is a no-op returning true
        return true;
    }

    m_impl->m_lastError = AudioCaptureError::None;
    m_impl->m_lastErrorMessage.clear();

    if (!m_impl->m_source) {
        m_impl->m_state = AudioCaptureState::Error;
        m_impl->m_lastError = AudioCaptureError::DeviceNotFound;
        m_impl->m_lastErrorMessage = "Audio capture source is not initialized.";
        emit stateChanged(m_impl->m_state);
        emit captureError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    bool started = m_impl->m_source->start();
    if (!started) {
        m_impl->m_state = AudioCaptureState::Error;
        m_impl->m_lastError = AudioCaptureError::CaptureFailed;
        m_impl->m_lastErrorMessage = "Failed to start audio recording on the selected device.";
        emit stateChanged(m_impl->m_state);
        emit captureError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
        return false;
    }

    m_impl->m_state = AudioCaptureState::Recording;
    emit stateChanged(m_impl->m_state);
    emit captureStarted();
    return true;
}

AudioBuffer AudioCaptureService::stopCapture()
{
    if (m_impl->m_state != AudioCaptureState::Recording) {
        return AudioBuffer{};
    }

    m_impl->m_state = AudioCaptureState::Stopping;
    emit stateChanged(m_impl->m_state);

    AudioBuffer buf;
    if (m_impl->m_source) {
        buf = m_impl->m_source->stop();
    }

    m_impl->m_state = AudioCaptureState::Idle;
    emit stateChanged(m_impl->m_state);
    emit captureFinished(buf);
    return buf;
}

void AudioCaptureService::cancelCapture()
{
    if (m_impl->m_state != AudioCaptureState::Recording) {
        return;
    }

    if (m_impl->m_source) {
        m_impl->m_source->cancel();
    }

    m_impl->m_state = AudioCaptureState::Idle;
    emit stateChanged(m_impl->m_state);
    emit captureCancelled();
}

bool AudioCaptureService::isRecording() const
{
    return m_impl->m_state == AudioCaptureState::Recording;
}

AudioCaptureState AudioCaptureService::state() const
{
    return m_impl->m_state;
}

AudioCaptureError AudioCaptureService::lastError() const
{
    return m_impl->m_lastError;
}

QString AudioCaptureService::lastErrorMessage() const
{
    return m_impl->m_lastErrorMessage;
}

void AudioCaptureService::setCaptureSource(std::shared_ptr<IAudioCaptureSource> source)
{
    cancelCapture();
    m_impl->m_source = std::move(source);
}

const char* audioCaptureStateToString(AudioCaptureState state) noexcept
{
    switch (state) {
        case AudioCaptureState::Idle:      return "Idle";
        case AudioCaptureState::Recording: return "Recording";
        case AudioCaptureState::Stopping:  return "Stopping";
        case AudioCaptureState::Error:     return "Error";
    }
    return "Unknown";
}

const char* audioCaptureErrorToString(AudioCaptureError error) noexcept
{
    switch (error) {
        case AudioCaptureError::None:               return "None";
        case AudioCaptureError::DeviceNotFound:     return "DeviceNotFound";
        case AudioCaptureError::PermissionDenied:   return "PermissionDenied";
        case AudioCaptureError::DeviceUnavailable:  return "DeviceUnavailable";
        case AudioCaptureError::FormatNotSupported: return "FormatNotSupported";
        case AudioCaptureError::CaptureFailed:      return "CaptureFailed";
    }
    return "Unknown";
}

} // namespace flex::voice

