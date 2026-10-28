#ifndef FLEX_UI_NAVIGATION_SIDEBAR_HPP
#define FLEX_UI_NAVIGATION_SIDEBAR_HPP

#include "flex/ui/conversation_list_widget.hpp"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include <map>
#include <string>

namespace flex::ui {

/**
 * Left Navigation Sidebar for FLEX 1.0.
 *
 * Hosts:
 * - Brand header [ F ] FLEX 1.0
 * - "+ New Chat" action button
 * - Main navigation switches: Home, Voice Studio, Memory, Knowledge Base,
 *   Projects & Files, Tools, Settings.
 * - Dynamic Recent Chats list with search filtering.
 * - Honest Local Profile Panel (offline local status).
 */
class NavigationSidebar : public QWidget {
    Q_OBJECT

public:
    explicit NavigationSidebar(QWidget* parent = nullptr);
    ~NavigationSidebar() override = default;

    [[nodiscard]] ConversationListWidget* conversationListWidget() const { return m_convList; }
    [[nodiscard]] QString activeViewKey() const { return m_activeViewKey; }

    void setActiveView(const QString& key);

signals:
    void newChatRequested();
    void viewChangeRequested(const QString& viewKey);

private slots:
    void onNavButtonClicked();

private:
    void setupUi();

    QPushButton* m_newChatBtn{nullptr};
    ConversationListWidget* m_convList{nullptr};

    std::map<QString, QPushButton*> m_navButtons;
    QString m_activeViewKey{"home"};
};

} // namespace flex::ui

#endif // FLEX_UI_NAVIGATION_SIDEBAR_HPP

