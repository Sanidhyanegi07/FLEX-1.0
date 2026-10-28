#ifndef FLEX_UI_MEMORY_VIEW_HPP
#define FLEX_UI_MEMORY_VIEW_HPP

#include "flex/backend.hpp"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QWidget>

#include <memory>
#include <vector>

namespace flex::ui {

/**
 * Functional Local Conversation-Memory View for FLEX 1.0.
 *
 * Connected directly to the SQLite native storage engine:
 * - Real-time search query filtering across conversation titles and IDs.
 * - Conversation metadata inspection (turn counts, creation timestamps).
 * - Full message history inspection transcript.
 * - Recall button to resume any saved conversation in ChatView.
 * - Safe cascade deletion.
 */
class MemoryView : public QWidget {
    Q_OBJECT

public:
    explicit MemoryView(
        std::shared_ptr<BackendCoordinator> coordinator,
        QWidget* parent = nullptr);

    ~MemoryView() override = default;

    void refreshConversations();

signals:
    void resumeConversationRequested(const QString& conversationId);

private slots:
    void onSearchChanged(const QString& text);
    void onItemSelectionChanged();
    void onResumeClicked();
    void onDeleteClicked();

private:
    void setupUi();
    void updateDetailPane(const QString& cid);

    std::shared_ptr<BackendCoordinator> m_coordinator;

    QLineEdit* m_searchBox{nullptr};
    QListWidget* m_listWidget{nullptr};

    // Detail Pane
    QLabel* m_detailTitleLabel{nullptr};
    QLabel* m_detailMetaLabel{nullptr};
    QTextBrowser* m_messagesBrowser{nullptr};
    QPushButton* m_resumeBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};

    std::vector<ConversationRecord> m_allRecords;
    QString m_selectedCid;
};

} // namespace flex::ui

#endif // FLEX_UI_MEMORY_VIEW_HPP

