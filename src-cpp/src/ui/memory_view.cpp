#include "flex/ui/memory_view.hpp"
#include "flex/ui/theme.hpp"

#include <QDateTime>
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

    auto* titleLbl = new QLabel("🧠 Conversation Memory & Context Vault", headerBox);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: 700; color: #F8FAFC;");

    auto* subLbl = new QLabel("Native SQLite memory storage engine. Inspect, recall, or purge stored conversation history.", headerBox);
    subLbl->setStyleSheet("font-size: 12px; color: #94A3B8;");

    headerLayout->addWidget(titleLbl);
    headerLayout->addWidget(subLbl);
    mainLayout->addWidget(headerBox);

    // Splitter: List on Left (320px) + Details on Right
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);

    // Left List Container
    auto* leftContainer = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 8, 0);
    leftLayout->setSpacing(8);

    m_searchBox = new QLineEdit(leftContainer);
    m_searchBox->setPlaceholderText("Search conversation memory...");
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

    m_detailTitleLabel = new QLabel("Select a conversation to inspect memory", rightContainer);
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
    m_resumeBtn->setEnabled(false);
    connect(m_resumeBtn, &QPushButton::clicked, this, &MemoryView::onResumeClicked);

    m_deleteBtn = new QPushButton("🗑 Delete from SQLite", rightContainer);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MemoryView::onDeleteClicked);

    btnRow->addWidget(m_resumeBtn);
    btnRow->addWidget(m_deleteBtn);
    rightLayout->addLayout(btnRow);

    splitter->addWidget(rightContainer);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 280, 560 });

    mainLayout->addWidget(splitter, 1);
}

void MemoryView::refreshConversations()
{
    if (!m_coordinator) {
        return;
    }

    m_allRecords = m_coordinator->listConversations(100);
    onSearchChanged(m_searchBox ? m_searchBox->text() : QString());
}

void MemoryView::onSearchChanged(const QString& text)
{
    m_listWidget->clear();
    QString query = text.trimmed().toLower();

    for (const auto& rec : m_allRecords) {
        QString title = QString::fromStdString(rec.title);
        QString cid = QString::fromStdString(rec.id);

        if (!query.isEmpty() && !title.toLower().contains(query) && !cid.toLower().contains(query)) {
            continue;
        }

        auto* item = new QListWidgetItem(m_listWidget);
        item->setText(title.isEmpty() ? "Untitled Chat" : title);
        item->setData(Qt::UserRole, cid);
        m_listWidget->addItem(item);
    }
}

void MemoryView::onItemSelectionChanged()
{
    auto* item = m_listWidget->currentItem();
    if (!item) {
        m_selectedCid.clear();
        m_detailTitleLabel->setText("Select a conversation to inspect memory");
        m_detailMetaLabel->setText("CID: None | Messages: 0");
        m_messagesBrowser->clear();
        m_resumeBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
        return;
    }

    m_selectedCid = item->data(Qt::UserRole).toString();
    updateDetailPane(m_selectedCid);
}

void MemoryView::updateDetailPane(const QString& cid)
{
    if (!m_coordinator || cid.isEmpty()) {
        return;
    }

    auto convOpt = m_coordinator->getConversation(cid.toStdString());
    if (!convOpt.has_value()) {
        return;
    }

    auto messages = m_coordinator->getConversationMessages(cid.toStdString());

    m_detailTitleLabel->setText(QString::fromStdString(convOpt->title));
    m_detailMetaLabel->setText(
        QString("CID: %1 | Messages: %2 | Updated: %3")
        .arg(cid.left(12) + "...")
        .arg(messages.size())
        .arg(QString::fromStdString(convOpt->updatedAt))
    );

    QString html = "<div style='font-family: Segoe UI, sans-serif; font-size: 12px;'>";
    for (const auto& msg : messages) {
        bool isUser = (msg.role == "user");
        QString roleColor = isUser ? Theme::TextAccentViolet : Theme::TextAccentCyan;
        QString roleName = isUser ? "USER" : "ASSISTANT";

        html += QString("<p><b style='color: %1;'>[%2]</b> <span style='color: #64748B; font-size: 10px;'>#%3</span><br/>")
            .arg(roleColor, roleName).arg(msg.sequenceNum);
        html += QString("<span style='color: #F8FAFC;'>%1</span></p>")
            .arg(QString::fromStdString(msg.content).toHtmlEscaped().replace("\n", "<br/>"));
    }
    html += "</div>";

    m_messagesBrowser->setHtml(html);
    m_resumeBtn->setEnabled(true);
    m_deleteBtn->setEnabled(true);
}

void MemoryView::onResumeClicked()
{
    if (!m_selectedCid.isEmpty()) {
        emit resumeConversationRequested(m_selectedCid);
    }
}

void MemoryView::onDeleteClicked()
{
    if (!m_coordinator || m_selectedCid.isEmpty()) {
        return;
    }

    auto reply = QMessageBox::question(
        this,
        "Purge Conversation",
        "Are you sure you want to permanently delete this conversation from SQLite?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        m_coordinator->deleteConversation(m_selectedCid.toStdString());
        refreshConversations();
    }
}

} // namespace flex::ui
