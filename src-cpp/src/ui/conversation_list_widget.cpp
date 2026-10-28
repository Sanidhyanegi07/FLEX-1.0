#include "flex/ui/conversation_list_widget.hpp"
#include "flex/ui/theme.hpp"

#include <QAction>
#include <QMenu>
#include <QMessageBox>

namespace flex::ui {

ConversationListWidget::ConversationListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // Search bar
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Search conversations...");
    m_searchBox->setClearButtonEnabled(true);
    connect(m_searchBox, &QLineEdit::textChanged, this, &ConversationListWidget::onFilterTextChanged);

    // Conversation List Widget
    m_listWidget = new QListWidget(this);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listWidget, &QListWidget::itemClicked, this, &ConversationListWidget::onItemClicked);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, this, &ConversationListWidget::onCustomContextMenuRequested);

    layout->addWidget(m_searchBox);
    layout->addWidget(m_listWidget);
}

void ConversationListWidget::setConversations(const std::vector<ConversationRecord>& conversations)
{
    m_allConversations = conversations;
    rebuildListItems();
}

void ConversationListWidget::rebuildListItems()
{
    m_listWidget->clear();

    QString filter = m_currentFilter.trimmed().toLower();
    QListWidgetItem* itemToSelect = nullptr;

    for (const auto& conv : m_allConversations) {
        QString title = QString::fromStdString(conv.title);
        QString cid = QString::fromStdString(conv.id);

        if (!filter.isEmpty() && !title.toLower().contains(filter) && !cid.toLower().contains(filter)) {
            continue;
        }

        auto* item = new QListWidgetItem();
        item->setText(title.isEmpty() ? "New Chat" : title);
        item->setData(Qt::UserRole, cid);
        item->setToolTip(QString("ID: %1\nUpdated: %2").arg(cid, QString::fromStdString(conv.updatedAt)));

        m_listWidget->addItem(item);

        if (cid == m_selectedCid) {
            itemToSelect = item;
        }
    }

    if (itemToSelect) {
        m_listWidget->setCurrentItem(itemToSelect);
    }
}

void ConversationListWidget::selectConversation(const QString& conversationId)
{
    m_selectedCid = conversationId;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto* item = m_listWidget->item(i);
        if (item && item->data(Qt::UserRole).toString() == conversationId) {
            m_listWidget->setCurrentItem(item);
            return;
        }
    }
}

QString ConversationListWidget::currentConversationId() const
{
    auto* item = m_listWidget->currentItem();
    if (!item) {
        return QString();
    }
    return item->data(Qt::UserRole).toString();
}

int ConversationListWidget::conversationCount() const
{
    return static_cast<int>(m_allConversations.size());
}

void ConversationListWidget::clearList()
{
    m_allConversations.clear();
    m_selectedCid.clear();
    m_listWidget->clear();
}

void ConversationListWidget::onItemClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }
    QString cid = item->data(Qt::UserRole).toString();
    m_selectedCid = cid;
    emit conversationSelected(cid);
}

void ConversationListWidget::onFilterTextChanged(const QString& filterText)
{
    m_currentFilter = filterText;
    rebuildListItems();
}

void ConversationListWidget::onCustomContextMenuRequested(const QPoint& pos)
{
    auto* item = m_listWidget->itemAt(pos);
    if (!item) {
        return;
    }

    QString cid = item->data(Qt::UserRole).toString();
    QString title = item->text();

    QMenu menu(this);
    QAction* deleteAction = menu.addAction(QString("Delete \"%1\"").arg(title));

    QAction* selectedAction = menu.exec(m_listWidget->mapToGlobal(pos));
    if (selectedAction == deleteAction) {
        emit conversationDeleteRequested(cid);
    }
}

} // namespace flex::ui

