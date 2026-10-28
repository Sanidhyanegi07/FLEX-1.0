#ifndef FLEX_UI_RIGHT_SIDEBAR_HPP
#define FLEX_UI_RIGHT_SIDEBAR_HPP

#include <QFrame>
#include <QLabel>
#include <QPushButton>

namespace flex::ui {

/**
 * Right Information & System Status Sidebar for FLEX 1.0.
 *
 * Components:
 * 1. User Profile Card (honest local offline identity notice).
 * 2. System Status Panel (live telemetry for AI engine, C11 core, voice, storage, network).
 * 3. Quick Actions Panel (shortcuts to Voice Studio, Projects, Memory).
 * 4. Tagline footer.
 */
class RightSidebar : public QWidget {
    Q_OBJECT

public:
    explicit RightSidebar(QWidget* parent = nullptr);
    ~RightSidebar() override = default;

    void updateTelemetry(
        bool ollamaOnline,
        const QString& modelName,
        bool coreProcessing,
        const QString& dbPath = QString());

signals:
    void quickActionRequested(const QString& actionKey);
    void diagnosticsRequested();

private slots:
    void onSignInClicked();

private:
    void setupUi();

    // User Profile Card
    QFrame* m_profileCard{nullptr};

    // System Status
    QLabel* m_overallHealthBadge{nullptr};
    QLabel* m_aiStatusPill{nullptr};
    QLabel* m_coreStatusPill{nullptr};
    QLabel* m_voiceStatusPill{nullptr};
    QLabel* m_storageStatusPill{nullptr};
    QLabel* m_networkStatusPill{nullptr};

    QPushButton* m_diagnosticsBtn{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_RIGHT_SIDEBAR_HPP

