#include "flex/ui/voice_studio_view.hpp"
#include "flex/ui/theme.hpp"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QVBoxLayout>

namespace flex::ui {

VoiceStudioView::VoiceStudioView(
    std::shared_ptr<voice::VoiceConversationController> controller,
    QWidget* parent)
    : QWidget(parent)
    , m_controller(std::move(controller))
{
    setupUi();
    if (m_controller) {
        setVoiceController(m_controller);
    }
}

void VoiceStudioView::setupUi()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");

    auto* container = new QWidget();
    container->setStyleSheet("background-color: transparent;");
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(36, 28, 36, 28);
    layout->setSpacing(18);
    layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // 1. Header & Title
    auto* titleBox = new QWidget(container);
    auto* titleLayout = new QVBoxLayout(titleBox);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(4);
    titleLayout->setAlignment(Qt::AlignCenter);

    auto* titleLbl = new QLabel("🎙️ Voice Studio", titleBox);
    titleLbl->setStyleSheet("font-size: 20px; font-weight: 700; color: #F8FAFC;");
    titleLbl->setAlignment(Qt::AlignCenter);

    auto* subLbl = new QLabel("High-fidelity local speech-to-text (Whisper) and native speech synthesis (SAPI)", titleBox);
    subLbl->setStyleSheet("font-size: 12px; color: #94A3B8;");
    subLbl->setAlignment(Qt::AlignCenter);

    titleLayout->addWidget(titleLbl);
    titleLayout->addWidget(subLbl);
    layout->addWidget(titleBox);

    // 2. Central Animated AI Core
    m_coreWidget = new AICoreWidget(container, false);
    m_coreWidget->setFixedSize(220, 200);
    layout->addWidget(m_coreWidget, 0, Qt::AlignCenter);

    // 3. Status Header
    m_statusHeaderLabel = new QLabel("○ Microphone Ready — Click to Start Speaking", container);
    m_statusHeaderLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::TextAccentCyan));
    m_statusHeaderLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusHeaderLabel);

    // 4. Primary Voice Action Deck
    auto* actionsCard = new QFrame(container);
    actionsCard->setObjectName("UserProfileCard");
    actionsCard->setFixedWidth(640);

    auto* actionsLayout = new QVBoxLayout(actionsCard);
    actionsLayout->setContentsMargins(16, 16, 16, 16);
    actionsLayout->setSpacing(12);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(12);

    m_recordBtn = new QPushButton("🎙 Start Recording", actionsCard);
    m_recordBtn->setObjectName("PrimaryButton");
    m_recordBtn->setCursor(Qt::PointingHandCursor);
    m_recordBtn->setMinimumHeight(38);
    m_recordBtn->setStyleSheet(
        QString("QPushButton#PrimaryButton { background-color: %1; color: #FFFFFF; font-size: 13px; font-weight: 700; }")
        .arg(Theme::AccentPurple)
    );
    connect(m_recordBtn, &QPushButton::clicked, this, &VoiceStudioView::onRecordClicked);

    m_testTtsBtn = new QPushButton("🔊 Test Voice", actionsCard);
    m_testTtsBtn->setCursor(Qt::PointingHandCursor);
    m_testTtsBtn->setMinimumHeight(38);
    connect(m_testTtsBtn, &QPushButton::clicked, this, &VoiceStudioView::onTestTtsClicked);

    m_voiceOutputBtn = new QPushButton("🔊 Voice Output: ON", actionsCard);
    m_voiceOutputBtn->setCursor(Qt::PointingHandCursor);
    m_voiceOutputBtn->setMinimumHeight(38);
    connect(m_voiceOutputBtn, &QPushButton::clicked, this, &VoiceStudioView::onVoiceOutputToggled);

    btnRow->addWidget(m_recordBtn, 2);
    btnRow->addWidget(m_testTtsBtn, 1);
    btnRow->addWidget(m_voiceOutputBtn, 1);
    actionsLayout->addLayout(btnRow);

    m_telemetryDetailsLabel = new QLabel("Format: 16 kHz Mono PCM | Model: Whisper ggml-tiny.en | Engine: Windows SAPI 5.4", actionsCard);
    m_telemetryDetailsLabel->setStyleSheet("font-family: 'Consolas', monospace; font-size: 10px; color: #64748B;");
    m_telemetryDetailsLabel->setAlignment(Qt::AlignCenter);
    actionsLayout->addWidget(m_telemetryDetailsLabel);

    layout->addWidget(actionsCard, 0, Qt::AlignCenter);

    // 5. Transcript Review & Editing Deck
    m_reviewCard = new QFrame(container);
    m_reviewCard->setObjectName("UserProfileCard");
    m_reviewCard->setFixedWidth(640);
    m_reviewCard->setVisible(false);

    auto* reviewLayout = new QVBoxLayout(m_reviewCard);
    reviewLayout->setContentsMargins(16, 16, 16, 16);
    reviewLayout->setSpacing(10);

    auto* revHdr = new QLabel("TRANSCRIPT REVIEW & CORRECTION", m_reviewCard);
    revHdr->setObjectName("SectionHeaderLabel");
    reviewLayout->addWidget(revHdr);

    m_transcriptEdit = new QPlainTextEdit(m_reviewCard);
    m_transcriptEdit->setPlaceholderText("Spoken transcript will appear here. You can edit it before sending...");
    m_transcriptEdit->setFixedHeight(80);
    reviewLayout->addWidget(m_transcriptEdit);

    auto* revBtnRow = new QHBoxLayout();
    revBtnRow->setSpacing(10);

    m_confirmBtn = new QPushButton("✓ Send Spoken Prompt", m_reviewCard);
    m_confirmBtn->setObjectName("PrimaryButton");
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    connect(m_confirmBtn, &QPushButton::clicked, this, &VoiceStudioView::onConfirmTranscriptClicked);

    m_discardBtn = new QPushButton("✕ Discard", m_reviewCard);
    m_discardBtn->setCursor(Qt::PointingHandCursor);
    connect(m_discardBtn, &QPushButton::clicked, this, &VoiceStudioView::onDiscardTranscriptClicked);

    revBtnRow->addWidget(m_confirmBtn);
    revBtnRow->addWidget(m_discardBtn);
    reviewLayout->addLayout(revBtnRow);

    layout->addWidget(m_reviewCard, 0, Qt::AlignCenter);

    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

void VoiceStudioView::setVoiceController(std::shared_ptr<voice::VoiceConversationController> controller)
{
    m_controller = std::move(controller);
    if (!m_controller) {
        return;
    }

    connect(m_controller.get(), &voice::VoiceConversationController::stateChanged,
            this, &VoiceStudioView::onVoiceStateChanged);
    connect(m_controller.get(), &voice::VoiceConversationController::transcriptReadyForReview,
            this, &VoiceStudioView::onTranscriptReady);
    connect(m_controller.get(), &voice::VoiceConversationController::errorOccurred,
            this, &VoiceStudioView::onVoiceError);
    connect(m_controller.get(), &voice::VoiceConversationController::recordingFinished,
            this, [this](const flex::voice::AudioBuffer& buffer) {
                onAudioCaptured(buffer.sampleCount(), static_cast<double>(buffer.durationMs) / 1000.0);
            });

    updateUiForState(m_controller->state());
}

void VoiceStudioView::onRecordClicked()
{
    if (!m_controller) {
        return;
    }

    if (m_controller->state() == voice::VoiceSessionState::Recording) {
        m_controller->stopRecording();
    } else if (m_controller->state() == voice::VoiceSessionState::Idle) {
        m_reviewCard->setVisible(false);
        m_controller->startRecording();
    }
}

void VoiceStudioView::onConfirmTranscriptClicked()
{
    if (!m_controller) {
        return;
    }
    QString text = m_transcriptEdit->toPlainText().trimmed();
    if (!text.isEmpty()) {
        m_reviewCard->setVisible(false);
        m_controller->submitTranscript(text);
        emit sendSpokenPromptRequested(text);
    }
}

void VoiceStudioView::onDiscardTranscriptClicked()
{
    if (!m_controller) {
        return;
    }
    m_reviewCard->setVisible(false);
    m_controller->cancelVoiceInput();
}

void VoiceStudioView::onTestTtsClicked()
{
    if (!m_controller || !m_controller->textToSpeechService()) {
        return;
    }

    auto tts = m_controller->textToSpeechService();
    if (tts->isSpeaking()) {
        tts->stop();
    } else {
        tts->speak("FLEX 1.0 voice studio synthesized speech verified.");
    }
}

void VoiceStudioView::onVoiceOutputToggled()
{
    if (!m_controller) {
        return;
    }
    bool enabled = !m_controller->isVoiceOutputEnabled();
    m_controller->setVoiceOutputEnabled(enabled);
    m_voiceOutputBtn->setText(enabled ? "🔊 Voice Output: ON" : "🔇 Voice Output: OFF");
}

void VoiceStudioView::onVoiceStateChanged(voice::VoiceSessionState state)
{
    updateUiForState(state);
}

void VoiceStudioView::updateUiForState(voice::VoiceSessionState state)
{
    switch (state) {
    case voice::VoiceSessionState::Idle:
        m_coreWidget->setVisualState(CoreVisualState::Idle);
        m_recordBtn->setText("🎙 Start Recording");
        m_recordBtn->setEnabled(true);
        m_statusHeaderLabel->setText("○ Microphone Ready — Click to Start Speaking");
        m_statusHeaderLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::TextAccentCyan));
        break;

    case voice::VoiceSessionState::Recording:
        m_coreWidget->setVisualState(CoreVisualState::Attentive);
        m_recordBtn->setText("⏹ Stop Recording");
        m_recordBtn->setEnabled(true);
        m_statusHeaderLabel->setText("● Recording Audio from Microphone...");
        m_statusHeaderLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::StatusError));
        break;

    case voice::VoiceSessionState::Transcribing:
        m_coreWidget->setVisualState(CoreVisualState::Thinking);
        m_recordBtn->setText("⏳ Transcribing...");
        m_recordBtn->setEnabled(false);
        m_statusHeaderLabel->setText("● Whisper Speech Recognition In Progress...");
        m_statusHeaderLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::StatusWarning));
        break;

    case voice::VoiceSessionState::Reviewing:
        m_coreWidget->setVisualState(CoreVisualState::Idle);
        m_recordBtn->setText("🎙 Record Again");
        m_recordBtn->setEnabled(true);
        m_statusHeaderLabel->setText("● Review Transcript Below");
        m_statusHeaderLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::TextAccentViolet));
        m_reviewCard->setVisible(true);
        break;

    case voice::VoiceSessionState::Thinking:
        m_coreWidget->setVisualState(CoreVisualState::Responding);
        m_recordBtn->setText("🎙 Busy");
        m_recordBtn->setEnabled(false);
        m_statusHeaderLabel->setText("● Local AI Processing Prompt...");
        m_statusHeaderLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::TextAccentCyan));
        break;

    case voice::VoiceSessionState::Speaking:
        m_coreWidget->setVisualState(CoreVisualState::Responding);
        m_recordBtn->setText("🎙 Busy (Speaking)");
        m_recordBtn->setEnabled(false);
        m_statusHeaderLabel->setText("● SAPI Synthesizing Voice Response...");
        m_statusHeaderLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;").arg(Theme::StatusSuccess));
        break;

    case voice::VoiceSessionState::Error:
        m_coreWidget->setVisualState(CoreVisualState::Error);
        m_recordBtn->setText("🎙 Start Recording");
        m_recordBtn->setEnabled(true);
        break;
    }
}

void VoiceStudioView::onTranscriptReady(const QString& transcript)
{
    m_transcriptEdit->setPlainText(transcript);
    m_reviewCard->setVisible(true);
    m_transcriptEdit->setFocus();
}

void VoiceStudioView::onVoiceError(const QString& errorMessage)
{
    m_statusHeaderLabel->setText(QString("✕ Voice Error: %1").arg(errorMessage));
    m_statusHeaderLabel->setStyleSheet(QString("font-size: 13px; font-weight: 600; color: %1;").arg(Theme::StatusError));
}

void VoiceStudioView::onAudioCaptured(size_t sampleCount, double durationSeconds)
{
    m_telemetryDetailsLabel->setText(
        QString("Captured %1 samples (approx %2s) | 16 kHz Mono | Whisper ggml-tiny.en")
        .arg(sampleCount)
        .arg(QString::number(durationSeconds, 'f', 1))
    );
}

} // namespace flex::ui
