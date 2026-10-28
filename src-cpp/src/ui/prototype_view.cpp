#include "flex/ui/prototype_view.hpp"
#include "flex/ui/theme.hpp"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace flex::ui {

PrototypeView::PrototypeView(
    const QString& title,
    const QString& icon,
    const QString& badgeText,
    const QString& badgeColor,
    const QString& description,
    const QString& statusExplanation,
    const QStringList& unavailableFeatures,
    QWidget* parent)
    : QWidget(parent)
{
    setupUi(title, icon, badgeText, badgeColor, description, statusExplanation, unavailableFeatures);
}

void PrototypeView::setupUi(
    const QString& title,
    const QString& icon,
    const QString& badgeText,
    const QString& badgeColor,
    const QString& description,
    const QString& statusExplanation,
    const QStringList& unavailableFeatures)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");

    auto* container = new QWidget();
    container->setStyleSheet("background-color: transparent;");
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(36, 36, 36, 36);
    layout->setSpacing(16);

    // Header Card
    auto* headerCard = new QFrame(container);
    headerCard->setObjectName("UserProfileCard");
    auto* headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(20, 20, 20, 20);
    headerLayout->setSpacing(8);

    auto* topRow = new QHBoxLayout();
    auto* iconLbl = new QLabel(icon, headerCard);
    iconLbl->setStyleSheet("font-size: 26px;");
    auto* titleLbl = new QLabel(title, headerCard);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: 700; color: #F8FAFC;");

    topRow->addWidget(iconLbl);
    topRow->addWidget(titleLbl);
    topRow->addStretch();

    auto* badgeLbl = new QLabel(badgeText, headerCard);
    badgeLbl->setStyleSheet(
        QString("background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; "
                "padding: 4px 10px; font-family: 'Consolas', monospace; font-size: 11px; font-weight: bold;")
        .arg(Theme::BgCard, badgeColor, Theme::Border)
    );
    topRow->addWidget(badgeLbl);
    headerLayout->addLayout(topRow);

    auto* descLbl = new QLabel(description, headerCard);
    descLbl->setStyleSheet("color: #94A3B8; font-size: 13px; margin-top: 4px;");
    descLbl->setWordWrap(true);
    headerLayout->addWidget(descLbl);
    layout->addWidget(headerCard);

    // Architectural Status Explanation Card
    auto* statusCard = new QFrame(container);
    statusCard->setObjectName("UserProfileCard");
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(20, 16, 20, 16);
    statusLayout->setSpacing(6);

    auto* statusHdr = new QLabel("ARCHITECTURAL STATUS", statusCard);
    statusHdr->setObjectName("SectionHeaderLabel");
    statusLayout->addWidget(statusHdr);

    auto* explLbl = new QLabel(statusExplanation, statusCard);
    explLbl->setStyleSheet("color: #F8FAFC; font-size: 12px; line-height: 1.4;");
    explLbl->setWordWrap(true);
    statusLayout->addWidget(explLbl);
    layout->addWidget(statusCard);

    // Truthful Limitations Card
    if (!unavailableFeatures.isEmpty()) {
        auto* limitsCard = new QFrame(container);
        limitsCard->setObjectName("UserProfileCard");
        auto* limitsLayout = new QVBoxLayout(limitsCard);
        limitsLayout->setContentsMargins(20, 16, 20, 16);
        limitsLayout->setSpacing(8);

        auto* limitsHdr = new QLabel("SCOPE BOUNDARIES & UNIMPLEMENTED CAPABILITIES", limitsCard);
        limitsHdr->setObjectName("SectionHeaderLabel");
        limitsLayout->addWidget(limitsHdr);

        for (const auto& feat : unavailableFeatures) {
            auto* itemLbl = new QLabel("•  " + feat, limitsCard);
            itemLbl->setStyleSheet("color: #94A3B8; font-size: 12px;");
            itemLbl->setWordWrap(true);
            limitsLayout->addWidget(itemLbl);
        }
        layout->addWidget(limitsCard);
    }

    layout->addStretch();
    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

} // namespace flex::ui
