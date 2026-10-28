#include "flex/ui/navigation_sidebar.hpp"
#include "flex/ui/theme.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace flex::ui {

NavigationSidebar::NavigationSidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("LeftSidebarWidget");
    setFixedWidth(220);
    setupUi();
}

void NavigationSidebar::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 12, 10, 10);
    mainLayout->setSpacing(8);

    // =========================================================================
    // 1. Brand Row: [ F ] FLEX 1.0
    // =========================================================================
    auto* brandRow = new QWidget(this);
    auto* brandLayout = new QHBoxLayout(brandRow);
    brandLayout->setContentsMargins(4, 0, 4, 4);
    brandLayout->setSpacing(8);

    auto* logoBox = new QLabel(" F ", brandRow);
    logoBox->setObjectName("BrandLogoBox");

    auto* brandTitle = new QLabel("FLEX 1.0", brandRow);
    brandTitle->setStyleSheet("font-size: 13px; font-weight: 700; color: #F8FAFC; letter-spacing: 0.5px;");

    brandLayout->addWidget(logoBox);
    brandLayout->addWidget(brandTitle);
    brandLayout->addStretch();
    mainLayout->addWidget(brandRow);

    // =========================================================================
    // 2. New Chat Button
    // =========================================================================
    m_newChatBtn = new QPushButton("+ New Chat", this);
    m_newChatBtn->setObjectName("NewChatButton");
    m_newChatBtn->setCursor(Qt::PointingHandCursor);
    connect(m_newChatBtn, &QPushButton::clicked, this, &NavigationSidebar::newChatRequested);
    mainLayout->addWidget(m_newChatBtn);

    // =========================================================================
    // 3. Navigation Action Items
    // =========================================================================
    struct NavItemDef {
        QString key;
        QString text;
    };

    const std::vector<NavItemDef> items = {
        {"home",      "🏠  Home"},
        {"voice",     "🎙️  Voice Studio"},
        {"memory",    "🧠  Memory"},
        {"knowledge", "📚  Knowledge Base"},
        {"projects",  "📁  Projects & Files"},
        {"tools",     "⚙️  Tools"},
        {"settings",  "🛠️  Settings"}
    };

    auto* navContainer = new QWidget(this);
    auto* navLayout = new QVBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 4, 0, 4);
    navLayout->setSpacing(2);

    for (const auto& item : items) {
        auto* btn = new QPushButton(item.text, navContainer);
        btn->setObjectName("NavButton");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("viewKey", item.key);
        btn->setProperty("active", item.key == m_activeViewKey);
        connect(btn, &QPushButton::clicked, this, &NavigationSidebar::onNavButtonClicked);

        m_navButtons[item.key] = btn;
        navLayout->addWidget(btn);
    }
    mainLayout->addWidget(navContainer);

    // =========================================================================
    // 4. Recent Chats Section
    // =========================================================================
    auto* recentHeader = new QLabel("Recent Chats", this);
    recentHeader->setObjectName("RecentChatsHeader");
    mainLayout->addWidget(recentHeader);

    m_convList = new ConversationListWidget(this);
    mainLayout->addWidget(m_convList, 1);

    // =========================================================================
    // 5. Local Profile Panel (Honest Offline Identity)
    // =========================================================================
    auto* profileCard = new QFrame(this);
    profileCard->setObjectName("LocalProfileCard");

    auto* profLayout = new QVBoxLayout(profileCard);
    profLayout->setContentsMargins(8, 8, 8, 8);
    profLayout->setSpacing(2);

    auto* profReady = new QLabel("● Local AI Ready", profileCard);
    profReady->setStyleSheet(QString("font-size: 11px; font-weight: 700; color: %1;").arg(Theme::StatusSuccess));

    auto* profOffline = new QLabel("● Offline / Local Only", profileCard);
    profOffline->setStyleSheet(QString("font-family: 'Consolas', monospace; font-size: 9px; color: %1;").arg(Theme::StatusSuccess));

    auto* profNote = new QLabel("Cloud Auth: Not Implemented (Local Only)", profileCard);
    profNote->setStyleSheet("font-size: 8px; font-style: italic; color: #64748B;");

    profLayout->addWidget(profReady);
    profLayout->addWidget(profOffline);
    profLayout->addWidget(profNote);

    mainLayout->addWidget(profileCard);
}

void NavigationSidebar::setActiveView(const QString& key)
{
    if (m_activeViewKey == key) {
        return;
    }
    m_activeViewKey = key;

    for (auto& [viewKey, btn] : m_navButtons) {
        bool isActive = (viewKey == key);
        btn->setProperty("active", isActive);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

void NavigationSidebar::onNavButtonClicked()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) {
        return;
    }
    QString key = btn->property("viewKey").toString();
    setActiveView(key);
    emit viewChangeRequested(key);
}

} // namespace flex::ui

