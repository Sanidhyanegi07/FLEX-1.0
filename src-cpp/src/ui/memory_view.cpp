#include "flex/ui/memory_view.hpp"
#include "flex/ui/theme.hpp"

#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace flex::ui {

MemoryView::MemoryView(
    std::shared_ptr<BackendCoordinator> coordinator,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinator(std::move(coordinator))
{
    setupUi();
    refreshAll();
}

void MemoryView::refreshAll()
{
    refreshMemories();
    refreshConversations();
}

void MemoryView::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(14);

    // Header
    auto* headerBox = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(headerBox);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto* titleLbl = new QLabel("🧠 Long-Term Memory & Conversation Vault", headerBox);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: 700; color: #F8FAFC;");

    auto* subLbl = new QLabel("Native SQLite persistent storage. Manage user preferences, facts, and conversation transcripts.", headerBox);
    subLbl->setStyleSheet("font-size: 12px; color: #94A3B8;");

    headerLayout->addWidget(titleLbl);
    headerLayout->addWidget(subLbl);
    mainLayout->addWidget(headerBox);

    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget, 1);

    // =========================================================================
    // Tab 1: Long-term Memory Facts
    // =========================================================================
    auto* factsTab = new QWidget(m_tabWidget);
    auto* factsLayout = new QVBoxLayout(factsTab);
    factsLayout->setContentsMargins(8, 12, 8, 8);

    auto* factSplitter = new QSplitter(Qt::Horizontal, factsTab);
    factSplitter->setHandleWidth(1);

    // Left List Container
    auto* factLeftContainer = new QWidget(factSplitter);
    auto* factLeftLayout = new QVBoxLayout(factLeftContainer);
    factLeftLayout->setContentsMargins(0, 0, 8, 0);
    factLeftLayout->setSpacing(8);

    auto* factSearchRow = new QHBoxLayout();
    m_factSearchBox = new QLineEdit(factLeftContainer);
    m_factSearchBox->setPlaceholderText("Search long-term memory facts...");
    connect(m_factSearchBox, &QLineEdit::textChanged, this, &MemoryView::onFactSearchChanged);

    m_addFactBtn = new QPushButton("+ New Fact", factLeftContainer);
    m_addFactBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addFactBtn, &QPushButton::clicked, this, &MemoryView::onAddFactClicked);

    factSearchRow->addWidget(m_factSearchBox, 1);
    factSearchRow->addWidget(m_addFactBtn);
    factLeftLayout->addLayout(factSearchRow);

    m_factListWidget = new QListWidget(factLeftContainer);
    connect(m_factListWidget, &QListWidget::itemSelectionChanged, this, &MemoryView::onFactSelectionChanged);
    factLeftLayout->addWidget(m_factListWidget, 1);

    factSplitter->addWidget(factLeftContainer);

    // Right Detail / Editor Container
    auto* factRightContainer = new QFrame(factSplitter);
    factRightContainer->setObjectName("UserProfileCard");
    auto* factRightLayout = new QVBoxLayout(factRightContainer);
    factRightLayout->setContentsMargins(16, 16, 16, 16);
    factRightLayout->setSpacing(12);

    auto* factDetailTitle = new QLabel("Memory Fact Details", factRightContainer);
    factDetailTitle->setStyleSheet("font-size: 15px; font-weight: 700; color: #F8FAFC;");
    factRightLayout->addWidget(factDetailTitle);

    auto* factForm = new QFormLayout();
    factForm->setSpacing(10);

    m_keyEdit = new QLineEdit(factRightContainer);
    m_keyEdit->setPlaceholderText("e.g. User Language Preference");
    factForm->addRow("Topic / Key:", m_keyEdit);

    m_categoryCombo = new QComboBox(factRightContainer);
    m_categoryCombo->addItem("preference");
    m_categoryCombo->addItem("fact");
    m_categoryCombo->addItem("instruction");
    m_categoryCombo->addItem("project");
    m_categoryCombo->addItem("general");
    factForm->addRow("Category:", m_categoryCombo);

    m_contentEdit = new QTextEdit(factRightContainer);
    m_contentEdit->setPlaceholderText("Enter the fact or instruction to remember across conversations...");
    m_contentEdit->setStyleSheet(
        QString("QTextEdit { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px; color: #F8FAFC; }")
        .arg(Theme::BgRoot, Theme::Border)
    );
    factForm->addRow("Fact Content:", m_contentEdit);

    m_factMetaLabel = new QLabel("ID: None | Created: None", factRightContainer);
    m_factMetaLabel->setStyleSheet("font-family: 'Consolas', monospace; font-size: 11px; color: #64748B;");
    factForm->addRow("Metadata:", m_factMetaLabel);

    factRightLayout->addLayout(factForm, 1);

    m_factStatusLabel = new QLabel("Ready", factRightContainer);
    m_factStatusLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
    factRightLayout->addWidget(m_factStatusLabel);

    auto* factBtnRow = new QHBoxLayout();
    m_saveFactBtn = new QPushButton("💾 Save Fact", factRightContainer);
    m_saveFactBtn->setObjectName("PrimaryButton");
    m_saveFactBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveFactBtn, &QPushButton::clicked, this, &MemoryView::onSaveFactClicked);

    m_deleteFactBtn = new QPushButton("🗑 Delete Fact", factRightContainer);
    m_deleteFactBtn->setCursor(Qt::PointingHandCursor);
    connect(m_deleteFactBtn, &QPushButton::clicked, this, &MemoryView::onDeleteFactClicked);

    factBtnRow->addWidget(m_saveFactBtn);
    factBtnRow->addWidget(m_deleteFactBtn);
    factBtnRow->addStretch();
    factRightLayout->addLayout(factBtnRow);

    factSplitter->addWidget(factRightContainer);
    factSplitter->setSizes({320, 500});
    factsLayout->addWidget(factSplitter);

    m_tabWidget->addTab(factsTab, "🧠 Persistent Facts");

    // =========================================================================
    // Tab 2: Conversation Transcripts (Existing Vault)
    // =========================================================================
    auto* convTab = new QWidget(m_tabWidget);
    auto* convLayout = new QVBoxLayout(convTab);
    convLayout->setContentsMargins(8, 12, 8, 8);

    auto* splitter = new QSplitter(Qt::Horizontal, convTab);
    splitter->setHandleWidth(1);

    // Left List Container
    auto* leftContainer = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(8);

    m_searchBox = new QLineEdit(leftContainer);
    m_searchBox->setPlaceholderText("Search conversation history...");
    connect(m_searchBox, &QLineEdit::textChanged, this, &MemoryView::onSearchChanged);
    leftLayout->addWidget(m_searchBox);

    m_listWidget = new QListWidget(leftContainer);
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, &MemoryView::onItemSelectionChanged);
    leftLayout->addWidget(m_listWidget, 1);

    splitter->addWidget(leftContainer);

    // Right Detail Container
    auto* rightContainer = new QFrame(splitter);
    rightContainer->setObjectName("UserProfileCard");
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(16, 16, 16, 16);
    rightLayout->setSpacing(10);

    m_detailTitleLabel = new QLabel("Select a conversation to inspect transcript", rightContainer);
    m_detailTitleLabel->setStyleSheet("font-size: 15px; font-weight: 700; color: #F8FAFC;");
    m_detailTitleLabel->setWordWrap(true);
    rightLayout->addWidget(m_detailTitleLabel);

    m_detailMetaLabel = new QLabel("CID: None | Messages: 0", rightContainer);
    m_detailMetaLabel->setStyleSheet("font-family: 'Consolas', monospace; font-size: 11px; color: #64748B;");
    rightLayout->addWidget(m_detailMetaLabel);

    m_messagesBrowser = new QTextBrowser(rightContainer);
    m_messagesBrowser->setStyleSheet(
        QString("QTextBrowser { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 10px; color: #F8FAFC; }")
        .arg(Theme::BgRoot, Theme::Border)
    );
    rightLayout->addWidget(m_messagesBrowser, 1);

    auto* btnRow = new QHBoxLayout();
    m_resumeBtn = new QPushButton("💬 Open in Chat Workspace", rightContainer);
    m_resumeBtn->setObjectName("PrimaryButton");
    m_resumeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resumeBtn, &QPushButton::clicked, this, &MemoryView::onResumeClicked);

    m_deleteBtn = new QPushButton("🗑 Purge Memory", rightContainer);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MemoryView::onDeleteClicked);

    btnRow->addWidget(m_resumeBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch();
    rightLayout->addLayout(btnRow);

    splitter->addWidget(rightContainer);
    splitter->setSizes({320, 500});
    convLayout->addWidget(splitter);

    m_tabWidget->addTab(convTab, "📜 Chat Transcripts");
}

void MemoryView::refreshMemories()
{
    if (!m_coordinator) return;

    m_allMemories = m_coordinator->listMemoryEntries(100);

    if (m_factListWidget) {
        m_factListWidget->blockSignals(true);
        m_factListWidget->clear();
        for (const auto& mem : m_allMemories) {
            QString itemText = QString("[%1] %2 — %3")
                .arg(QString::fromStdString(mem.category))
                .arg(QString::fromStdString(mem.key))
                .arg(QString::fromStdString(mem.content).left(40));
            auto* item = new QListWidgetItem(itemText, m_factListWidget);
            item->setData(Qt::UserRole, QString::fromStdString(mem.id));
        }
        m_factListWidget->blockSignals(false);
    }

    if (!m_selectedMemoryId.isEmpty()) {
        updateFactDetailPane(m_selectedMemoryId);
    }
}

void MemoryView::onFactSearchChanged(const QString& text)
{
    if (!m_coordinator) return;

    std::string query = text.trimmed().toStdString();
    if (query.empty()) {
        refreshMemories();
        return;
    }

    auto results = m_coordinator->searchMemoryEntries(query, 50);

    if (m_factListWidget) {
        m_factListWidget->blockSignals(true);
        m_factListWidget->clear();
        for (const auto& mem : results) {
            QString itemText = QString("[%1] %2 — %3")
                .arg(QString::fromStdString(mem.category))
                .arg(QString::fromStdString(mem.key))
                .arg(QString::fromStdString(mem.content).left(40));
            auto* item = new QListWidgetItem(itemText, m_factListWidget);
            item->setData(Qt::UserRole, QString::fromStdString(mem.id));
        }
        m_factListWidget->blockSignals(false);
    }
}

void MemoryView::onFactSelectionChanged()
{
    if (!m_factListWidget) return;
    auto items = m_factListWidget->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    QString fid = items.first()->data(Qt::UserRole).toString();
    updateFactDetailPane(fid);
}

void MemoryView::updateFactDetailPane(const QString& fid)
{
    m_selectedMemoryId = fid;
    if (!m_coordinator) return;

    auto opt = m_coordinator->getMemoryEntry(fid.toStdString());
    if (opt.has_value()) {
        if (m_keyEdit) m_keyEdit->setText(QString::fromStdString(opt->key));
        if (m_contentEdit) m_contentEdit->setPlainText(QString::fromStdString(opt->content));
        if (m_categoryCombo) {
            int idx = m_categoryCombo->findText(QString::fromStdString(opt->category));
            if (idx >= 0) m_categoryCombo->setCurrentIndex(idx);
        }
        if (m_factMetaLabel) {
            m_factMetaLabel->setText(QString("ID: %1 | Updated: %2")
                .arg(QString::fromStdString(opt->id).left(8))
                .arg(QString::fromStdString(opt->updatedAt)));
        }
        if (m_factStatusLabel) {
            m_factStatusLabel->setText("Editing saved memory fact.");
            m_factStatusLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
        }
    }
}

void MemoryView::onAddFactClicked()
{
    m_selectedMemoryId.clear();
    if (m_factListWidget) m_factListWidget->clearSelection();
    if (m_keyEdit) m_keyEdit->clear();
    if (m_contentEdit) m_contentEdit->clear();
    if (m_categoryCombo) m_categoryCombo->setCurrentIndex(0);
    if (m_factMetaLabel) m_factMetaLabel->setText("ID: New (Auto-generated) | Ready to save");
    if (m_factStatusLabel) {
        m_factStatusLabel->setText("Enter key and content, then click 'Save Fact'.");
        m_factStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusWarning));
    }
    if (m_keyEdit) m_keyEdit->setFocus();
}

void MemoryView::onSaveFactClicked()
{
    if (!m_coordinator) return;

    QString key = m_keyEdit ? m_keyEdit->text().trimmed() : QString();
    QString content = m_contentEdit ? m_contentEdit->toPlainText().trimmed() : QString();
    QString category = m_categoryCombo ? m_categoryCombo->currentText() : "general";

    if (key.isEmpty() || content.isEmpty()) {
        if (m_factStatusLabel) {
            m_factStatusLabel->setText("✕ Both Key and Content must be non-empty.");
            m_factStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusError));
        }
        return;
    }

    try {
        if (m_selectedMemoryId.isEmpty()) {
            std::string newId = m_coordinator->saveMemoryEntry(key.toStdString(), content.toStdString(), category.toStdString());
            m_selectedMemoryId = QString::fromStdString(newId);
        } else {
            m_coordinator->updateMemoryEntry(m_selectedMemoryId.toStdString(), key.toStdString(), content.toStdString(), category.toStdString());
        }

        if (m_factStatusLabel) {
            m_factStatusLabel->setText("✓ Memory fact saved successfully.");
            m_factStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusSuccess));
        }
        refreshMemories();
    } catch (const std::exception& exc) {
        if (m_factStatusLabel) {
            m_factStatusLabel->setText(QString("✕ Error saving fact: %1").arg(exc.what()));
            m_factStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusError));
        }
    }
}

void MemoryView::onDeleteFactClicked()
{
    if (!m_coordinator || m_selectedMemoryId.isEmpty()) return;

    m_coordinator->deleteMemoryEntry(m_selectedMemoryId.toStdString());
    m_selectedMemoryId.clear();
    if (m_keyEdit) m_keyEdit->clear();
    if (m_contentEdit) m_contentEdit->clear();
    if (m_factMetaLabel) m_factMetaLabel->setText("ID: None");
    if (m_factStatusLabel) {
        m_factStatusLabel->setText("✓ Memory fact deleted.");
        m_factStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusSuccess));
    }
    refreshMemories();
}

void MemoryView::refreshConversations()
{
    if (!m_coordinator) return;

    m_allRecords = m_coordinator->listConversations(50);

    if (m_listWidget) {
        m_listWidget->blockSignals(true);
        m_listWidget->clear();
        for (const auto& rec : m_allRecords) {
            QString title = QString::fromStdString(rec.title);
            if (title.isEmpty()) title = "Untitled Conversation";

            auto* item = new QListWidgetItem(title, m_listWidget);
            item->setData(Qt::UserRole, QString::fromStdString(rec.id));
        }
        m_listWidget->blockSignals(false);
    }

    if (!m_selectedCid.isEmpty()) {
        updateDetailPane(m_selectedCid);
    }
}

void MemoryView::onSearchChanged(const QString& text)
{
    if (!m_coordinator) return;

    std::string query = text.trimmed().toStdString();
    if (query.empty()) {
        refreshConversations();
        return;
    }

    auto results = m_coordinator->searchConversations(query, 50, false);

    if (m_listWidget) {
        m_listWidget->blockSignals(true);
        m_listWidget->clear();
        for (const auto& rec : results) {
            QString title = QString::fromStdString(rec.title);
            if (title.isEmpty()) title = "Untitled Conversation";

            auto* item = new QListWidgetItem(title, m_listWidget);
            item->setData(Qt::UserRole, QString::fromStdString(rec.id));
        }
        m_listWidget->blockSignals(false);
    }
}

void MemoryView::onItemSelectionChanged()
{
    if (!m_listWidget) return;
    auto items = m_listWidget->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    QString cid = items.first()->data(Qt::UserRole).toString();
    updateDetailPane(cid);
}

void MemoryView::updateDetailPane(const QString& cid)
{
    m_selectedCid = cid;
    if (!m_coordinator) return;

    auto opt = m_coordinator->getConversation(cid.toStdString());
    if (opt.has_value()) {
        m_detailTitleLabel->setText(QString::fromStdString(opt->title));
        int64_t msgCount = m_coordinator->getMessageCount(cid.toStdString());
        m_detailMetaLabel->setText(
            QString("CID: %1 | Messages: %2 | Created: %3")
            .arg(QString::fromStdString(opt->id).left(8))
            .arg(msgCount)
            .arg(QString::fromStdString(opt->createdAt))
        );
    }

    auto messages = m_coordinator->getConversationMessages(cid.toStdString());
    QString transcriptHtml;
    for (const auto& msg : messages) {
        QString roleColor = (msg.role == "user") ? Theme::Primary : Theme::Accent;
        QString roleName = (msg.role == "user") ? "User" : "FLEX Assistant";

        transcriptHtml += QString("<p style='margin: 4px 0;'><b style='color: %1;'>%2:</b> %3</p>")
            .arg(roleColor, roleName, QString::fromStdString(msg.content).toHtmlEscaped());
    }

    if (transcriptHtml.isEmpty()) {
        transcriptHtml = "<p style='color: #64748B; font-style: italic;'>No messages in this conversation yet.</p>";
    }

    m_messagesBrowser->setHtml(transcriptHtml);
}

void MemoryView::onResumeClicked()
{
    if (!m_selectedCid.isEmpty()) {
        emit resumeConversationRequested(m_selectedCid);
    }
}

void MemoryView::onDeleteClicked()
{
    if (m_selectedCid.isEmpty() || !m_coordinator) return;

    m_coordinator->deleteConversation(m_selectedCid.toStdString());
    m_selectedCid.clear();
    m_detailTitleLabel->setText("Select a conversation to inspect transcript");
    m_detailMetaLabel->setText("CID: None | Messages: 0");
    m_messagesBrowser->clear();
    refreshConversations();
}

} // namespace flex::ui
