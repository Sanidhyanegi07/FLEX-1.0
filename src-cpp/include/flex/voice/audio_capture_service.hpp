#ifndef FLEX_VOICE_AUDIO_CAPTURE_SERVICE_HPP
#define FLEX_VOICE_AUDIO_CAPTURE_SERVICE_HPP

#include "flex/voice/audio_buffer.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flex::voice {

enum class AudioSampleFormat {
    Unknown = 0,
    Int16,
    Float32,
    Int32,
    UInt8
};

struct AudioResampleResult {
    bool success{false};
    AudioBuffer buffer;
    std::string errorMessage;
};

/**
 * Decodes raw PCM bytes of any supported sample format and channel count,
 * normalizes samples to [-1.0f, 1.0f], downmixes multi-channel to mono,
 * and resamples the audio stream to target sample rate (default 16 kHz).
 *
 * Algorithm: Linear interpolation time-domain resampling.
 * Limitations: Does not implement polyphase anti-aliasing sinc filtering;
 * input frequency components above target Nyquist (8 kHz for 16 kHz output)
 * will fold back (alias) into the audible band.
 */
AudioResampleResult convertAndResampleTo16kMono(
    const void* data,
    size_t byteCount,
    int sourceSampleRate,
    int sourceChannels,
    AudioSampleFormat sourceFormat,
    int targetSampleRate = 16000);

enum class AudioCaptureState {
    Idle = 0,
    Recording,
    Stopping,
    Error
};

enum class AudioCaptureError {
    None = 0,
    DeviceNotFound,
    PermissionDenied,
    DeviceUnavailable,
    FormatNotSupported,
    CaptureFailed
};

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    bool isDefault{false};
};

/**
 * Abstract interface for audio capture backends.
 * Enables deterministic mock testing without physical microphones.
 */
class IAudioCaptureSource {
public:
    virtual ~IAudioCaptureSource() = default;
    virtual bool start() = 0;
    virtual AudioBuffer stop() = 0;
    virtual void cancel() = 0;
    [[nodiscard]] virtual bool isRecording() const = 0;
    [[nodiscard]] virtual std::vector<AudioDeviceInfo> availableDevices() const = 0;
    [[nodiscard]] virtual AudioDeviceInfo defaultDevice() const = 0;
    virtual void setPreferredDeviceId(const std::string& id) { (void)id; }
    [[nodiscard]] virtual std::string preferredDeviceId() const { return {}; }
    [[nodiscard]] virtual std::string activeDeviceName() const { return defaultDevice().name; }

    virtual void setErrorCallback(std::function<void(AudioCaptureError, const QString&)> callback)
    {
        m_errorCallback = std::move(callback);
    }

protected:
    std::function<void(AudioCaptureError, const QString&)> m_errorCallback;
};

class AudioCaptureServiceImpl;

/**
 * Native Audio Capture Service powered by Qt 6 Multimedia.
 *
 * Responsibilities:
 * - Enumerates input audio devices.
 * - Captures 16 kHz 16-bit mono PCM streams on a non-blocking background buffer.
 * - Automatically resamples and downmixes arbitrary device formats to 16 kHz mono.
 * - Extracts normalized floating-point samples [-1.0f, 1.0f] for future STT inference.
 * - Handles device removal, errors, cancellation, and clean shutdown.
 * - Automatically stops capture when reaching a configurable duration limit (default 30s).
 * - Enforces hard buffer size safeguard to prevent unbounded memory growth.
 * - Supports dependency injection of mock audio sources for CI test isolation.
 */
class AudioCaptureService : public QObject {
    Q_OBJECT

public:
    explicit AudioCaptureService(
        std::shared_ptr<IAudioCaptureSource> customSource = nullptr,
        QObject* parent = nullptr);

    ~AudioCaptureService() override;

    // Device Discovery & Selection
    [[nodiscard]] std::vector<AudioDeviceInfo> availableInputDevices() const;
    [[nodiscard]] AudioDeviceInfo defaultInputDevice() const;
    void setPreferredDeviceId(const std::string& id);
    [[nodiscard]] std::string preferredDeviceId() const;
    [[nodiscard]] std::string activeDeviceName() const;

    // Capture Operations
    bool startCapture();
    AudioBuffer stopCapture();
    void cancelCapture();

    // Duration Limit Controls
    void setMaxDurationMs(int ms);
    [[nodiscard]] int maxDurationMs() const;
    [[nodiscard]] bool wasMaxDurationReached() const;

    // State Inspection
    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] AudioCaptureState state() const;
    [[nodiscard]] AudioCaptureError lastError() const;
    [[nodiscard]] QString lastErrorMessage() const;

    // Mock Source Injection
    void setCaptureSource(std::shared_ptr<IAudioCaptureSource> source);

signals:
    void stateChanged(flex::voice::AudioCaptureState newState);
    void captureStarted();
    void captureFinished(const flex::voice::AudioBuffer& buffer);
    void captureCancelled();
    void captureError(flex::voice::AudioCaptureError error, const QString& message);
    void captureMaxDurationReached();
    void audioLevelChanged(double levelRms);

private slots:
    void onMaxDurationTimeout();

private:
    std::unique_ptr<AudioCaptureServiceImpl> m_impl;
};

// String helper
const char* audioCaptureStateToString(AudioCaptureState state) noexcept;
const char* audioCaptureErrorToString(AudioCaptureError error) noexcept;

} // namespace flex::voice

#endif // FLEX_VOICE_AUDIO_CAPTURE_SERVICE_HPP

