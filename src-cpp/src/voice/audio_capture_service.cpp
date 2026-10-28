#include "flex/voice/audio_capture_service.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QBuffer>
#include <QMediaDevices>
#include <QPointer>
#include <QTimer>

#include <cmath>
#include <cstring>
#include <vector>

namespace flex::voice {

// -----------------------------------------------------------------------------
// Standalone Audio Resampling & Conversion Implementation
// -----------------------------------------------------------------------------

AudioResampleResult convertAndResampleTo16kMono(
    const void* data,
    size_t byteCount,
    int sourceSampleRate,
    int sourceChannels,
    AudioSampleFormat sourceFormat,
    int targetSampleRate)
{
    AudioResampleResult result;

    if (sourceSampleRate <= 0 || targetSampleRate <= 0) {
        result.errorMessage = "Invalid sample rate (must be greater than 0).";
        return result;
    }
    if (sourceChannels <= 0) {
        result.errorMessage = "Invalid channel count (must be greater than 0).";
        return result;
    }
    if (sourceFormat == AudioSampleFormat::Unknown) {
        result.errorMessage = "Unsupported or unknown audio sample format.";
        return result;
    }
    if (byteCount == 0 || !data) {
        result.success = true;
        result.buffer.sampleRate = targetSampleRate;
        result.buffer.channelCount = 1;
        result.buffer.sampleSizeBits = 16;
        result.buffer.durationMs = 0;
        return result;
    }

    size_t bytesPerSample = 0;
    switch (sourceFormat) {
        case AudioSampleFormat::Int16:   bytesPerSample = 2; break;
        case AudioSampleFormat::Float32: bytesPerSample = 4; break;
        case AudioSampleFormat::Int32:   bytesPerSample = 4; break;
        case AudioSampleFormat::UInt8:   bytesPerSample = 1; break;
        default:
            result.errorMessage = "Unsupported audio sample format.";
            return result;
    }

    size_t frameSize = bytesPerSample * static_cast<size_t>(sourceChannels);
    if (byteCount % frameSize != 0) {
        result.errorMessage = "Truncated or malformed audio buffer (length is not a multiple of frame size).";
        return result;
    }

    size_t numInputFrames = byteCount / frameSize;
    std::vector<float> monoInput;
    monoInput.reserve(numInputFrames);

    const auto* bytePtr = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < numInputFrames; ++i) {
        double sum = 0.0;
        const uint8_t* frameStart = bytePtr + (i * frameSize);

        for (int ch = 0; ch < sourceChannels; ++ch) {
            const uint8_t* samplePtr = frameStart + (static_cast<size_t>(ch) * bytesPerSample);
            double norm = 0.0;
            switch (sourceFormat) {
                case AudioSampleFormat::Int16: {
                    int16_t v = 0;
                    std::memcpy(&v, samplePtr, sizeof(int16_t));
                    norm = static_cast<double>(v) / 32768.0;
                    break;
                }
                case AudioSampleFormat::Float32: {
                    float v = 0.0f;
                    std::memcpy(&v, samplePtr, sizeof(float));
                    if (std::isnan(v)) {
                        v = 0.0f;
                    }
                    norm = static_cast<double>(v);
                    break;
                }
                case AudioSampleFormat::Int32: {
                    int32_t v = 0;
                    std::memcpy(&v, samplePtr, sizeof(int32_t));
                    norm = static_cast<double>(v) / 2147483648.0;
                    break;
                }
                case AudioSampleFormat::UInt8: {
                    uint8_t v = *samplePtr;
                    norm = (static_cast<double>(v) - 128.0) / 128.0;
                    break;
                }
                default:
                    break;
            }
            sum += norm;
        }

        double mono = sum / static_cast<double>(sourceChannels);
        if (std::isnan(mono)) {
            mono = 0.0;
        }
        if (mono > 1.0) mono = 1.0;
        if (mono < -1.0) mono = -1.0;
        monoInput.push_back(static_cast<float>(mono));
    }

    std::vector<float> resampled;
    if (sourceSampleRate == targetSampleRate) {
        resampled = std::move(monoInput);
    } else {
        double ratio = static_cast<double>(targetSampleRate) / static_cast<double>(sourceSampleRate);
        size_t numOut = static_cast<size_t>(std::round(static_cast<double>(numInputFrames) * ratio));
        resampled.reserve(numOut);

        double step = static_cast<double>(sourceSampleRate) / static_cast<double>(targetSampleRate);
        for (size_t outIdx = 0; outIdx < numOut; ++outIdx) {
            double inPos = static_cast<double>(outIdx) * step;
            size_t idx0 = static_cast<size_t>(inPos);
            double frac = inPos - static_cast<double>(idx0);

            float s0 = (idx0 < numInputFrames) ? monoInput[idx0] : monoInput.back();
            float s1 = (idx0 + 1 < numInputFrames) ? monoInput[idx0 + 1] : s0;

            double interp = (1.0 - frac) * static_cast<double>(s0) + frac * static_cast<double>(s1);
            if (std::isnan(interp)) interp = 0.0;
            if (interp > 1.0) interp = 1.0;
            if (interp < -1.0) interp = -1.0;
            resampled.push_back(static_cast<float>(interp));
        }
    }

    QByteArray pcmBytes;
    pcmBytes.resize(static_cast<qsizetype>(resampled.size() * sizeof(int16_t)));
    auto* pcmOut = reinterpret_cast<int16_t*>(pcmBytes.data());
    for (size_t i = 0; i < resampled.size(); ++i) {
        double scaled = static_cast<double>(resampled[i]) * 32767.0;
        if (scaled > 32767.0) scaled = 32767.0;
        if (scaled < -32768.0) scaled = -32768.0;
        pcmOut[i] = static_cast<int16_t>(std::round(scaled));
    }

    result.success = true;
    result.buffer.sampleRate = targetSampleRate;
    result.buffer.channelCount = 1;
    result.buffer.sampleSizeBits = 16;
    result.buffer.samples = std::move(resampled);
    result.buffer.rawPcmBytes = std::move(pcmBytes);
    result.buffer.durationMs = static_cast<int64_t>(
        (static_cast<double>(result.buffer.sampleCount()) / static_cast<double>(targetSampleRate)) * 1000.0);
    return result;
}

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
        }

        m_activeFormat = format;
        m_audioSource = std::make_unique<QAudioSource>(dev, format);

        // Monitor state transitions and unexpected hardware errors
        connect(m_audioSource.get(), &QAudioSource::stateChanged, this, [this](QAudio::State state) {
            if (state == QAudio::StoppedState && m_recording) {
                if (m_audioSource && m_audioSource->error() != QAudio::NoError) {
                    m_recording = false;
                    if (m_errorCallback) {
                        m_errorCallback(AudioCaptureError::DeviceUnavailable,
                                        "Audio hardware error or device disconnected.");
                    }
                }
            }
        });

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

        AudioSampleFormat fmt = AudioSampleFormat::Unknown;
        switch (m_activeFormat.sampleFormat()) {
            case QAudioFormat::Int16:   fmt = AudioSampleFormat::Int16; break;
            case QAudioFormat::Float:   fmt = AudioSampleFormat::Float32; break;
            case QAudioFormat::Int32:   fmt = AudioSampleFormat::Int32; break;
            case QAudioFormat::UInt8:   fmt = AudioSampleFormat::UInt8; break;
            default:                    fmt = AudioSampleFormat::Unknown; break;
        }

        auto res = convertAndResampleTo16kMono(
            m_pcmBuffer.constData(),
            static_cast<size_t>(m_pcmBuffer.size()),
            m_activeFormat.sampleRate(),
            m_activeFormat.channelCount(),
            fmt,
            16000);

        m_pcmBuffer.clear();

        if (!res.success) {
            if (m_errorCallback) {
                m_errorCallback(AudioCaptureError::FormatNotSupported,
                                QString::fromStdString(res.errorMessage));
            }
            return AudioBuffer{};
        }

        return res.buffer;
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
    QTimer* m_maxDurationTimer{nullptr};
    int m_maxDurationMs{30000}; // 30 seconds default duration cap
    bool m_wasMaxDurationReached{false};
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
    m_impl->m_maxDurationTimer = new QTimer(this);
    m_impl->m_maxDurationTimer->setSingleShot(true);
    connect(m_impl->m_maxDurationTimer, &QTimer::timeout, this, &AudioCaptureService::onMaxDurationTimeout);

    if (m_impl->m_source) {
        m_impl->m_source->setErrorCallback([this](AudioCaptureError err, const QString& msg) {
            if (m_impl->m_state == AudioCaptureState::Recording) {
                if (m_impl->m_maxDurationTimer) {
                    m_impl->m_maxDurationTimer->stop();
                }
                m_impl->m_state = AudioCaptureState::Error;
                m_impl->m_lastError = err;
                m_impl->m_lastErrorMessage = msg;
                emit stateChanged(m_impl->m_state);
                emit captureError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
            }
        });
    }
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
        // Already recording; repeated start call is a safe no-op returning true
        return true;
    }

    m_impl->m_lastError = AudioCaptureError::None;
    m_impl->m_lastErrorMessage.clear();
    m_impl->m_wasMaxDurationReached = false;

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

    if (m_impl->m_maxDurationMs > 0 && m_impl->m_maxDurationTimer) {
        m_impl->m_maxDurationTimer->start(m_impl->m_maxDurationMs);
    }

    emit stateChanged(m_impl->m_state);
    emit captureStarted();
    return true;
}

AudioBuffer AudioCaptureService::stopCapture()
{
    if (m_impl->m_state != AudioCaptureState::Recording) {
        return AudioBuffer{};
    }

    if (m_impl->m_maxDurationTimer) {
        m_impl->m_maxDurationTimer->stop();
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

    if (m_impl->m_maxDurationTimer) {
        m_impl->m_maxDurationTimer->stop();
    }

    if (m_impl->m_source) {
        m_impl->m_source->cancel();
    }

    m_impl->m_state = AudioCaptureState::Idle;
    emit stateChanged(m_impl->m_state);
    emit captureCancelled();
}

void AudioCaptureService::setMaxDurationMs(int ms)
{
    m_impl->m_maxDurationMs = ms;
}

int AudioCaptureService::maxDurationMs() const
{
    return m_impl->m_maxDurationMs;
}

bool AudioCaptureService::wasMaxDurationReached() const
{
    return m_impl->m_wasMaxDurationReached;
}

void AudioCaptureService::onMaxDurationTimeout()
{
    if (m_impl->m_state == AudioCaptureState::Recording) {
        m_impl->m_wasMaxDurationReached = true;
        emit captureMaxDurationReached();
        stopCapture();
    }
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

    if (m_impl->m_source) {
        m_impl->m_source->setErrorCallback([this](AudioCaptureError err, const QString& msg) {
            if (m_impl->m_state == AudioCaptureState::Recording) {
                if (m_impl->m_maxDurationTimer) {
                    m_impl->m_maxDurationTimer->stop();
                }
                m_impl->m_state = AudioCaptureState::Error;
                m_impl->m_lastError = err;
                m_impl->m_lastErrorMessage = msg;
                emit stateChanged(m_impl->m_state);
                emit captureError(m_impl->m_lastError, m_impl->m_lastErrorMessage);
            }
        });
    }
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
