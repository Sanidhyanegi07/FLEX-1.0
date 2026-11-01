#ifndef FLEX_UI_MEMORY_VIEW_HPP
#define FLEX_UI_MEMORY_VIEW_HPP

#include "flex/backend.hpp"

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QWidget>

#include <memory>
#include <vector>

namespace flex::ui {

/**
 * Functional Local Conversation & Fact Memory View for FLEX 1.0.
 *
 * Connected directly to the SQLite native storage engine:
 * - Persistent memory facts (CRUD): key, content, category, timestamp.
 * - Semantic and ranked search across long-term user memories.
 * - Real-time search query filtering across conversation titles and IDs.
 * - Full message history inspection transcript.
 * - Recall button to resume any saved conversation in ChatView.
 * - Safe deletion.
 */
class MemoryView : public QWidget {
    Q_OBJECT

public:
    explicit MemoryView(
        std::shared_ptr<BackendCoordinator> coordinator,
        QWidget* parent = nullptr);

    ~MemoryView() override = default;

    void refreshConversations();
    void refreshMemories();
    void refreshAll();

    [[nodiscard]] QListWidget* factListWidget() const noexcept { return m_factListWidget; }
    [[nodiscard]] QLineEdit* keyEdit() const noexcept { return m_keyEdit; }
    [[nodiscard]] QTextEdit* contentEdit() const noexcept { return m_contentEdit; }
    [[nodiscard]] QPushButton* saveFactButton() const noexcept { return m_saveFactBtn; }
    [[nodiscard]] QPushButton* deleteFactButton() const noexcept { return m_deleteFactBtn; }
    [[nodiscard]] QLabel* factStatusLabel() const noexcept { return m_factStatusLabel; }

signals:
    void resumeConversationRequested(const QString& conversationId);

private slots:
    void onSearchChanged(const QString& text);
    void onItemSelectionChanged();
    void onResumeClicked();
    void onDeleteClicked();

    // Fact Memory Slots
    void onFactSearchChanged(const QString& text);
    void onFactSelectionChanged();
    void onAddFactClicked();
    void onSaveFactClicked();
    void onDeleteFactClicked();

private:
    void setupUi();
    void updateDetailPane(const QString& cid);
    void updateFactDetailPane(const QString& fid);

    std::shared_ptr<BackendCoordinator> m_coordinator;

    QTabWidget* m_tabWidget{nullptr};

    // Tab 1: Memory Facts
    QLineEdit* m_factSearchBox{nullptr};
    QListWidget* m_factListWidget{nullptr};
    QLineEdit* m_keyEdit{nullptr};
    QTextEdit* m_contentEdit{nullptr};
    QComboBox* m_categoryCombo{nullptr};
    QLabel* m_factMetaLabel{nullptr};
    QLabel* m_factStatusLabel{nullptr};
    QPushButton* m_addFactBtn{nullptr};
    QPushButton* m_saveFactBtn{nullptr};
    QPushButton* m_deleteFactBtn{nullptr};

    std::vector<MemoryEntry> m_allMemories;
    QString m_selectedMemoryId;

    // Tab 2: Conversation Transcripts
    QLineEdit* m_searchBox{nullptr};
    QListWidget* m_listWidget{nullptr};
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
