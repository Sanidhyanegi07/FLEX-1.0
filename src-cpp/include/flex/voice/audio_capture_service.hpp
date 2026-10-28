#ifndef FLEX_VOICE_AUDIO_CAPTURE_SERVICE_HPP
#define FLEX_VOICE_AUDIO_CAPTURE_SERVICE_HPP

#include "flex/voice/audio_buffer.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flex::voice {

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
};

class AudioCaptureServiceImpl;

/**
 * Native Audio Capture Service powered by Qt 6 Multimedia.
 *
 * Responsibilities:
 * - Enumerates input audio devices.
 * - Captures 16 kHz 16-bit mono PCM streams on a non-blocking background buffer.
 * - Extracts normalized floating-point samples [-1.0f, 1.0f] for future STT inference.
 * - Handles device removal, errors, cancellation, and clean shutdown.
 * - Supports dependency injection of mock audio sources for CI test isolation.
 */
class AudioCaptureService : public QObject {
    Q_OBJECT

public:
    explicit AudioCaptureService(
        std::shared_ptr<IAudioCaptureSource> customSource = nullptr,
        QObject* parent = nullptr);

    ~AudioCaptureService() override;

    // Device Discovery
    [[nodiscard]] std::vector<AudioDeviceInfo> availableInputDevices() const;
    [[nodiscard]] AudioDeviceInfo defaultInputDevice() const;

    // Capture Operations
    bool startCapture();
    AudioBuffer stopCapture();
    void cancelCapture();

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
    void audioLevelChanged(double levelRms);

private:
    std::unique_ptr<AudioCaptureServiceImpl> m_impl;
};

// String helper
const char* audioCaptureStateToString(AudioCaptureState state) noexcept;
const char* audioCaptureErrorToString(AudioCaptureError error) noexcept;

} // namespace flex::voice

#endif // FLEX_VOICE_AUDIO_CAPTURE_SERVICE_HPP

