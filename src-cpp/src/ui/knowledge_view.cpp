#include "flex/ui/knowledge_view.hpp"
#include "flex/ui/theme.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>

namespace flex::ui {

KnowledgeView::KnowledgeView(
    std::shared_ptr<BackendCoordinator> coordinator,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinator(std::move(coordinator))
{
    setupUi();
    refreshDocuments();
}

void KnowledgeView::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    // 1. Header & Format Badges
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);

    auto* titleLabel = new QLabel("📚 Knowledge Base Vault", this);
    titleLabel->setStyleSheet(QString("font-size: 18px; font-weight: bold; color: %1;").arg(Theme::TextPrimary));
    titleCol->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel("Local document indexing, C11 chunking, and context retrieval for AI inference.", this);
    subtitleLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextMuted));
    titleCol->addWidget(subtitleLabel);

    headerLayout->addLayout(titleCol);
    headerLayout->addStretch();

    // Stats Label
    m_statsLabel = new QLabel("Docs: 0 | Chunks: 0", this);
    m_statsLabel->setStyleSheet(QString(
        "background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 4px 10px; font-size: 11px; color: %3;"
    ).arg(Theme::BgCard, Theme::Border, Theme::TextAccentCyan));
    headerLayout->addWidget(m_statsLabel);

    // Supported formats badge
    m_formatBadge = new QLabel("Supported: .txt, .md  (PDF deferred)", this);
    m_formatBadge->setStyleSheet(QString(
        "background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 4px 10px; font-size: 11px; color: %3;"
    ).arg(Theme::BgCard, Theme::Border, Theme::StatusWarning));
    headerLayout->addWidget(m_formatBadge);

    mainLayout->addLayout(headerLayout);

    // 2. Action Toolbar & Search
    auto* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(10);

    m_importBtn = new QPushButton("➕  Import Document (.txt, .md)...", this);
    m_importBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; border-radius: 6px; padding: 8px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: %2; }"
    ).arg(Theme::AccentPurple, Theme::AccentPurpleHover));
    connect(m_importBtn, &QPushButton::clicked, this, &KnowledgeView::onImportClicked);
    toolbarLayout->addWidget(m_importBtn);

    m_removeBtn = new QPushButton("🗑️  Remove Selected", this);
    m_removeBtn->setEnabled(false);
    m_removeBtn->setStyleSheet(QString(
        "QPushButton { background-color: #2D1515; color: #FCA5A5; border: 1px solid #7F1D1D; border-radius: 6px; padding: 8px 14px; }"
        "QPushButton:hover { background-color: #451A1A; }"
        "QPushButton:disabled { background-color: #1A1A24; color: #4B5563; border-color: #2E3650; }"
    ));
    connect(m_removeBtn, &QPushButton::clicked, this, &KnowledgeView::onRemoveClicked);
    toolbarLayout->addWidget(m_removeBtn);

    toolbarLayout->addSpacing(16);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("🔍  Search indexed chunks across all documents...");
    m_searchEdit->setStyleSheet(QString(
        "QLineEdit { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 7px 12px; color: %3; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid %4; }"
    ).arg(Theme::BgInput, Theme::Border, Theme::TextPrimary, Theme::BorderFocus));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &KnowledgeView::onSearchChanged);
    toolbarLayout->addWidget(m_searchEdit, 1);

    mainLayout->addLayout(toolbarLayout);

    // 3. Status Alert Strip
    m_statusLabel = new QLabel("Ready. Select or import documents to begin.", this);
    m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextMuted));
    mainLayout->addWidget(m_statusLabel);

    // 4. Two-Pane Splitter (Documents Left, Chunks & Inspection Right)
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setStyleSheet(QString("QSplitter::handle { background-color: %1; width: 2px; }").arg(Theme::Border));

    // Left Pane: Document List
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(8);

    auto* docHeader = new QLabel("Imported Documents", leftWidget);
    docHeader->setStyleSheet(QString("font-weight: bold; font-size: 13px; color: %1;").arg(Theme::TextPrimary));
    leftLayout->addWidget(docHeader);

    m_docListWidget = new QListWidget(leftWidget);
    m_docListWidget->setStyleSheet(QString(
        "QListWidget { background-color: %1; border: 1px solid %2; border-radius: 8px; color: %3; padding: 4px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 4px; margin-bottom: 2px; }"
        "QListWidget::item:selected { background-color: %4; color: %5; }"
        "QListWidget::item:hover:!selected { background-color: %6; }"
    ).arg(Theme::BgPanel, Theme::Border, Theme::TextPrimary, Theme::SelectionBg, Theme::TextPrimary, Theme::BgSidebarHover));
    connect(m_docListWidget, &QListWidget::itemSelectionChanged, this, &KnowledgeView::onDocSelectionChanged);
    leftLayout->addWidget(m_docListWidget, 1);

    splitter->addWidget(leftWidget);

    // Right Pane: Chunks & Inspection
    auto* rightWidget = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto* chunkHeader = new QLabel("Indexed Chunks & Semantic Inspection", rightWidget);
    chunkHeader->setStyleSheet(QString("font-weight: bold; font-size: 13px; color: %1;").arg(Theme::TextPrimary));
    rightLayout->addWidget(chunkHeader);

    m_chunkListWidget = new QListWidget(rightWidget);
    m_chunkListWidget->setStyleSheet(QString(
        "QListWidget { background-color: %1; border: 1px solid %2; border-radius: 8px; color: %3; padding: 4px; }"
        "QListWidget::item { padding: 6px 8px; border-radius: 4px; margin-bottom: 2px; font-family: monospace; font-size: 11px; }"
        "QListWidget::item:selected { background-color: %4; color: %5; }"
        "QListWidget::item:hover:!selected { background-color: %6; }"
    ).arg(Theme::BgPanel, Theme::Border, Theme::TextPrimary, Theme::SelectionBg, Theme::TextPrimary, Theme::BgSidebarHover));
    connect(m_chunkListWidget, &QListWidget::itemSelectionChanged, this, &KnowledgeView::onChunkSelectionChanged);
    rightLayout->addWidget(m_chunkListWidget, 1);

    auto* previewHeader = new QLabel("Chunk Full Text Preview", rightWidget);
    previewHeader->setStyleSheet(QString("font-weight: bold; font-size: 12px; color: %1;").arg(Theme::TextAccentViolet));
    rightLayout->addWidget(previewHeader);

    m_chunkDetailBrowser = new QTextBrowser(rightWidget);
    m_chunkDetailBrowser->setStyleSheet(QString(
        "QTextBrowser { background-color: %1; border: 1px solid %2; border-radius: 8px; color: %3; padding: 10px; font-size: 12px; }"
    ).arg(Theme::BgCard, Theme::Border, Theme::TextPrimary));
    rightLayout->addWidget(m_chunkDetailBrowser, 1);

    splitter->addWidget(rightWidget);

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 6);

    mainLayout->addWidget(splitter, 1);
}

void KnowledgeView::refreshDocuments()
{
    if (!m_coordinator) return;

    m_allDocs = m_coordinator->listDocuments(200);

    int64_t docCount = 0, chunkCount = 0;
    m_coordinator->getKnowledgeStats(&docCount, &chunkCount);
    if (m_statsLabel) {
        m_statsLabel->setText(QString("Docs: %1 | Chunks: %2").arg(docCount).arg(chunkCount));
    }

    if (m_docListWidget) {
        m_docListWidget->blockSignals(true);
        m_docListWidget->clear();

        for (const auto& doc : m_allDocs) {
            double kb = doc.fileSize / 1024.0;
            QString text = QString("📄 %1\n   %2 chunks • %3 KB • %4")
                .arg(QString::fromStdString(doc.filename))
                .arg(doc.chunkCount)
                .arg(QString::number(kb, 'f', 1))
                .arg(QString::fromStdString(doc.createdAt).left(10));

            auto* item = new QListWidgetItem(text, m_docListWidget);
            item->setData(Qt::UserRole, QString::fromStdString(doc.id));
        }

        m_docListWidget->blockSignals(false);
    }

    if (m_allDocs.empty()) {
        if (m_statusLabel) {
            m_statusLabel->setText("No documents imported yet. Click 'Import Document' to add .txt or .md files.");
            m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextMuted));
        }
        if (m_chunkListWidget) m_chunkListWidget->clear();
        if (m_chunkDetailBrowser) m_chunkDetailBrowser->clear();
        if (m_removeBtn) m_removeBtn->setEnabled(false);
    } else {
        if (!m_selectedDocId.isEmpty()) {
            refreshChunks(m_selectedDocId);
        } else if (m_docListWidget && m_docListWidget->count() > 0) {
            m_docListWidget->setCurrentRow(0);
        }
    }
}

void KnowledgeView::refreshChunks(const QString& documentId)
{
    if (!m_coordinator || documentId.isEmpty()) return;

    auto chunks = m_coordinator->listDocumentChunks(documentId.toStdString(), 500);

    if (m_chunkListWidget) {
        m_chunkListWidget->blockSignals(true);
        m_chunkListWidget->clear();

        for (const auto& chk : chunks) {
            QString snippet = QString::fromStdString(chk.content).replace('\n', ' ').trimmed().left(70);
            QString itemText = QString("[Chunk %1] (%2 tokens) %3...")
                .arg(chk.chunkIndex)
                .arg(chk.tokenCount)
                .arg(snippet);

            auto* item = new QListWidgetItem(itemText, m_chunkListWidget);
            item->setData(Qt::UserRole, QString::fromStdString(chk.content));
            item->setData(Qt::UserRole + 1, static_cast<qint64>(chk.chunkIndex));
        }

        m_chunkListWidget->blockSignals(false);
    }

    if (m_chunkListWidget && m_chunkListWidget->count() > 0) {
        m_chunkListWidget->setCurrentRow(0);
    } else if (m_chunkDetailBrowser) {
        m_chunkDetailBrowser->setPlainText("No chunks in this document.");
    }
}

void KnowledgeView::onDocSelectionChanged()
{
    if (!m_docListWidget) return;
    auto items = m_docListWidget->selectedItems();
    if (items.isEmpty()) {
        m_selectedDocId.clear();
        if (m_removeBtn) m_removeBtn->setEnabled(false);
        return;
    }

    m_selectedDocId = items.first()->data(Qt::UserRole).toString();
    if (m_removeBtn) m_removeBtn->setEnabled(true);
    refreshChunks(m_selectedDocId);
}

void KnowledgeView::onChunkSelectionChanged()
{
    if (!m_chunkListWidget || !m_chunkDetailBrowser) return;
    auto items = m_chunkListWidget->selectedItems();
    if (items.isEmpty()) {
        m_chunkDetailBrowser->clear();
        return;
    }

    QString content = items.first()->data(Qt::UserRole).toString();
    m_chunkDetailBrowser->setPlainText(content);
}

void KnowledgeView::onSearchChanged(const QString& text)
{
    if (!m_coordinator) return;

    QString query = text.trimmed();
    if (query.isEmpty()) {
        if (!m_selectedDocId.isEmpty()) {
            refreshChunks(m_selectedDocId);
        } else {
            refreshDocuments();
        }
        return;
    }

    auto results = m_coordinator->searchKnowledgeChunks(query.toStdString(), 30);

    if (m_chunkListWidget) {
        m_chunkListWidget->blockSignals(true);
        m_chunkListWidget->clear();

        for (const auto& chk : results) {
            QString snippet = QString::fromStdString(chk.content).replace('\n', ' ').trimmed().left(60);
            QString itemText = QString("[Score: %1] [%2: #%3] %4...")
                .arg(QString::number(chk.relevanceScore, 'f', 1))
                .arg(QString::fromStdString(chk.documentName))
                .arg(chk.chunkIndex)
                .arg(snippet);

            auto* item = new QListWidgetItem(itemText, m_chunkListWidget);
            item->setData(Qt::UserRole, QString::fromStdString(chk.content));
        }

        m_chunkListWidget->blockSignals(false);
    }

    if (m_statusLabel) {
        m_statusLabel->setText(QString("Found %1 relevant chunk(s) for '%2'.").arg(results.size()).arg(query));
        m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextAccentCyan));
    }

    if (m_chunkListWidget && m_chunkListWidget->count() > 0) {
        m_chunkListWidget->setCurrentRow(0);
    }
}

void KnowledgeView::onImportClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Import Document into Knowledge Base",
        QString(),
        "Supported Documents (*.txt *.md);;Plain Text (*.txt);;Markdown (*.md);;All Files (*.*)"
    );

    if (filePath.isEmpty()) {
        return;
    }

    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    if (ext != "txt" && ext != "md") {
        if (ext == "pdf") {
            QMessageBox::information(
                this,
                "Format Deferred",
                "PDF extraction is currently deferred in FLEX 1.0. Please convert to .txt or .md before importing."
            );
        } else {
            QMessageBox::warning(
                this,
                "Unsupported Format",
                QString("Unsupported file extension '.%1'. Supported formats are .txt and .md.").arg(ext)
            );
        }
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (m_statusLabel) {
            m_statusLabel->setText(QString("✕ Error opening file: %1").arg(file.errorString()));
            m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusError));
        }
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    importDocumentDirect(fi.fileName(), filePath, content);
}

bool KnowledgeView::importDocumentDirect(const QString& filename, const QString& path, const QString& content)
{
    if (!m_coordinator) return false;

    if (m_statusLabel) {
        m_statusLabel->setText("Parsing and chunking document via C11 pipeline...");
        m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextAccentCyan));
    }

    std::string error;
    std::string docId = m_coordinator->importDocument(
        filename.toStdString(),
        path.toStdString(),
        content.toStdString(),
        &error
    );

    if (docId.empty()) {
        if (m_statusLabel) {
            m_statusLabel->setText(QString("✕ Import failed: %1").arg(QString::fromStdString(error)));
            m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusError));
        }
        return false;
    }

    m_selectedDocId = QString::fromStdString(docId);
    refreshDocuments();

    if (!error.empty()) {
        // Warning or duplicate notification
        if (m_statusLabel) {
            m_statusLabel->setText(QString("ℹ %1").arg(QString::fromStdString(error)));
            m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusWarning));
        }
        return false;
    }

    if (m_statusLabel) {
        m_statusLabel->setText(QString("✓ Document '%1' indexed successfully.").arg(filename));
        m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusSuccess));
    }
    return true;
}

void KnowledgeView::onRemoveClicked()
{
    if (!m_coordinator || m_selectedDocId.isEmpty()) return;

    bool deleted = m_coordinator->deleteDocument(m_selectedDocId.toStdString());
    if (deleted) {
        m_selectedDocId.clear();
        refreshDocuments();
        if (m_statusLabel) {
            m_statusLabel->setText("✓ Document and associated chunks removed from Knowledge Base.");
            m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusSuccess));
        }
    } else {
        if (m_statusLabel) {
            m_statusLabel->setText("✕ Failed to remove document from database.");
            m_statusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusError));
        }
    }
}

} // namespace flex::ui

