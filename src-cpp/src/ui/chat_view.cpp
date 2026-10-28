#include "flex/ui/chat_view.hpp"
#include "flex/ui/theme.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTimer>

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
    heroLayout->setContentsMargins(10, 40, 10, 40);
    heroLayout->setSpacing(12);
    heroLayout->setAlignment(Qt::AlignCenter);

    m_heroCore = new AICoreWidget(m_emptyHeroWidget, false);
    heroLayout->addWidget(m_heroCore, 0, Qt::AlignCenter);

    auto* heroDesc = new QLabel(
        "FLEX 1.0 is ready. Type a prompt below to begin your conversation.",
        m_emptyHeroWidget
    );
    heroDesc->setStyleSheet("color: #94A3B8; font-size: 13px;");
    heroDesc->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroDesc);

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

    // Voice-Ready Expansion Slot:
    // Future microphone widget/button will be added to m_inputControlsLayout seamlessly
    // without altering the chat interface or submission flow.
    m_inputControlsLayout = new QHBoxLayout();
    m_inputControlsLayout->setContentsMargins(0, 0, 0, 0);
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

} // namespace flex::ui

