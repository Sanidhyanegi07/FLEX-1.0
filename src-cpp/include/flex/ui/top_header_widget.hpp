#ifndef FLEX_UI_TOP_HEADER_WIDGET_HPP
#define FLEX_UI_TOP_HEADER_WIDGET_HPP

#include <QFrame>
#include <QLabel>
#include <QPushButton>

namespace flex::ui {

/**
 * Persistent Top Telemetry Header for FLEX 1.0.
 *
 * Displays:
 * - Brand title "FLEX 1.0" and subtitle "HYBRID C CORE (C11) + LOCAL AI PIPELINE".
 * - Live C11 Core status badge (IDLE / PROCESSING).
 * - Live Ollama AI engine connectivity badge.
 * - Monotonic operation counter (OP #N).
 * - Right Information Sidebar collapse/expand toggle button.
 */
class TopHeaderWidget : public QFrame {
    Q_OBJECT

public:
    explicit TopHeaderWidget(QWidget* parent = nullptr);
    ~TopHeaderWidget() override = default;

    void setOllamaStatus(bool online, const QString& modelName = QString());
    void setCoreStatus(const QString& stateText, bool processing = false);
    void setOpCount(uint64_t count);
    void setRightSidebarVisible(bool visible);

signals:
    void toggleRightSidebarRequested();

private slots:
    void onToggleClicked();

private:
    void setupUi();

    QLabel* m_brandTitle{nullptr};
    QLabel* m_brandSubtitle{nullptr};

    QLabel* m_coreBadge{nullptr};
    QLabel* m_aiBadge{nullptr};
    QLabel* m_opBadge{nullptr};
    QPushButton* m_sidebarToggleBtn{nullptr};

    bool m_rightSidebarVisible{true};
};

} // namespace flex::ui

#endif // FLEX_UI_TOP_HEADER_WIDGET_HPP

