#include "flex/ui/projects_view.hpp"
#include "flex/ui/theme.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QSplitter>
#include <QVBoxLayout>

namespace flex::ui {

ProjectsView::ProjectsView(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    setRootPath(detectInitialWorkspaceRoot());
}

QString ProjectsView::detectInitialWorkspaceRoot()
{
    // Search current path or application path upwards for .git or CMakeLists.txt
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 5; ++i) {
        if (dir.exists(".git") || dir.exists("CMakeLists.txt")) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    // Fallback: application directory
    QDir appDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        if (appDir.exists(".git") || appDir.exists("CMakeLists.txt")) {
            return appDir.absolutePath();
        }
        if (!appDir.cdUp()) {
            break;
        }
    }

    return QDir::currentPath();
}

void ProjectsView::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(14);

    // Header & Workspace Picker
    auto* headerBox = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(headerBox);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto* titleLbl = new QLabel("📁 Projects & Files Workspace Explorer", headerBox);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: 700; color: #F8FAFC;");

    auto* subLbl = new QLabel("Inspect local source code, documentation, and files for AI context ingestion.", headerBox);
    subLbl->setStyleSheet("font-size: 12px; color: #94A3B8;");

    headerLayout->addWidget(titleLbl);
    headerLayout->addWidget(subLbl);
    mainLayout->addWidget(headerBox);

    // Path row
    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setStyleSheet("font-family: 'Consolas', monospace; font-size: 12px;");
    pathRow->addWidget(m_pathEdit, 1);

    m_browseBtn = new QPushButton("Browse...", this);
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, &ProjectsView::onBrowseClicked);
    pathRow->addWidget(m_browseBtn);

    mainLayout->addLayout(pathRow);

    // Splitter: TreeView on Left (300px), Preview on Right
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);

    // Left Pane: File Tree
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    m_treeView = new QTreeView(splitter);
    m_treeView->setModel(m_fsModel);
    m_treeView->setColumnHidden(1, true); // Hide Size
    m_treeView->setColumnHidden(2, true); // Hide Type
    m_treeView->setColumnHidden(3, true); // Hide Date
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    connect(m_treeView, &QTreeView::clicked, this, &ProjectsView::onFileSelected);

    splitter->addWidget(m_treeView);

    // Right Pane: Preview Frame
    auto* previewFrame = new QFrame(splitter);
    previewFrame->setObjectName("UserProfileCard");
    auto* previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(16, 16, 16, 16);
    previewLayout->setSpacing(8);

    m_previewHeader = new QLabel("Select a file from the explorer to preview", previewFrame);
    m_previewHeader->setStyleSheet("font-size: 14px; font-weight: 700; color: #F8FAFC;");
    m_previewHeader->setWordWrap(true);
    previewLayout->addWidget(m_previewHeader);

    m_previewMeta = new QLabel("Size: 0 bytes | Lines: 0", previewFrame);
    m_previewMeta->setStyleSheet("font-family: 'Consolas', monospace; font-size: 11px; color: #64748B;");
    previewLayout->addWidget(m_previewMeta);

    m_previewEdit = new QPlainTextEdit(previewFrame);
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setStyleSheet(
        QString("QPlainTextEdit { background-color: %1; border: 1px solid %2; border-radius: 6px; font-family: 'Consolas', monospace; font-size: 12px; }")
        .arg(Theme::BgRoot, Theme::Border)
    );
    previewLayout->addWidget(m_previewEdit, 1);

    m_attachBtn = new QPushButton("📎 Insert File Content into Chat Input", previewFrame);
    m_attachBtn->setObjectName("PrimaryButton");
    m_attachBtn->setCursor(Qt::PointingHandCursor);
    m_attachBtn->setEnabled(false);
    connect(m_attachBtn, &QPushButton::clicked, this, &ProjectsView::onAttachClicked);
    previewLayout->addWidget(m_attachBtn);

    splitter->addWidget(previewFrame);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 280, 560 });

    mainLayout->addWidget(splitter, 1);
}

void ProjectsView::setRootPath(const QString& path)
{
    m_rootPath = path;
    m_pathEdit->setText(path);

    QModelIndex rootIdx = m_fsModel->setRootPath(path);
    m_treeView->setRootIndex(rootIdx);
}

void ProjectsView::onBrowseClicked()
{
    QString selected = QFileDialog::getExistingDirectory(
        this,
        "Select Workspace Root Directory",
        m_rootPath.isEmpty() ? QDir::currentPath() : m_rootPath
    );

    if (!selected.isEmpty()) {
        setRootPath(selected);
    }
}

void ProjectsView::onFileSelected(const QModelIndex& index)
{
    QString filePath = m_fsModel->filePath(index);
    QFileInfo fi(filePath);

    if (fi.isDir()) {
        m_selectedFilePath.clear();
        m_previewHeader->setText(fi.fileName() + " (Directory)");
        m_previewMeta->setText("Path: " + filePath);
        m_previewEdit->clear();
        m_attachBtn->setEnabled(false);
        return;
    }

    m_selectedFilePath = filePath;
    m_previewHeader->setText(fi.fileName());

    if (fi.size() > 1024 * 1024) { // Larger than 1 MB
        m_previewMeta->setText(QString("Size: %1 KB (File too large for live preview)").arg(fi.size() / 1024));
        m_previewEdit->setPlainText("[File exceeds 1 MB limit for inline preview]");
        m_attachBtn->setEnabled(false);
        return;
    }

    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(file.readAll());
        int lines = content.count('\n') + 1;
        m_previewMeta->setText(QString("Size: %1 bytes | Lines: %2 | Path: %3")
            .arg(fi.size()).arg(lines).arg(filePath));
        m_previewEdit->setPlainText(content);
        m_attachBtn->setEnabled(true);
    } else {
        m_previewMeta->setText("Cannot open file (binary or permission denied)");
        m_previewEdit->setPlainText("[Binary or unreadable file format]");
        m_attachBtn->setEnabled(false);
    }
}

void ProjectsView::onAttachClicked()
{
    if (m_selectedFilePath.isEmpty() || m_previewEdit->toPlainText().isEmpty()) {
        return;
    }
    emit attachFileToChatRequested(m_selectedFilePath, m_previewEdit->toPlainText());
}

} // namespace flex::ui

