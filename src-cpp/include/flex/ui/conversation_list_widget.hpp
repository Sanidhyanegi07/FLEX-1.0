#ifndef FLEX_UI_CONVERSATION_LIST_WIDGET_HPP
#define FLEX_UI_CONVERSATION_LIST_WIDGET_HPP

#include "flex/storage.hpp"

#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

namespace flex::ui {

/**
 * Sidebar widget displaying the list of persistent conversations stored in SQLite.
 * Provides search filtering, active selection tracking, and deletion.
 */
class ConversationListWidget : public QWidget {
    Q_OBJECT

public:
    explicit ConversationListWidget(QWidget* parent = nullptr);
    ~ConversationListWidget() override = default;

    void setConversations(const std::vector<ConversationRecord>& conversations);
    void selectConversation(const QString& conversationId);
    [[nodiscard]] QString currentConversationId() const;
    [[nodiscard]] int conversationCount() const;
    void clearList();

signals:
    void conversationSelected(const QString& conversationId);
    void conversationDeleteRequested(const QString& conversationId);

private slots:
    void onItemClicked(QListWidgetItem* item);
    void onFilterTextChanged(const QString& filterText);
    void onCustomContextMenuRequested(const QPoint& pos);

private:
    void rebuildListItems();

    QLineEdit* m_searchBox{nullptr};
    QListWidget* m_listWidget{nullptr};

    std::vector<ConversationRecord> m_allConversations;
    QString m_selectedCid;
    QString m_currentFilter;
};

} // namespace flex::ui

#endif // FLEX_UI_CONVERSATION_LIST_WIDGET_HPP

