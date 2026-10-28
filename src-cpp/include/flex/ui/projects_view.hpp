#ifndef FLEX_UI_PROJECTS_VIEW_HPP
#define FLEX_UI_PROJECTS_VIEW_HPP

#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeView>
#include <QWidget>

namespace flex::ui {

/**
 * Projects & Files Workspace View for FLEX 1.0.
 *
 * Provides a secure local workspace explorer:
 * - Dynamically detects current repository root (without hardcoded paths).
 * - "Browse Folder..." dialog for workspace selection.
 * - Hierarchical file tree view (QFileSystemModel).
 * - Safe read-only file preview with encoding and binary checks.
 * - "Insert into Chat Prompt" action.
 */
class ProjectsView : public QWidget {
    Q_OBJECT

public:
    explicit ProjectsView(QWidget* parent = nullptr);
    ~ProjectsView() override = default;

    void setRootPath(const QString& path);
    [[nodiscard]] QString currentRootPath() const { return m_rootPath; }

signals:
    void attachFileToChatRequested(const QString& filePath, const QString& content);

private slots:
    void onBrowseClicked();
    void onFileSelected(const QModelIndex& index);
    void onAttachClicked();

private:
    void setupUi();
    static QString detectInitialWorkspaceRoot();

    QString m_rootPath;
    QString m_selectedFilePath;

    QLineEdit* m_pathEdit{nullptr};
    QPushButton* m_browseBtn{nullptr};

    QTreeView* m_treeView{nullptr};
    QFileSystemModel* m_fsModel{nullptr};

    // Preview Pane
    QLabel* m_previewHeader{nullptr};
    QLabel* m_previewMeta{nullptr};
    QPlainTextEdit* m_previewEdit{nullptr};
    QPushButton* m_attachBtn{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_PROJECTS_VIEW_HPP

