#include "flex/ui/right_sidebar.hpp"
#include "flex/backend.hpp"
#include "flex/ui/theme.hpp"

#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace flex::ui {

RightSidebar::RightSidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("RightSidebarWidget");
    setFixedWidth(240);
    setupUi();
}

void RightSidebar::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 14, 12, 14);
    mainLayout->setSpacing(12);

    // =========================================================================
    // 1. User Profile Card
    // =========================================================================
    m_profileCard = new QFrame(this);
    m_profileCard->setObjectName("UserProfileCard");

    auto* profLayout = new QVBoxLayout(m_profileCard);
    profLayout->setContentsMargins(10, 10, 10, 10);
    profLayout->setSpacing(4);
    profLayout->setAlignment(Qt::AlignCenter);

    auto* avatarLbl = new QLabel("👤", m_profileCard);
    avatarLbl->setStyleSheet(QString("font-size: 22px; color: %1;").arg(Theme::TextAccentViolet));
    avatarLbl->setAlignment(Qt::AlignCenter);

    auto* welcomeTitle = new QLabel("Local Workspace", m_profileCard);
    welcomeTitle->setStyleSheet("font-size: 12px; font-weight: 700; color: #F8FAFC;");
    welcomeTitle->setAlignment(Qt::AlignCenter);

    auto* welcomeSub = new QLabel("Offline & Private Identity", m_profileCard);
    welcomeSub->setStyleSheet("font-size: 10px; color: #94A3B8;");
    welcomeSub->setAlignment(Qt::AlignCenter);

    auto* sessionBtn = new QPushButton("Session Details", m_profileCard);
    sessionBtn->setObjectName("NavButton");
    sessionBtn->setCursor(Qt::PointingHandCursor);
    sessionBtn->setStyleSheet(
        QString("QPushButton { background-color: %1; color: #F8FAFC; border: 1px solid %2; "
                "border-radius: 4px; padding: 4px 10px; font-size: 11px; font-weight: 600; text-align: center; }"
                "QPushButton:hover { background-color: #2C1B5E; border-color: %3; }")
        .arg(Theme::BgSidebarActive, Theme::Border, Theme::BorderFocus)
    );
    connect(sessionBtn, &QPushButton::clicked, this, &RightSidebar::onSignInClicked);

    auto* localNote = new QLabel("Cloud Auth: Not Required (Local Only)", m_profileCard);
    localNote->setStyleSheet("font-size: 8px; font-style: italic; color: #64748B;");
    localNote->setAlignment(Qt::AlignCenter);

    profLayout->addWidget(avatarLbl);
    profLayout->addWidget(welcomeTitle);
    profLayout->addWidget(welcomeSub);
    profLayout->addSpacing(4);
    profLayout->addWidget(sessionBtn);
    profLayout->addWidget(localNote);

    mainLayout->addWidget(m_profileCard);

    // =========================================================================
    // 2. System Status Panel
    // =========================================================================
    auto* statusHdrBox = new QWidget(this);
    auto* statusHdrLayout = new QHBoxLayout(statusHdrBox);
    statusHdrLayout->setContentsMargins(0, 4, 0, 2);

    auto* statusHdr = new QLabel("SYSTEM STATUS", statusHdrBox);
    statusHdr->setObjectName("SectionHeaderLabel");

    m_overallHealthBadge = new QLabel("● READY", statusHdrBox);
    m_overallHealthBadge->setStyleSheet(
        QString("color: %1; font-family: 'Consolas', monospace; font-size: 9px; font-weight: bold;")
        .arg(Theme::StatusSuccess)
    );

    statusHdrLayout->addWidget(statusHdr);
    statusHdrLayout->addStretch();
    statusHdrLayout->addWidget(m_overallHealthBadge);
    mainLayout->addWidget(statusHdrBox);

    auto makePill = [&](const QString& txt, const QString& col = QString()) {
        auto* lbl = new QLabel(txt, this);
        lbl->setObjectName("StatusPill");
        if (!col.isEmpty()) lbl->setStyleSheet(QString("color: %1;").arg(col));
        mainLayout->addWidget(lbl);
        return lbl;
    };
    m_aiStatusPill = makePill("● AI Engine — Checking...");
    m_coreStatusPill = makePill("● C Core — C11 IDLE", Theme::StatusSuccess);
    m_voiceStatusPill = makePill("● Voice — Whisper & SAPI", Theme::TextAccentViolet);
    m_storageStatusPill = makePill("● Storage — SQLite Native");
    m_networkStatusPill = makePill("● Network — 127.0.0.1:11434");

    m_diagnosticsBtn = new QPushButton("📊 Diagnostics Drawer ▼", this);
    m_diagnosticsBtn->setCursor(Qt::PointingHandCursor);
    m_diagnosticsBtn->setStyleSheet(
        QString("QPushButton { background-color: %1; color: #F8FAFC; border: 1px solid %2; "
                "border-radius: 4px; padding: 5px 8px; font-size: 11px; font-weight: 600; text-align: center; margin-top: 2px; }"
                "QPushButton:hover { background-color: %3; border-color: %4; }")
        .arg(Theme::BgSidebarActive, Theme::Border, Theme::BgSidebarHover, Theme::BorderFocus)
    );
    connect(m_diagnosticsBtn, &QPushButton::clicked, this, &RightSidebar::diagnosticsRequested);
    mainLayout->addWidget(m_diagnosticsBtn);

    // Retry AI Connection button (visible when AI is offline/error/model-missing)
    m_retryBtn = new QPushButton("\xF0\x9F\x94\x84 Retry AI Connection", this);
    m_retryBtn->setObjectName("RetryBtn");
    m_retryBtn->setCursor(Qt::PointingHandCursor);
    m_retryBtn->setToolTip("Click to immediately retry connecting to Ollama");
    m_retryBtn->setStyleSheet("QPushButton { background-color: #1E3A5F; color: #60A5FA; border: 1px solid #3B82F6; "
                              "border-radius: 4px; padding: 4px 8px; font-size: 11px; font-weight: 600; text-align: center; margin-top: 2px; }"
                              "QPushButton:hover { background-color: #1D4ED8; color: #F8FAFC; border-color: #93C5FD; }");
    connect(m_retryBtn, &QPushButton::clicked, this, &RightSidebar::retryAiConnectionRequested);
    m_retryBtn->setVisible(false);   // Hidden when AI is healthy
    mainLayout->addWidget(m_retryBtn);

    // =========================================================================
    // 3. Quick Actions Panel
    // =========================================================================
    auto* qaHdr = new QLabel("QUICK ACTIONS", this);
    qaHdr->setObjectName("SectionHeaderLabel");
    mainLayout->addSpacing(6);
    mainLayout->addWidget(qaHdr);

    struct QuickActionDef {
        QString label;
        QString key;
    };
    const std::vector<QuickActionDef> actions = {
        {"🎙 Start Voice Chat", "voice"},
        {"📁 Open My Files", "projects"},
        {"🧠 View Memory", "memory"}
    };

    for (const auto& act : actions) {
        auto* btn = new QPushButton(act.label, this);
        btn->setObjectName("QuickActionBtn");
        btn->setCursor(Qt::PointingHandCursor);
        QString key = act.key;
        connect(btn, &QPushButton::clicked, this, [this, key]() {
            emit quickActionRequested(key);
        });
        mainLayout->addWidget(btn);
    }

    // Spacer
    mainLayout->addStretch(1);

    // =========================================================================
    // 4. Tagline Footer
    // =========================================================================
    auto* footerContainer = new QWidget(this);
    auto* footerLayout = new QVBoxLayout(footerContainer);
    footerLayout->setContentsMargins(0, 0, 0, 4);
    footerLayout->setSpacing(2);
    footerLayout->setAlignment(Qt::AlignCenter);

    auto* starLbl = new QLabel("✨", footerContainer);
    starLbl->setStyleSheet(QString("font-size: 14px; color: %1;").arg(Theme::TextAccentViolet));
    starLbl->setAlignment(Qt::AlignCenter);

    auto* taglineLbl = new QLabel("Built for privacy. Designed for intelligence.", footerContainer);
    taglineLbl->setObjectName("TaglineLabel");
    taglineLbl->setWordWrap(true);
    taglineLbl->setAlignment(Qt::AlignCenter);

    footerLayout->addWidget(starLbl);
    footerLayout->addWidget(taglineLbl);

    mainLayout->addWidget(footerContainer);
}

void RightSidebar::updateTelemetry(
    flex::AiServiceState aiState,
    const QString& modelName,
    bool coreProcessing,
    const QString& dbPath)
{
    QString pillText, pillColor, badgeText, badgeColor, retryTip;
    bool showRetry = false;
    QString mdl = modelName.isEmpty() ? "online" : modelName;

    switch (aiState) {
    case flex::AiServiceState::Ready:
        pillText = QString("\xE2\x97\x8F AI Engine \xE2\x80\x94 Connected (%1)").arg(mdl);
        pillColor = Theme::StatusSuccess;
        badgeText = "\xE2\x97\x8F HEALTHY";
        badgeColor = Theme::StatusSuccess;
        break;
    case flex::AiServiceState::Connecting:
        pillText = "\xE2\x97\x8F AI Engine \xE2\x80\x94 Connecting...";
        pillColor = "#67E8F9";
        badgeText = "\xE2\x97\x8F STARTING";
        badgeColor = "#67E8F9";
        break;
    case flex::AiServiceState::Recovering:
        pillText = "\xE2\x97\x8F AI Engine \xE2\x80\x94 Reconnecting...";
        pillColor = "#FCD34D";
        badgeText = "\xE2\x97\x8B RECOVERING";
        badgeColor = Theme::StatusWarning;
        break;
    case flex::AiServiceState::Offline:
        pillText = "\xE2\x97\x8B AI Engine \xE2\x80\x94 Offline";
        pillColor = Theme::StatusOffline;
        badgeText = "\xE2\x97\x8B AI OFFLINE";
        badgeColor = Theme::StatusWarning;
        showRetry = true;
        retryTip = "Ollama is not running.\nStart it with: ollama serve\nThen click Retry.";
        break;
    case flex::AiServiceState::ModelMissing:
        pillText = QString("\xE2\x97\x8B AI Engine \xE2\x80\x94 Model Missing (%1)").arg(mdl);
        pillColor = Theme::StatusError;
        badgeText = "\xE2\x97\x8B MODEL MISSING";
        badgeColor = Theme::StatusError;
        showRetry = true;
        retryTip = QString("Model '%1' is not pulled.\nRun: ollama pull %1\nThen click Retry.").arg(mdl);
        break;
    default:
        pillText = "\xE2\x97\x8B AI Engine \xE2\x80\x94 Error";
        pillColor = Theme::StatusError;
        badgeText = "\xE2\x97\x8B AI ERROR";
        badgeColor = Theme::StatusError;
        showRetry = true;
        break;
    }

    m_aiStatusPill->setText(pillText);
    m_aiStatusPill->setStyleSheet(QString("color: %1;").arg(pillColor));
    m_overallHealthBadge->setText(badgeText);
    m_overallHealthBadge->setStyleSheet(QString("color: %1;").arg(badgeColor));
    if (m_retryBtn) {
        if (!retryTip.isEmpty()) m_retryBtn->setToolTip(retryTip);
        m_retryBtn->setVisible(showRetry);
    }

    // C Core status
    if (coreProcessing) {
        m_coreStatusPill->setText("\xE2\x97\x8F C Core \xE2\x80\x94 PROCESSING");
        m_coreStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::TextAccentCyan));
    } else {
        m_coreStatusPill->setText("\xE2\x97\x8F C Core \xE2\x80\x94 C11 IDLE");
        m_coreStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::StatusSuccess));
    }

    // Storage path tooltip
    if (!dbPath.isEmpty()) {
        m_storageStatusPill->setToolTip(dbPath);
    }
}

void RightSidebar::onSignInClicked()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("FLEX 1.0 — Local Session");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("<b>Local AI Workspace Active</b>");
    msgBox.setInformativeText(
        "FLEX 1.0 runs strictly on your machine with 100% local privacy.<br><br>"
        "All memory, conversations, and AI models execute on this device. "
        "Cloud authentication is not required for any native operations."
    );
    msgBox.setStyleSheet(Theme::getApplicationStylesheet());
    msgBox.exec();
}

} // namespace flex::ui

