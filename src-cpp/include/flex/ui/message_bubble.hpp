#ifndef FLEX_UI_MESSAGE_BUBBLE_HPP
#define FLEX_UI_MESSAGE_BUBBLE_HPP

#include <QFrame>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace flex::ui {

/**
 * Visual bubble widget for a single conversation message turn.
 * Supports distinct styling for user, assistant, and error/system messages,
 * as well as real-time chunk appending during streaming responses.
 */
class MessageBubble : public QFrame {
    Q_OBJECT

public:
    explicit MessageBubble(
        const QString& role,
        const QString& content,
        const QString& timestamp = QString(),
        const QString& model = QString(),
        QWidget* parent = nullptr);

    ~MessageBubble() override = default;

    [[nodiscard]] QString role() const { return m_role; }
    [[nodiscard]] QString content() const { return m_content; }
    [[nodiscard]] QString timestamp() const { return m_timestamp; }

    void appendChunk(const QString& chunk);
    void setContent(const QString& fullContent);

private:
    void setupUi();
    void updateContentDisplay();

    QString m_role;
    QString m_content;
    QString m_timestamp;
    QString m_model;

    QLabel* m_roleBadge{nullptr};
    QLabel* m_metaLabel{nullptr};
    QTextBrowser* m_contentBrowser{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_MESSAGE_BUBBLE_HPP

