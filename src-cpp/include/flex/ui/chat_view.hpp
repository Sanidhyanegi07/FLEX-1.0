#ifndef FLEX_UI_CHAT_VIEW_HPP
#define FLEX_UI_CHAT_VIEW_HPP

#include "flex/storage.hpp"
#include "flex/ui/ai_core_widget.hpp"
#include "flex/ui/message_bubble.hpp"

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

namespace flex::ui {

/**
 * Custom multi-line text input widget.
 * Submits on Enter, inserts a newline on Shift+Enter or Ctrl+Enter.
 */
class ChatInputEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit ChatInputEdit(QWidget* parent = nullptr);
    ~ChatInputEdit() override = default;

signals:
    void submitRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

/**
 * Main chat conversation workspace for FLEX 1.0.
 *
 * Responsibilities:
 * - Displays active conversation messages with role badges, timestamps, and model tags.
 * - Central AI Core visualization (Hero mode when empty, compact HUD when conversing).
 * - Multi-line prompt input deck with Enter-to-send and Shift+Enter for newlines.
 * - Voice-ready modular layout (microphone expansion container).
 * - Real-time streaming token assembly and auto-scrolling.
 * - Loading indicator and dismissible error banner.
 */
class ChatView : public QWidget {
    Q_OBJECT

public:
    explicit ChatView(QWidget* parent = nullptr);
    ~ChatView() override = default;

    void setConversation(
        const QString& id,
        const QString& title,
        const std::vector<MessageRecord>& messages);

    void clearConversation();
    void appendUserMessage(const QString& text, const QString& timestamp = QString());
    void startAssistantMessage(const QString& model = QString(), const QString& timestamp = QString());
    void appendAssistantChunk(const QString& chunk);
    void finishAssistantMessage(const QString& fullText = QString());

    void setVisualState(CoreVisualState state);
    void setInputEnabled(bool enabled);
    void showErrorBanner(const QString& errorMessage);
    void hideErrorBanner();

    [[nodiscard]] QString conversationId() const { return m_conversationId; }
    [[nodiscard]] int messageCount() const { return static_cast<int>(m_bubbles.size()); }
    [[nodiscard]] QString inputText() const;
    void setInputText(const QString& text);
    void focusInput();

signals:
    void sendRequested(const QString& text);
    void stopRequested();

private slots:
    void onSendClicked();
    void onInputTextChanged();

private:
    void setupUi();
    void scrollToBottom();
    void updateEmptyState();

    QString m_conversationId;
    QString m_conversationTitle;

    // Header widgets
    QLabel* m_titleLabel{nullptr};
    AICoreWidget* m_compactCore{nullptr};

    // Scroll & Message list widgets
    QScrollArea* m_scrollArea{nullptr};
    QWidget* m_messagesContainer{nullptr};
    QVBoxLayout* m_messagesLayout{nullptr};
    AICoreWidget* m_heroCore{nullptr};
    QWidget* m_emptyHeroWidget{nullptr};

    // Status & Error banners
    QFrame* m_errorBanner{nullptr};
    QLabel* m_errorTextLabel{nullptr};
    QFrame* m_loadingBanner{nullptr};
    QLabel* m_loadingLabel{nullptr};

    // Input Deck
    ChatInputEdit* m_inputEdit{nullptr};
    QPushButton* m_sendButton{nullptr};
    QHBoxLayout* m_inputControlsLayout{nullptr}; // Slot for future voice mic button

    // Active streaming message
    MessageBubble* m_currentAssistantBubble{nullptr};
    std::vector<MessageBubble*> m_bubbles;
};

} // namespace flex::ui

#endif // FLEX_UI_CHAT_VIEW_HPP

