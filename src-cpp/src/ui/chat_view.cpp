#include "flex/ui/chat_view.hpp"
#include "flex/ui/theme.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTimer>
#include <iostream>

namespace flex::ui {

// -----------------------------------------------------------------------------
// ChatInputEdit Implementation
// -----------------------------------------------------------------------------

ChatInputEdit::ChatInputEdit(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setPlaceholderText("Ask FLEX anything... (Enter to send, Shift+Enter for new line)");
    setMinimumHeight(50);
    setMaximumHeight(120);
    setStyleSheet(
        "QPlainTextEdit { background-color: transparent; border: none; color: #F8FAFC; "
        "font-family: 'Segoe UI', system-ui, sans-serif; font-size: 13px; padding: 4px; }"
    );
}

void ChatInputEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
            QPlainTextEdit::keyPressEvent(event);
        } else {
            event->accept();
            emit submitRequested();
        }
    } else {
        QPlainTextEdit::keyPressEvent(event);
    }
}

// -----------------------------------------------------------------------------
// ChatView Implementation
// -----------------------------------------------------------------------------

ChatView::ChatView(QWidget* parent)
    : QWidget(parent)
{
    m_audioCapture = std::make_shared<voice::AudioCaptureService>(nullptr, this);
    m_tts = std::make_shared<voice::TextToSpeechService>(nullptr, this);

    connect(m_tts.get(), &voice::TextToSpeechService::speechStarted, this, &ChatView::onTtsStarted);
    connect(m_tts.get(), &voice::TextToSpeechService::speechFinished, this, &ChatView::onTtsFinished);
    connect(m_tts.get(), &voice::TextToSpeechService::speechCancelled, this, &ChatView::onTtsCancelled);
    connect(m_tts.get(), &voice::TextToSpeechService::speechError, this, &ChatView::onTtsError);

    connect(m_audioCapture.get(), &voice::AudioCaptureService::captureStarted, this, &ChatView::onCaptureStarted);
    connect(m_audioCapture.get(), &voice::AudioCaptureService::captureFinished, this, &ChatView::onCaptureFinished);
    connect(m_audioCapture.get(), &voice::AudioCaptureService::captureCancelled, this, &ChatView::onCaptureCancelled);
    connect(m_audioCapture.get(), &voice::AudioCaptureService::captureError, this, &ChatView::onCaptureError);

    setupUi();
}

void ChatView::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 1. Header Bar
    auto* headerFrame = new QFrame(this);
    headerFrame->setObjectName("ChatHeaderFrame");
    headerFrame->setStyleSheet(
        QString("QFrame#ChatHeaderFrame { background-color: %1; border-bottom: 1px solid %2; padding: 6px 16px; }")
        .arg(Theme::BgPanel, Theme::Border)
    );

    auto* headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(12, 6, 12, 6);

    m_titleLabel = new QLabel("New Conversation", headerFrame);
    m_titleLabel->setStyleSheet("color: #F8FAFC; font-size: 15px; font-weight: 700;");

    m_compactCore = new AICoreWidget(headerFrame, true);
    m_compactCore->setVisible(false); // Only visible during active chat

    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_compactCore);

    rootLayout->addWidget(headerFrame);

    // 2. Central Scroll Area for Messages
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");

    m_messagesContainer = new QWidget();
    m_messagesContainer->setStyleSheet("background-color: transparent;");
    m_messagesLayout = new QVBoxLayout(m_messagesContainer);
    m_messagesLayout->setContentsMargins(20, 20, 20, 20);
    m_messagesLayout->setSpacing(14);

    // Empty state Hero Widget
    m_emptyHeroWidget = new QWidget(m_messagesContainer);
    auto* heroLayout = new QVBoxLayout(m_emptyHeroWidget);
    heroLayout->setContentsMargins(10, 30, 10, 30);
    heroLayout->setSpacing(12);
    heroLayout->setAlignment(Qt::AlignCenter);

    m_heroCore = new AICoreWidget(m_emptyHeroWidget, false);
    heroLayout->addWidget(m_heroCore, 0, Qt::AlignCenter);

    auto* heroTitle = new QLabel(
        "What can I help you build today?",
        m_emptyHeroWidget
    );
    heroTitle->setStyleSheet("color: #F8FAFC; font-size: 18px; font-weight: 700;");
    heroTitle->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroTitle);

    auto* heroDesc = new QLabel(
        "Local AI companion with native C core determinism and private storage.",
        m_emptyHeroWidget
    );
    heroDesc->setStyleSheet("color: #94A3B8; font-size: 13px;");
    heroDesc->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroDesc);

    // Suggestion prompt chips container
    auto* chipsContainer = new QWidget(m_emptyHeroWidget);
    auto* chipsLayout = new QHBoxLayout(chipsContainer);
    chipsLayout->setContentsMargins(0, 10, 0, 0);
    chipsLayout->setSpacing(10);
    chipsLayout->setAlignment(Qt::AlignCenter);

    struct ChipDef {
        QString label;
        QString prompt;
    };
    const std::vector<ChipDef> chips = {
        {"💡 Explain C11 Memory Safety", "Explain how C11 memory safety and bounded buffers prevent security exploits."},
        {"⚡ Test Local Ollama Inference", "Test local Ollama inference: summarize the key strengths of native C++ desktop software."},
        {"🎙️ Record Microphone Voice", "Explain how local Whisper speech-to-text operates completely offline."},
        {"📁 Inspect Project Files", "What is the architectural purpose of core-c in this hybrid AI assistant?"}
    };

    for (const auto& chip : chips) {
        auto* chipBtn = new QPushButton(chip.label, chipsContainer);
        chipBtn->setObjectName("PromptSuggestionChip");
        chipBtn->setCursor(Qt::PointingHandCursor);
        QString promptText = chip.prompt;
        connect(chipBtn, &QPushButton::clicked, this, [this, promptText]() {
            setInputText(promptText);
            focusInput();
        });
        chipsLayout->addWidget(chipBtn);
    }
    heroLayout->addWidget(chipsContainer);

    m_messagesLayout->addWidget(m_emptyHeroWidget);
    m_messagesLayout->addStretch();

    m_scrollArea->setWidget(m_messagesContainer);
    rootLayout->addWidget(m_scrollArea, 1);

    // 3. Loading Status Banner
    m_loadingBanner = new QFrame(this);
    m_loadingBanner->setStyleSheet(
        "QFrame { background-color: #111B2B; border-top: 1px solid #1D3A5F; padding: 4px 16px; }"
    );
    auto* loadingLayout = new QHBoxLayout(m_loadingBanner);
    loadingLayout->setContentsMargins(16, 4, 16, 4);
    m_loadingLabel = new QLabel("FLEX AI is thinking...", m_loadingBanner);
    m_loadingLabel->setStyleSheet("color: #38BDF8; font-size: 12px; font-weight: 600;");
    loadingLayout->addWidget(m_loadingLabel);
    loadingLayout->addStretch();
    m_loadingBanner->setVisible(false);
    rootLayout->addWidget(m_loadingBanner);

    // 4. Error Banner
    m_errorBanner = new QFrame(this);
    m_errorBanner->setStyleSheet(
        "QFrame { background-color: #2D1418; border-top: 1px solid #7F1D1D; padding: 6px 16px; }"
    );
    auto* errorLayout = new QHBoxLayout(m_errorBanner);
    errorLayout->setContentsMargins(16, 6, 16, 6);

    m_errorTextLabel = new QLabel(m_errorBanner);
    m_errorTextLabel->setStyleSheet("color: #F87171; font-size: 12px;");
    m_errorTextLabel->setWordWrap(true);

    auto* dismissBtn = new QPushButton("✕", m_errorBanner);
    dismissBtn->setFixedSize(20, 20);
    dismissBtn->setStyleSheet("QPushButton { background: transparent; border: none; color: #F87171; font-weight: bold; }");
    connect(dismissBtn, &QPushButton::clicked, this, &ChatView::hideErrorBanner);

    errorLayout->addWidget(m_errorTextLabel, 1);
    errorLayout->addWidget(dismissBtn);
    m_errorBanner->setVisible(false);
    rootLayout->addWidget(m_errorBanner);

    // 5. Input Deck
    auto* inputContainer = new QFrame(this);
    inputContainer->setObjectName("InputContainerFrame");
    inputContainer->setStyleSheet(
        QString("QFrame#InputContainerFrame { background-color: %1; border-top: 1px solid %2; padding: 12px 18px; }")
        .arg(Theme::BgPanel, Theme::Border)
    );

    auto* inputDeckLayout = new QVBoxLayout(inputContainer);
    inputDeckLayout->setContentsMargins(0, 0, 0, 0);
    inputDeckLayout->setSpacing(8);

    auto* inputCard = new QFrame(inputContainer);
    inputCard->setStyleSheet(
        QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; padding: 6px; }")
        .arg(Theme::BgInput, Theme::Border)
    );
    auto* cardLayout = new QVBoxLayout(inputCard);
    cardLayout->setContentsMargins(4, 4, 4, 4);
    cardLayout->setSpacing(4);

    m_inputEdit = new ChatInputEdit(inputCard);
    connect(m_inputEdit, &ChatInputEdit::submitRequested, this, &ChatView::onSendClicked);
    connect(m_inputEdit, &QPlainTextEdit::textChanged, this, &ChatView::onInputTextChanged);

    // Controls Row: Voice-ready slot + Send button
    auto* controlsRow = new QHBoxLayout();
    controlsRow->setContentsMargins(4, 0, 4, 0);

    // Voice Controls Layout (Audio capture & SAPI TTS foundation)
    m_inputControlsLayout = new QHBoxLayout();
    m_inputControlsLayout->setContentsMargins(0, 0, 0, 0);
    m_inputControlsLayout->setSpacing(8);

    m_recordAudioBtn = new QPushButton("🎙 Record Audio", inputCard);
    m_recordAudioBtn->setCursor(Qt::PointingHandCursor);
    m_recordAudioBtn->setStyleSheet(
        "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
        "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
    );
    connect(m_recordAudioBtn, &QPushButton::clicked, this, &ChatView::onRecordButtonClicked);
    m_inputControlsLayout->addWidget(m_recordAudioBtn);

    m_testTtsBtn = new QPushButton("🔊 Test Voice", inputCard);
    m_testTtsBtn->setCursor(Qt::PointingHandCursor);
    m_testTtsBtn->setStyleSheet(
        "QPushButton { background-color: #171826; color: #A78BFA; border: 1px solid #2E3650; "
        "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background-color: #23273D; color: #FFFFFF; }"
    );
    connect(m_testTtsBtn, &QPushButton::clicked, this, &ChatView::onTestTtsButtonClicked);
    m_inputControlsLayout->addWidget(m_testTtsBtn);

    m_voiceOutputBtn = new QPushButton("🔊 Voice Output: ON", inputCard);
    m_voiceOutputBtn->setCursor(Qt::PointingHandCursor);
    m_voiceOutputBtn->setStyleSheet(
        "QPushButton { background-color: #171826; color: #38BDF8; border: 1px solid #1D3A5F; "
        "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background-color: #1E293B; color: #FFFFFF; }"
    );
    connect(m_voiceOutputBtn, &QPushButton::clicked, this, &ChatView::onVoiceOutputToggled);
    m_inputControlsLayout->addWidget(m_voiceOutputBtn);

    m_audioStatusLabel = new QLabel("○ Audio: Ready", inputCard);
    m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextMuted));
    m_inputControlsLayout->addWidget(m_audioStatusLabel);

    controlsRow->addLayout(m_inputControlsLayout);

    controlsRow->addStretch();

    m_sendButton = new QPushButton("Send", inputCard);
    m_sendButton->setObjectName("PrimaryButton");
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setEnabled(false);
    connect(m_sendButton, &QPushButton::clicked, this, &ChatView::onSendClicked);
    controlsRow->addWidget(m_sendButton);

    cardLayout->addWidget(m_inputEdit);
    cardLayout->addLayout(controlsRow);

    inputDeckLayout->addWidget(inputCard);
    rootLayout->addWidget(inputContainer);
}

void ChatView::setConversation(
    const QString& id,
    const QString& title,
    const std::vector<MessageRecord>& messages)
{
    clearConversation();
    m_conversationId = id;
    m_conversationTitle = title.isEmpty() ? "New Conversation" : title;
    m_titleLabel->setText(m_conversationTitle);

    for (const auto& msg : messages) {
        QString role = QString::fromStdString(msg.role);
        QString content = QString::fromStdString(msg.content);
        QString timestamp = QString::fromStdString(msg.createdAt);
        QString model = msg.model.has_value() ? QString::fromStdString(msg.model.value()) : QString();

        auto* bubble = new MessageBubble(role, content, timestamp, model, m_messagesContainer);
        m_bubbles.push_back(bubble);
        // Insert before bottom stretch
        m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, bubble);
    }

    updateEmptyState();
    scrollToBottom();
}

void ChatView::clearConversation()
{
    if (m_audioCapture && m_audioCapture->isRecording()) {
        m_audioCapture->cancelCapture();
        if (m_recordAudioBtn) {
            m_recordAudioBtn->setText("🎙 Record Audio");
            m_recordAudioBtn->setStyleSheet(
                "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
                "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
            );
        }
    }
    if (m_tts && m_tts->isSpeaking()) {
        m_tts->stop();
        if (m_testTtsBtn) {
            m_testTtsBtn->setText("🔊 Test Voice");
            m_testTtsBtn->setStyleSheet(
                "QPushButton { background-color: #171826; color: #A78BFA; border: 1px solid #2E3650; "
                "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #23273D; color: #FFFFFF; }"
            );
        }
    }

    m_conversationId.clear();
    m_conversationTitle = "New Conversation";
    m_titleLabel->setText(m_conversationTitle);

    for (auto* bubble : m_bubbles) {
        m_messagesLayout->removeWidget(bubble);
        bubble->deleteLater();
    }
    m_bubbles.clear();
    m_currentAssistantBubble = nullptr;

    hideErrorBanner();
    updateEmptyState();
}

void ChatView::appendUserMessage(const QString& text, const QString& timestamp)
{
    auto* bubble = new MessageBubble("user", text, timestamp, QString(), m_messagesContainer);
    m_bubbles.push_back(bubble);
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, bubble);

    updateEmptyState();
    scrollToBottom();
}

void ChatView::startAssistantMessage(const QString& model, const QString& timestamp)
{
    auto* bubble = new MessageBubble("assistant", "", timestamp, model, m_messagesContainer);
    m_bubbles.push_back(bubble);
    m_currentAssistantBubble = bubble;
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, bubble);

    updateEmptyState();
    scrollToBottom();
}

void ChatView::appendAssistantChunk(const QString& chunk)
{
    if (m_currentAssistantBubble) {
        m_currentAssistantBubble->appendChunk(chunk);
        scrollToBottom();
    }
}

void ChatView::finishAssistantMessage(const QString& fullText)
{
    if (m_currentAssistantBubble) {
        if (!fullText.isEmpty() && m_currentAssistantBubble->content().isEmpty()) {
            m_currentAssistantBubble->setContent(fullText);
        }
        m_currentAssistantBubble = nullptr;
    }
    scrollToBottom();
}

void ChatView::setVisualState(CoreVisualState state)
{
    if (m_heroCore) {
        m_heroCore->setVisualState(state);
    }
    if (m_compactCore) {
        m_compactCore->setVisualState(state);
    }
}

void ChatView::setInputEnabled(bool enabled)
{
    m_inputEdit->setEnabled(enabled);
    m_sendButton->setEnabled(enabled && !m_inputEdit->toPlainText().trimmed().isEmpty());

    m_loadingBanner->setVisible(!enabled);
    if (!enabled) {
        m_loadingLabel->setText("FLEX AI is processing your request...");
    }
}

void ChatView::showErrorBanner(const QString& errorMessage)
{
    m_errorTextLabel->setText(errorMessage);
    m_errorBanner->setVisible(true);
}

void ChatView::hideErrorBanner()
{
    m_errorBanner->setVisible(false);
}

QString ChatView::inputText() const
{
    return m_inputEdit->toPlainText();
}

void ChatView::setInputText(const QString& text)
{
    m_inputEdit->setPlainText(text);
}

void ChatView::focusInput()
{
    m_inputEdit->setFocus();
}

void ChatView::onSendClicked()
{
    QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_inputEdit->clear();
    hideErrorBanner();
    emit sendRequested(text);
}

void ChatView::onInputTextChanged()
{
    bool hasText = !m_inputEdit->toPlainText().trimmed().isEmpty();
    if (m_inputEdit->isEnabled()) {
        m_sendButton->setEnabled(hasText);
    }
}

void ChatView::updateEmptyState()
{
    bool empty = m_bubbles.empty();
    m_emptyHeroWidget->setVisible(empty);
    m_compactCore->setVisible(!empty);
}

void ChatView::scrollToBottom()
{
    QTimer::singleShot(15, this, [this]() {
        if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
            m_scrollArea->verticalScrollBar()->setValue(
                m_scrollArea->verticalScrollBar()->maximum()
            );
        }
    });
}

QString ChatView::audioStatusText() const
{
    return m_audioStatusLabel ? m_audioStatusLabel->text() : QString();
}

QString ChatView::recordButtonText() const
{
    return m_recordAudioBtn ? m_recordAudioBtn->text() : QString();
}

QString ChatView::testTtsButtonText() const
{
    return m_testTtsBtn ? m_testTtsBtn->text() : QString();
}

QString ChatView::voiceOutputButtonText() const
{
    return m_voiceOutputBtn ? m_voiceOutputBtn->text() : QString();
}

void ChatView::setVoiceController(std::shared_ptr<voice::VoiceConversationController> controller)
{
    m_voiceController = std::move(controller);
    if (!m_voiceController) {
        return;
    }

    m_audioCapture = m_voiceController->audioCaptureService();
    m_tts = m_voiceController->textToSpeechService();

    connect(m_voiceController.get(), &voice::VoiceConversationController::transcriptReadyForReview,
            this, [this](const QString& text) {
        std::cout << "[ChatView] Handler received transcriptReadyForReview: \"" << text.toStdString()
                  << "\". Populating editable chat input.\n";
        setInputText(text);
        focusInput();
        if (m_sendButton) {
            m_sendButton->setEnabled(!text.trimmed().isEmpty());
        }
        if (m_audioStatusLabel) {
            m_audioStatusLabel->setText("✓ Transcript ready — Review and press Enter to send");
            m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::StatusSuccess));
        }
    });

    connect(m_voiceController.get(), &voice::VoiceConversationController::stateChanged,
            this, &ChatView::onVoiceControllerStateChanged);

    connect(m_voiceController.get(), &voice::VoiceConversationController::errorOccurred,
            this, [this](const QString& msg) {
        std::cout << "[ChatView] Handler received errorOccurred: \"" << msg.toStdString() << "\"\n";
        showErrorBanner(msg);
        if (m_audioStatusLabel) {
            if (msg.contains("No speech detected", Qt::CaseInsensitive)) {
                m_audioStatusLabel->setText("○ No speech detected");
            } else {
                m_audioStatusLabel->setText("○ Audio: Ready");
            }
            m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextMuted));
        }
    });

    connect(m_voiceController.get(), &voice::VoiceConversationController::voiceOutputToggled,
            this, [this](bool enabled) {
        if (m_voiceOutputBtn) {
            m_voiceOutputBtn->setText(enabled ? "🔊 Voice Output: ON" : "🔇 Voice Output: OFF");
            m_voiceOutputBtn->setStyleSheet(enabled ?
                "QPushButton { background-color: #171826; color: #38BDF8; border: 1px solid #1D3A5F; "
                "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #1E293B; color: #FFFFFF; }" :
                "QPushButton { background-color: #181926; color: #64748B; border: 1px solid #334155; "
                "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #1E293B; color: #94A3B8; }"
            );
        }
    });
}

void ChatView::onVoiceOutputToggled()
{
    if (m_voiceController) {
        m_voiceController->setVoiceOutputEnabled(!m_voiceController->isVoiceOutputEnabled());
    } else {
        bool enabled = m_voiceOutputBtn && m_voiceOutputBtn->text().contains("ON");
        if (m_voiceOutputBtn) {
            m_voiceOutputBtn->setText(enabled ? "🔇 Voice Output: OFF" : "🔊 Voice Output: ON");
        }
    }
}

void ChatView::onVoiceControllerStateChanged(voice::VoiceSessionState state)
{
    switch (state) {
        case voice::VoiceSessionState::Recording:
            if (m_recordAudioBtn) {
                m_recordAudioBtn->setText("⏹ Stop Capture");
                m_recordAudioBtn->setStyleSheet(
                    "QPushButton { background-color: #3D1418; color: #F87171; border: 1px solid #EF4444; "
                    "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: bold; }"
                );
            }
            if (m_audioStatusLabel) {
                m_audioStatusLabel->setText("● Recording microphone...");
                m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::StatusError));
            }
            setVisualState(CoreVisualState::Attentive);
            break;

        case voice::VoiceSessionState::Transcribing:
            if (m_recordAudioBtn) {
                m_recordAudioBtn->setText("⏳ Transcribing...");
                m_recordAudioBtn->setStyleSheet(
                    "QPushButton { background-color: #0E2938; color: #38BDF8; border: 1px solid #0284C7; "
                    "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: bold; }"
                );
            }
            if (m_audioStatusLabel) {
                m_audioStatusLabel->setText("● Transcribing with Whisper...");
                m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextAccentCyan));
            }
            setVisualState(CoreVisualState::Thinking);
            break;

        case voice::VoiceSessionState::Reviewing:
            if (m_recordAudioBtn) {
                m_recordAudioBtn->setText("🎙 Record Audio");
                m_recordAudioBtn->setStyleSheet(
                    "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
                    "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                    "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
                );
            }
            setVisualState(CoreVisualState::Idle);
            break;

        case voice::VoiceSessionState::Thinking:
            if (m_audioStatusLabel) {
                m_audioStatusLabel->setText("● Generating response...");
                m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextAccentCyan));
            }
            setVisualState(CoreVisualState::Thinking);
            break;

        case voice::VoiceSessionState::Speaking:
            if (m_testTtsBtn) {
                m_testTtsBtn->setText("⏹ Stop Voice");
                m_testTtsBtn->setStyleSheet(
                    "QPushButton { background-color: #0E2938; color: #38BDF8; border: 1px solid #0284C7; "
                    "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: bold; }"
                );
            }
            if (m_audioStatusLabel) {
                m_audioStatusLabel->setText("● Speaking response...");
                m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextAccentCyan));
            }
            setVisualState(CoreVisualState::Responding);
            break;

        case voice::VoiceSessionState::Idle:
            if (m_recordAudioBtn) {
                m_recordAudioBtn->setText("🎙 Record Audio");
                m_recordAudioBtn->setStyleSheet(
                    "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
                    "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                    "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
                );
            }
            if (m_testTtsBtn) {
                m_testTtsBtn->setText("🔊 Test Voice");
                m_testTtsBtn->setStyleSheet(
                    "QPushButton { background-color: #171826; color: #A78BFA; border: 1px solid #2E3650; "
                    "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                    "QPushButton:hover { background-color: #23273D; color: #FFFFFF; }"
                );
            }
            if (m_audioStatusLabel && (!m_tts || !m_tts->isSpeaking())) {
                m_audioStatusLabel->setText("○ Audio: Ready");
                m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextMuted));
            }
            setVisualState(CoreVisualState::Idle);
            break;

        case voice::VoiceSessionState::Error:
            if (m_recordAudioBtn) {
                m_recordAudioBtn->setText("🎙 Record Audio");
                m_recordAudioBtn->setStyleSheet(
                    "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
                    "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
                    "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
                );
            }
            if (m_audioStatusLabel && m_audioStatusLabel->text().contains("Transcribing")) {
                m_audioStatusLabel->setText("○ Audio: Ready");
                m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextMuted));
            }
            setVisualState(CoreVisualState::Error);
            break;
    }
}

void ChatView::onRecordButtonClicked()
{
    if (m_voiceController) {
        if (m_voiceController->state() != voice::VoiceSessionState::Recording) {
            m_voiceController->startRecording();
        } else {
            m_voiceController->stopRecording();
        }
        return;
    }

    if (!m_audioCapture) {
        return;
    }

    if (!m_audioCapture->isRecording()) {
        m_audioCapture->startCapture();
    } else {
        m_audioCapture->stopCapture();
    }
}

void ChatView::onTestTtsButtonClicked()
{
    if (m_voiceController && m_voiceController->state() == voice::VoiceSessionState::Speaking) {
        m_voiceController->stopSpeaking();
        return;
    }

    if (!m_tts) {
        return;
    }

    if (!m_tts->isSpeaking()) {
        m_tts->speak("FLEX 1.0 native audio foundation is operational.", true);
    } else {
        m_tts->stop();
    }
}

void ChatView::onTtsStarted()
{
    if (m_testTtsBtn) {
        m_testTtsBtn->setText("⏹ Stop Voice");
        m_testTtsBtn->setStyleSheet(
            "QPushButton { background-color: #0E2938; color: #38BDF8; border: 1px solid #0284C7; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: bold; }"
        );
    }
    if (m_audioStatusLabel) {
        m_audioStatusLabel->setText("● Speaking test phrase...");
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextAccentCyan));
    }
}

void ChatView::onTtsFinished()
{
    if (m_testTtsBtn) {
        m_testTtsBtn->setText("🔊 Test Voice");
        m_testTtsBtn->setStyleSheet(
            "QPushButton { background-color: #171826; color: #A78BFA; border: 1px solid #2E3650; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background-color: #23273D; color: #FFFFFF; }"
        );
    }
    if (m_audioStatusLabel) {
        m_audioStatusLabel->setText("✓ Speech finished");
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::StatusSuccess));
    }
}

void ChatView::onTtsCancelled()
{
    if (m_testTtsBtn) {
        m_testTtsBtn->setText("🔊 Test Voice");
        m_testTtsBtn->setStyleSheet(
            "QPushButton { background-color: #171826; color: #A78BFA; border: 1px solid #2E3650; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background-color: #23273D; color: #FFFFFF; }"
        );
    }
    if (m_audioStatusLabel) {
        m_audioStatusLabel->setText("○ Speech stopped");
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextMuted));
    }
}

void ChatView::onTtsError(voice::TtsError /*error*/, const QString& message)
{
    if (m_testTtsBtn) {
        m_testTtsBtn->setText("🔊 Test Voice");
        m_testTtsBtn->setStyleSheet(
            "QPushButton { background-color: #171826; color: #A78BFA; border: 1px solid #2E3650; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background-color: #23273D; color: #FFFFFF; }"
        );
    }
    if (m_audioStatusLabel) {
        m_audioStatusLabel->setText(QString("○ TTS Error: %1").arg(message));
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::StatusError));
    }
}

void ChatView::onCaptureStarted()
{
    if (m_recordAudioBtn) {
        m_recordAudioBtn->setText("⏹ Stop Capture");
        m_recordAudioBtn->setStyleSheet(
            "QPushButton { background-color: #3D1418; color: #F87171; border: 1px solid #EF4444; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: bold; }"
        );
    }
    if (m_audioStatusLabel) {
        m_audioStatusLabel->setText("● Recording 16kHz audio...");
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::StatusError));
    }
}

void ChatView::onCaptureFinished(const voice::AudioBuffer& buffer)
{
    if (m_recordAudioBtn) {
        m_recordAudioBtn->setText("🎙 Record Audio");
        m_recordAudioBtn->setStyleSheet(
            "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
        );
    }
    if (m_audioStatusLabel) {
        if (m_audioCapture && m_audioCapture->wasMaxDurationReached()) {
            m_audioStatusLabel->setText(QString("✓ Time limit reached (30s) — %1 samples").arg(buffer.sampleCount()));
        } else {
            double sec = static_cast<double>(buffer.durationMs) / 1000.0;
            m_audioStatusLabel->setText(QString("✓ Captured %1s audio (%2 samples)").arg(sec, 0, 'f', 1).arg(buffer.sampleCount()));
        }
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::StatusSuccess));
    }
}

void ChatView::onCaptureCancelled()
{
    if (m_recordAudioBtn) {
        m_recordAudioBtn->setText("🎙 Record Audio");
        m_recordAudioBtn->setStyleSheet(
            "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
        );
    }
    if (m_audioStatusLabel) {
        m_audioStatusLabel->setText("○ Recording cancelled");
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::TextMuted));
    }
}

void ChatView::onCaptureError(voice::AudioCaptureError /*error*/, const QString& message)
{
    if (m_recordAudioBtn) {
        m_recordAudioBtn->setText("🎙 Record Audio");
        m_recordAudioBtn->setStyleSheet(
            "QPushButton { background-color: #1E1A33; color: #C084FC; border: 1px solid #7C3AED; "
            "border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background-color: #2D244D; color: #FFFFFF; }"
        );
    }
    if (m_audioStatusLabel) {
        m_audioStatusLabel->setText(QString("○ Error: %1").arg(message));
        m_audioStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; margin-left: 4px;").arg(Theme::StatusError));
    }
}

} // namespace flex::ui

