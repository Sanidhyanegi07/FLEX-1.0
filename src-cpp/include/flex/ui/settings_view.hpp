#ifndef FLEX_UI_SETTINGS_VIEW_HPP
#define FLEX_UI_SETTINGS_VIEW_HPP

#include "flex/backend.hpp"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

#include <memory>

namespace flex::ui {

/**
 * Settings & Configuration View for FLEX 1.0.
 *
 * Controls:
 * - Ollama Server Endpoint URL
 * - Non-blocking connection test
 * - Active Model Name
 * - Inference Temperature (0.0 to 2.0)
 * - Context Window Tokens (num_ctx)
 * - Request Timeout (seconds)
 * - Save to runtime coordinator
 */
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(
        std::shared_ptr<BackendCoordinator> coordinator,
        QWidget* parent = nullptr);

    ~SettingsView() override = default;

signals:
    void settingsUpdated();

private slots:
    void onTestConnectionClicked();
    void onSaveClicked();
    void onResetClicked();

private:
    void setupUi();
    void loadCurrentConfig();

    std::shared_ptr<BackendCoordinator> m_coordinator;

    QLineEdit* m_endpointEdit{nullptr};
    QPushButton* m_testBtn{nullptr};
    QLabel* m_testResultLabel{nullptr};

    QLineEdit* m_modelEdit{nullptr};
    QDoubleSpinBox* m_tempSpin{nullptr};
    QSpinBox* m_numCtxSpin{nullptr};
    QSpinBox* m_timeoutSpin{nullptr};

    QPushButton* m_saveBtn{nullptr};
    QPushButton* m_resetBtn{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_SETTINGS_VIEW_HPP

