#ifndef FLEX_UI_PROTOTYPE_VIEW_HPP
#define FLEX_UI_PROTOTYPE_VIEW_HPP

#include <QFrame>
#include <QLabel>
#include <QStringList>
#include <QWidget>

namespace flex::ui {

/**
 * Honest Informational Prototype View for FLEX 1.0.
 *
 * Provides a space-themed architectural specification panel for unfinished capabilities.
 * Strictly displays truthful informational content; contains ZERO fake interactive buttons.
 */
class PrototypeView : public QWidget {
    Q_OBJECT

public:
    explicit PrototypeView(
        const QString& title,
        const QString& icon,
        const QString& badgeText,
        const QString& badgeColor,
        const QString& description,
        const QString& statusExplanation,
        const QStringList& unavailableFeatures,
        QWidget* parent = nullptr);

    ~PrototypeView() override = default;

private:
    void setupUi(
        const QString& title,
        const QString& icon,
        const QString& badgeText,
        const QString& badgeColor,
        const QString& description,
        const QString& statusExplanation,
        const QStringList& unavailableFeatures);
};

} // namespace flex::ui

#endif // FLEX_UI_PROTOTYPE_VIEW_HPP

