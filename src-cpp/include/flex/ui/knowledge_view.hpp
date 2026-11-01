#ifndef FLEX_UI_KNOWLEDGE_VIEW_HPP
#define FLEX_UI_KNOWLEDGE_VIEW_HPP

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <memory>
#include "flex/backend.hpp"

namespace flex::ui {

/**
 * Knowledge Base Desktop View for FLEX 1.0.
 *
 * Implements:
 * - Document file ingestion (.txt, .md) with C-core text validation and chunking.
 * - Local SQLite document & chunk management.
 * - Semantic chunk search and relevance inspection.
 * - Non-blocking asynchronous coordination with backend.
 */
class KnowledgeView : public QWidget {
    Q_OBJECT

public:
    explicit KnowledgeView(
        std::shared_ptr<BackendCoordinator> coordinator,
        QWidget* parent = nullptr);
    ~KnowledgeView() override = default;

    void refreshDocuments();
    void refreshChunks(const QString& documentId);

    // Test accessors
    [[nodiscard]] QPushButton* importButton() const { return m_importBtn; }
    [[nodiscard]] QPushButton* removeButton() const { return m_removeBtn; }
    [[nodiscard]] QListWidget* documentListWidget() const { return m_docListWidget; }
    [[nodiscard]] QListWidget* chunkListWidget() const { return m_chunkListWidget; }
    [[nodiscard]] QLineEdit* searchEdit() const { return m_searchEdit; }
    [[nodiscard]] QLabel* statusLabel() const { return m_statusLabel; }

    // Direct import helper for testing
    bool importDocumentDirect(const QString& filename, const QString& path, const QString& content);

private slots:
    void onImportClicked();
    void onRemoveClicked();
    void onDocSelectionChanged();
    void onSearchChanged(const QString& text);
    void onChunkSelectionChanged();

private:
    void setupUi();
    void updateDetailPane(const QString& docId);

    std::shared_ptr<BackendCoordinator> m_coordinator;
    std::vector<DocumentRecord> m_allDocs;
    QString m_selectedDocId;

    // Controls
    QPushButton* m_importBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QLineEdit* m_searchEdit{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_statsLabel{nullptr};
    QLabel* m_formatBadge{nullptr};

    QListWidget* m_docListWidget{nullptr};
    QListWidget* m_chunkListWidget{nullptr};
    QTextBrowser* m_chunkDetailBrowser{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_KNOWLEDGE_VIEW_HPP

