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

    m_aiStatusPill = new QLabel("● AI Engine — Checking...", this);
    m_aiStatusPill->setObjectName("StatusPill");
    mainLayout->addWidget(m_aiStatusPill);

    m_coreStatusPill = new QLabel("● C Core — C11 IDLE", this);
    m_coreStatusPill->setObjectName("StatusPill");
    m_coreStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::StatusSuccess));
    mainLayout->addWidget(m_coreStatusPill);

    m_voiceStatusPill = new QLabel("● Voice — Whisper & SAPI", this);
    m_voiceStatusPill->setObjectName("StatusPill");
    m_voiceStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::TextAccentViolet));
    mainLayout->addWidget(m_voiceStatusPill);

    m_storageStatusPill = new QLabel("● Storage — SQLite Native", this);
    m_storageStatusPill->setObjectName("StatusPill");
    mainLayout->addWidget(m_storageStatusPill);

    m_networkStatusPill = new QLabel("● Network — 127.0.0.1:11434", this);
    m_networkStatusPill->setObjectName("StatusPill");
    mainLayout->addWidget(m_networkStatusPill);

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
    m_retryBtn->setStyleSheet(
        QString("QPushButton { background-color: #1E3A5F; color: #60A5FA; border: 1px solid #3B82F6; "
                "border-radius: 4px; padding: 4px 8px; font-size: 11px; font-weight: 600; text-align: center; margin-top: 2px; }"
                "QPushButton:hover { background-color: #1D4ED8; color: #F8FAFC; border-color: #93C5FD; }")
    );
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
    // Update AI Engine status pill based on AiServiceState
    switch (aiState) {
    case flex::AiServiceState::Ready:
    {
        QString text = QString("\xE2\x97\x8F AI Engine \xE2\x80\x94 Connected (%1)").arg(modelName.isEmpty() ? "online" : modelName);
        m_aiStatusPill->setText(text);
        m_aiStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::StatusSuccess));
        m_overallHealthBadge->setText("\xE2\x97\x8F HEALTHY");
        m_overallHealthBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusSuccess));
        if (m_retryBtn) m_retryBtn->setVisible(false);
        break;
    }
    case flex::AiServiceState::Connecting:
    {
        m_aiStatusPill->setText("\xE2\x97\x8F AI Engine \xE2\x80\x94 Connecting...");
        m_aiStatusPill->setStyleSheet("color: #67E8F9;");  // cyan
        m_overallHealthBadge->setText("\xE2\x97\x8F STARTING");
        m_overallHealthBadge->setStyleSheet("color: #67E8F9;");
        if (m_retryBtn) m_retryBtn->setVisible(false);
        break;
    }
    case flex::AiServiceState::Recovering:
    {
        m_aiStatusPill->setText("\xE2\x97\x8F AI Engine \xE2\x80\x94 Reconnecting...");
        m_aiStatusPill->setStyleSheet("color: #FCD34D;");  // amber
        m_overallHealthBadge->setText("\xE2\x97\x8B RECOVERING");
        m_overallHealthBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusWarning));
        if (m_retryBtn) m_retryBtn->setVisible(false);
        break;
    }
    case flex::AiServiceState::Offline:
    {
        m_aiStatusPill->setText("\xE2\x97\x8B AI Engine \xE2\x80\x94 Offline");
        m_aiStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::StatusOffline));
        m_overallHealthBadge->setText("\xE2\x97\x8B AI OFFLINE");
        m_overallHealthBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusWarning));
        if (m_retryBtn) {
            m_retryBtn->setToolTip("Ollama is not running.\nStart it with: ollama serve\nThen click Retry.");
            m_retryBtn->setVisible(true);
        }
        break;
    }
    case flex::AiServiceState::ModelMissing:
    {
        QString mdl = modelName.isEmpty() ? "model" : modelName;
        m_aiStatusPill->setText(QString("\xE2\x97\x8B AI Engine \xE2\x80\x94 Model Missing (%1)").arg(mdl));
        m_aiStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::StatusError));
        m_overallHealthBadge->setText("\xE2\x97\x8B MODEL MISSING");
        m_overallHealthBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusError));
        if (m_retryBtn) {
            m_retryBtn->setToolTip(QString("Model '%1' is not pulled.\nRun: ollama pull %1\nThen click Retry.").arg(mdl));
            m_retryBtn->setVisible(true);
        }
        break;
    }
    case flex::AiServiceState::Error:
    default:
    {
        m_aiStatusPill->setText("\xE2\x97\x8B AI Engine \xE2\x80\x94 Error");
        m_aiStatusPill->setStyleSheet(QString("color: %1;").arg(Theme::StatusError));
        m_overallHealthBadge->setText("\xE2\x97\x8B AI ERROR");
        m_overallHealthBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusError));
        if (m_retryBtn) m_retryBtn->setVisible(true);
        break;
    }
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

