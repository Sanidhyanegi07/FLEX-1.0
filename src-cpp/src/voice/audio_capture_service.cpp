#include "flex/voice/audio_capture_service.hpp"
#include "audio_dsp.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QBuffer>
#include <QMediaDevices>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
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
    std::vector<float> interleavedFloats;
    interleavedFloats.reserve(numInputFrames * static_cast<size_t>(sourceChannels));

    const auto* bytePtr = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < numInputFrames; ++i) {
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
            if (std::isnan(norm) || std::isinf(norm)) {
                norm = 0.0;
            }
            if (norm > 1.0) norm = 1.0;
            if (norm < -1.0) norm = -1.0;
            interleavedFloats.push_back(static_cast<float>(norm));
        }
    }

    // Downmix multi-channel audio to mono using C core safe downmixing policy
    std::vector<float> monoInput(numInputFrames);
    if (sourceChannels == 1) {
        std::memcpy(monoInput.data(), interleavedFloats.data(), numInputFrames * sizeof(float));
    } else if (sourceChannels == 2) {
        flex_stereo_analysis_t analysis;
        flex_audio_analyze_stereo(interleavedFloats.data(), numInputFrames, &analysis);
        flex_audio_downmix_to_mono_safe(interleavedFloats.data(), monoInput.data(), numInputFrames, 2, &analysis);
        std::cout << "[AudioConversion] Stereo downmix: mode=" << analysis.downmix_mode
                  << ", corr=" << std::fixed << std::setprecision(2) << analysis.correlation
                  << ", L(rms=" << analysis.ch0_rms << ", pk=" << analysis.ch0_peak << ")"
                  << ", R(rms=" << analysis.ch1_rms << ", pk=" << analysis.ch1_peak << ")\n";
    } else {
        flex_audio_downmix_to_mono_safe(interleavedFloats.data(), monoInput.data(), numInputFrames, static_cast<uint32_t>(sourceChannels), nullptr);
    }

    // Resample to target sample rate using bandlimited windowed-sinc anti-aliasing filter
    std::vector<float> resampled;
    if (sourceSampleRate == targetSampleRate) {
        resampled = std::move(monoInput);
    } else {
        double ratio = static_cast<double>(targetSampleRate) / static_cast<double>(sourceSampleRate);
        size_t numOut = static_cast<size_t>(std::ceil(static_cast<double>(numInputFrames) * ratio));
        resampled.resize(numOut + 32);
        size_t actualOut = flex_audio_resample_bandlimited(
            monoInput.data(), monoInput.size(),
            static_cast<uint32_t>(sourceSampleRate),
            resampled.data(), resampled.size(),
            static_cast<uint32_t>(targetSampleRate)
        );
        resampled.resize(actualOut);
    }

    // Measure and apply conservative speech gain normalization (with noise gate)
    auto normRes = flex_audio_normalize_speech(resampled.data(), resampled.size(), 0.75f, 15.0f, 0.003f);

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

    std::cout << "[AudioConversion] Resampled: " << result.buffer.samples.size() << " samples ("
              << result.buffer.durationMs << " ms) at 16000 Hz Mono (bandlimited sinc)\n";
    std::cout << "[AudioConversion] Signal levels: Peak before=" << normRes.peak_before
              << ", RMS before=" << normRes.rms_before
              << ", Peak after=" << normRes.peak_after
              << ", RMS after=" << normRes.rms_after
              << ", Gain applied=" << normRes.gain_applied
              << ", Normalized=" << (normRes.was_normalized ? "YES" : "NO")
              << ", Silence=" << (normRes.is_silence ? "YES" : "NO") << "\n";

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

    static QAudioDevice selectBestInputDevice(const std::string& preferredId = {}, bool* outFallback = nullptr)
    {
        if (outFallback) {
            *outFallback = false;
        }

        const auto devices = QMediaDevices::audioInputs();
        if (devices.empty()) {
            return QAudioDevice{};
        }

        // 1. Check environment variable override
        QString envDev = QString::fromUtf8(qgetenv("FLEX_AUDIO_INPUT_DEVICE")).trimmed();
        if (!envDev.isEmpty()) {
            for (const auto& d : devices) {
                if (d.description().contains(envDev, Qt::CaseInsensitive) ||
                    QString::fromStdString(d.id().toStdString()).contains(envDev, Qt::CaseInsensitive)) {
                    std::cout << "[AudioCapture] Using FLEX_AUDIO_INPUT_DEVICE override: \""
                              << d.description().toStdString() << "\"\n";
                    return d;
                }
            }
        }

        // 2. Check explicit preferred ID or name
        if (!preferredId.empty() && preferredId != "Default") {
            for (const auto& d : devices) {
                if (d.id().toStdString() == preferredId ||
                    d.description().toStdString() == preferredId) {
                    return d;
                }
            }
            if (outFallback) {
                *outFallback = true;
            }
            std::cout << "[AudioCapture] Preferred input device \"" << preferredId
                      << "\" not found or unavailable. Falling back to default audio input.\n";
        }

        // 3. Inspect default device
        auto defDev = QMediaDevices::defaultAudioInput();
        QString defDesc = defDev.description().trimmed();

        bool isLoopback = defDesc.contains("Stereo Mix", Qt::CaseInsensitive) ||
                          defDesc.contains("What U Hear", Qt::CaseInsensitive) ||
                          defDesc.contains("Wave Out", Qt::CaseInsensitive) ||
                          defDesc.contains("Loopback", Qt::CaseInsensitive);

        if (!isLoopback && !defDev.isNull()) {
            return defDev;
        }

        // 4. Default is a loopback; prioritize a genuine physical microphone
        for (const auto& d : devices) {
            QString desc = d.description();
            bool loop = desc.contains("Stereo Mix", Qt::CaseInsensitive) ||
                        desc.contains("What U Hear", Qt::CaseInsensitive) ||
                        desc.contains("Wave Out", Qt::CaseInsensitive) ||
                        desc.contains("Loopback", Qt::CaseInsensitive);
            if (!loop && (desc.contains("Microphone", Qt::CaseInsensitive) ||
                          desc.contains("Mic", Qt::CaseInsensitive) ||
                          desc.contains("Headset", Qt::CaseInsensitive) ||
                          desc.contains("Array", Qt::CaseInsensitive))) {
                std::cout << "[AudioCapture] Default input is loopback (\"" << defDesc.toStdString()
                          << "\"). Auto-selected physical microphone: \"" << desc.toStdString() << "\"\n";
                return d;
            }
        }

        // 5. Fallback: first non-loopback device
        for (const auto& d : devices) {
            QString desc = d.description();
            if (!desc.contains("Stereo Mix", Qt::CaseInsensitive) &&
                !desc.contains("What U Hear", Qt::CaseInsensitive)) {
                return d;
            }
        }

        return defDev;
    }

    [[nodiscard]] std::vector<AudioDeviceInfo> availableDevices() const override
    {
        std::vector<AudioDeviceInfo> list;
        const auto devices = QMediaDevices::audioInputs();
        const auto defaultDev = selectBestInputDevice(m_preferredDeviceId);

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
        const auto dev = selectBestInputDevice(m_preferredDeviceId);
        AudioDeviceInfo info;
        info.id = dev.id().toStdString();
        info.name = dev.description().toStdString();
        info.isDefault = true;
        return info;
    }

    void setPreferredDeviceId(const std::string& id) override
    {
        m_preferredDeviceId = id;
    }

    [[nodiscard]] std::string preferredDeviceId() const override
    {
        return m_preferredDeviceId;
    }

    [[nodiscard]] std::string activeDeviceName() const override
    {
        return m_activeDeviceName;
    }

    [[nodiscard]] bool isFallbackActive() const override
    {
        return m_fallbackActive;
    }

    bool start() override
    {
        stopInternal();

        const auto dev = selectBestInputDevice(m_preferredDeviceId, &m_fallbackActive);
        if (dev.isNull()) {
            std::cout << "[AudioCapture] Error: No valid audio input device available.\n";
            return false;
        }

        m_activeDeviceName = dev.description().toStdString();
        std::cout << "[AudioCapture] Input device: \"" << m_activeDeviceName
                  << "\" (ID: " << dev.id().toStdString() << ")\n";

        // Target: 16 kHz, Mono, 16-bit signed PCM
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        if (!dev.isFormatSupported(format)) {
            // Pick preferred format if 16kHz mono is not natively accepted
            format = dev.preferredFormat();
            std::cout << "[AudioCapture] Device does not support native 16kHz mono Int16. Using preferred format: "
                      << format.sampleRate() << " Hz, " << format.channelCount() << " ch, format="
                      << static_cast<int>(format.sampleFormat()) << "\n";
        } else {
            std::cout << "[AudioCapture] Native 16 kHz mono Int16 capture supported.\n";
        }

        m_activeFormat = format;
        m_audioSource = std::make_unique<QAudioSource>(dev, format);

        // Monitor state transitions and unexpected hardware errors
        connect(m_audioSource.get(), &QAudioSource::stateChanged, this, [this](QAudio::State state) {
            if (state == QAudio::StoppedState && m_recording) {
                if (m_audioSource && m_audioSource->error() != QAudio::NoError) {
                    m_recording = false;
                    std::cout << "[AudioCapture] Hardware error or device disconnected. QAudio error: "
                              << m_audioSource->error() << "\n";
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
            std::cout << "[AudioCapture] Failed to open QBuffer for writing.\n";
            return false;
        }

        m_audioSource->start(&m_ioBuffer);
        m_recording = (m_audioSource->error() == QAudio::NoError);
        std::cout << "[AudioCapture] Capture start status: " << (m_recording ? "SUCCESS" : "FAILED")
                  << " (error code: " << m_audioSource->error() << ")\n";
        return m_recording;
    }

    AudioBuffer stop() override
    {
        if (!m_recording && m_pcmBuffer.isEmpty()) {
            std::cout << "[AudioCapture] Stop called but no audio was recorded.\n";
            return AudioBuffer{};
        }

        std::cout << "[AudioCapture] Stop called. Captured " << m_pcmBuffer.size()
                  << " raw bytes from device \"" << m_activeDeviceName << "\"\n";

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
            std::cout << "[AudioCapture] Format conversion failed: " << res.errorMessage << "\n";
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
    std::string m_preferredDeviceId;
    std::string m_activeDeviceName;
    bool m_fallbackActive{false};
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

void AudioCaptureService::setPreferredDeviceId(const std::string& id)
{
    if (m_impl->m_source) {
        m_impl->m_source->setPreferredDeviceId(id);
    }
}

std::string AudioCaptureService::preferredDeviceId() const
{
    if (m_impl->m_source) {
        return m_impl->m_source->preferredDeviceId();
    }
    return {};
}

std::string AudioCaptureService::activeDeviceName() const
{
    if (m_impl->m_source) {
        return m_impl->m_source->activeDeviceName();
    }
    return {};
}

bool AudioCaptureService::isFallbackActive() const
{
    if (m_impl->m_source) {
        return m_impl->m_source->isFallbackActive();
    }
    return false;
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
