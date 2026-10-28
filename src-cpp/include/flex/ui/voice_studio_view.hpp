#ifndef FLEX_UI_VOICE_STUDIO_VIEW_HPP
#define FLEX_UI_VOICE_STUDIO_VIEW_HPP

#include "flex/ui/ai_core_widget.hpp"
#include "flex/voice/voice_conversation_controller.hpp"

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>

#include <memory>

namespace flex::ui {

/**
 * Dedicated Voice Studio View for FLEX 1.0.
 *
 * Provides a specialized workspace for hands-free and voice-first interaction:
 * - Central AI Core visualization reacting to voice lifecycle states.
 * - Prominent Audio Capture controls with live status telemetry.
 * - Transcript Review & Editing Deck (mandated by Voice Subsystem design).
 * - Voice Output (TTS) toggles and test synthesis.
 * - Hardware device and Whisper model diagnostics.
 */
class VoiceStudioView : public QWidget {
    Q_OBJECT

public:
    explicit VoiceStudioView(
        std::shared_ptr<voice::VoiceConversationController> controller,
        QWidget* parent = nullptr);

    ~VoiceStudioView() override = default;

    void setVoiceController(std::shared_ptr<voice::VoiceConversationController> controller);

signals:
    void sendSpokenPromptRequested(const QString& promptText);

private slots:
    void onRecordClicked();
    void onConfirmTranscriptClicked();
    void onDiscardTranscriptClicked();
    void onTestTtsClicked();
    void onVoiceOutputToggled();

    // Voice conversation controller callbacks
    void onVoiceStateChanged(flex::voice::VoiceSessionState state);
    void onTranscriptReady(const QString& transcript);
    void onVoiceError(const QString& errorMessage);
    void onAudioCaptured(size_t sampleCount, double durationSeconds);

private:
    void setupUi();
    void updateUiForState(flex::voice::VoiceSessionState state);

    std::shared_ptr<voice::VoiceConversationController> m_controller;

    AICoreWidget* m_coreWidget{nullptr};

    QLabel* m_statusHeaderLabel{nullptr};
    QLabel* m_telemetryDetailsLabel{nullptr};

    QPushButton* m_recordBtn{nullptr};
    QPushButton* m_testTtsBtn{nullptr};
    QPushButton* m_voiceOutputBtn{nullptr};

    // Transcript review card
    QFrame* m_reviewCard{nullptr};
    QPlainTextEdit* m_transcriptEdit{nullptr};
    QPushButton* m_confirmBtn{nullptr};
    QPushButton* m_discardBtn{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_VOICE_STUDIO_VIEW_HPP

