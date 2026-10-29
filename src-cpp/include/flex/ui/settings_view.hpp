#ifndef FLEX_UI_SETTINGS_VIEW_HPP
#define FLEX_UI_SETTINGS_VIEW_HPP

#include "flex/backend.hpp"
#include "flex/voice/audio_capture_service.hpp"
#include "flex/voice/text_to_speech_service.hpp"
#include "flex/voice/speech_to_text_service.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include <memory>

namespace flex::ui {

/**
 * Settings & Hardware Preferences View for FLEX 1.0 (Track A).
 *
 * Controls:
 * - Ollama Server Endpoint URL
 * - Non-blocking connection test
 * - Active Model Name
 * - Inference Temperature (0.0 to 2.0)
 * - Context Window Tokens (num_ctx)
 * - Request Timeout (seconds)
 * - Microphone Audio Input Device selector
 * - SAPI TTS Speech Rate (-10 to +10) and Volume (0 to 100%)
 * - Whisper Speech-to-Text Model resolution status & path
 * - Atomic persistence to %LOCALAPPDATA%\FLEX\config.ini
 */
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(
        std::shared_ptr<BackendCoordinator> coordinator,
        QWidget* parent = nullptr);

    explicit SettingsView(
        std::shared_ptr<BackendCoordinator> coordinator,
        std::shared_ptr<voice::AudioCaptureService> audioCapture,
        std::shared_ptr<voice::TextToSpeechService> tts = nullptr,
        std::shared_ptr<voice::SpeechToTextService> stt = nullptr,
        QWidget* parent = nullptr);

    ~SettingsView() override = default;

    void setAudioCaptureService(std::shared_ptr<voice::AudioCaptureService> service);
    void setTextToSpeechService(std::shared_ptr<voice::TextToSpeechService> service);
    void setSpeechToTextService(std::shared_ptr<voice::SpeechToTextService> service);

    void loadCurrentConfig();

signals:
    void settingsUpdated();

private slots:
    void onTestConnectionClicked();
    void onSaveClicked();
    void onResetClicked();
    void refreshDeviceList();

private:
    void setupUi();

    std::shared_ptr<BackendCoordinator> m_coordinator;
    std::shared_ptr<voice::AudioCaptureService> m_audioCapture;
    std::shared_ptr<voice::TextToSpeechService> m_tts;
    std::shared_ptr<voice::SpeechToTextService> m_stt;

    // 1. Ollama controls
    QLineEdit* m_endpointEdit{nullptr};
    QPushButton* m_testBtn{nullptr};
    QLabel* m_testResultLabel{nullptr};
    QLineEdit* m_modelEdit{nullptr};
    QDoubleSpinBox* m_tempSpin{nullptr};
    QSpinBox* m_numCtxSpin{nullptr};
    QSpinBox* m_timeoutSpin{nullptr};

    // 2. Hardware / Audio controls
    QComboBox* m_micCombo{nullptr};
    QLabel* m_micStatusLabel{nullptr};
    QSpinBox* m_ttsRateSpin{nullptr};
    QSpinBox* m_ttsVolumeSpin{nullptr};

    // 3. Whisper Speech-to-Text controls
    QLineEdit* m_whisperPathEdit{nullptr};
    QLabel* m_whisperStatusLabel{nullptr};

    // 4. Persistence controls & feedback
    QPushButton* m_saveBtn{nullptr};
    QPushButton* m_resetBtn{nullptr};
    QLabel* m_saveStatusLabel{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_SETTINGS_VIEW_HPP
