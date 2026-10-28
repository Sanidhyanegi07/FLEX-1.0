#include "flex/ui/top_header_widget.hpp"
#include "flex/ui/theme.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace flex::ui {

TopHeaderWidget::TopHeaderWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("TopHeaderFrame");
    setFixedHeight(54);
    setupUi();
}

void TopHeaderWidget::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(18, 8, 16, 8);
    mainLayout->setSpacing(10);

    // Left brand container
    auto* brandContainer = new QWidget(this);
    auto* brandLayout = new QVBoxLayout(brandContainer);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(1);

    m_brandTitle = new QLabel("FLEX 1.0", brandContainer);
    m_brandTitle->setObjectName("TopBrandTitle");

    m_brandSubtitle = new QLabel("HYBRID C CORE (C11) + LOCAL AI PIPELINE", brandContainer);
    m_brandSubtitle->setObjectName("TopBrandSubtitle");

    brandLayout->addWidget(m_brandTitle);
    brandLayout->addWidget(m_brandSubtitle);
    mainLayout->addWidget(brandContainer);

    mainLayout->addStretch();

    // Right telemetry badges
    auto* telemetryContainer = new QWidget(this);
    auto* teleLayout = new QHBoxLayout(telemetryContainer);
    teleLayout->setContentsMargins(0, 0, 0, 0);
    teleLayout->setSpacing(8);

    m_coreBadge = new QLabel("● C CORE: IDLE", telemetryContainer);
    m_coreBadge->setObjectName("StatusBadge");
    m_coreBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusSuccess));

    m_aiBadge = new QLabel("○ OLLAMA: CHECKING...", telemetryContainer);
    m_aiBadge->setObjectName("StatusBadge");
    m_aiBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusWarning));

    m_opBadge = new QLabel("OP #0", telemetryContainer);
    m_opBadge->setObjectName("StatusBadge");

    m_sidebarToggleBtn = new QPushButton("◨ Sidebar", telemetryContainer);
    m_sidebarToggleBtn->setObjectName("SidebarToggleBtn");
    m_sidebarToggleBtn->setCursor(Qt::PointingHandCursor);
    m_sidebarToggleBtn->setToolTip("Toggle Right Information Sidebar");
    connect(m_sidebarToggleBtn, &QPushButton::clicked, this, &TopHeaderWidget::onToggleClicked);

    teleLayout->addWidget(m_coreBadge);
    teleLayout->addWidget(m_aiBadge);
    teleLayout->addWidget(m_opBadge);
    teleLayout->addWidget(m_sidebarToggleBtn);

    mainLayout->addWidget(telemetryContainer);
}

void TopHeaderWidget::setOllamaStatus(bool online, const QString& modelName)
{
    if (online) {
        QString text = QString("● OLLAMA: %1").arg(modelName.isEmpty() ? "ONLINE" : modelName.toUpper());
        m_aiBadge->setText(text);
        m_aiBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusSuccess));
    } else {
        m_aiBadge->setText("○ OLLAMA: OFFLINE");
        m_aiBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusOffline));
    }
}

void TopHeaderWidget::setCoreStatus(const QString& stateText, bool processing)
{
    if (processing) {
        m_coreBadge->setText(QString("● C CORE: %1").arg(stateText.toUpper()));
        m_coreBadge->setStyleSheet(QString("color: %1;").arg(Theme::TextAccentCyan));
    } else {
        m_coreBadge->setText(QString("● C CORE: %1").arg(stateText.toUpper()));
        m_coreBadge->setStyleSheet(QString("color: %1;").arg(Theme::StatusSuccess));
    }
}

void TopHeaderWidget::setOpCount(uint64_t count)
{
    m_opBadge->setText(QString("OP #%1").arg(count));
}

void TopHeaderWidget::setRightSidebarVisible(bool visible)
{
    m_rightSidebarVisible = visible;
    if (visible) {
        m_sidebarToggleBtn->setText("◨ Sidebar");
        m_sidebarToggleBtn->setToolTip("Hide Right Information Sidebar");
    } else {
        m_sidebarToggleBtn->setText("◧ Sidebar");
        m_sidebarToggleBtn->setToolTip("Show Right Information Sidebar");
    }
}

void TopHeaderWidget::onToggleClicked()
{
    setRightSidebarVisible(!m_rightSidebarVisible);
    emit toggleRightSidebarRequested();
}

} // namespace flex::ui

