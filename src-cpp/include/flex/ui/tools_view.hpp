#ifndef FLEX_UI_TOOLS_VIEW_HPP
#define FLEX_UI_TOOLS_VIEW_HPP

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextBrowser>
#include <QListWidget>
#include <QTabWidget>
#include <memory>

#include "flex/backend.hpp"

namespace flex::ui {

/**
 * Native C11/C++17 Tools View for FLEX 1.0.
 *
 * Provides real, functional tools:
 * 1. Safe Native Calculator (governed by C11 math_evaluator)
 * 2. Workspace Sandboxed File Reader (governed by C11 workspace_analyzer)
 * 3. System & Architecture Diagnostics (governed by C11 sysinfo_collector)
 */
class ToolsView : public QWidget {
    Q_OBJECT

public:
    explicit ToolsView(
        std::shared_ptr<BackendCoordinator> coordinator,
        QWidget* parent = nullptr);
    ~ToolsView() override = default;

    // Test accessors
    [[nodiscard]] QLineEdit* calcInputEdit() const { return m_calcInput; }
    [[nodiscard]] QPushButton* calcEvaluateButton() const { return m_calcEvalBtn; }
    [[nodiscard]] QLabel* calcResultLabel() const { return m_calcResultLabel; }
    [[nodiscard]] QListWidget* calcHistoryList() const { return m_calcHistoryList; }

    [[nodiscard]] QLineEdit* filePathEdit() const { return m_filePathEdit; }
    [[nodiscard]] QPushButton* fileReadButton() const { return m_fileReadBtn; }
    [[nodiscard]] QTextBrowser* fileContentBrowser() const { return m_fileContentBrowser; }
    [[nodiscard]] QLabel* fileMetricsLabel() const { return m_fileMetricsLabel; }

    [[nodiscard]] QTextBrowser* systemInfoBrowser() const { return m_sysInfoBrowser; }
    [[nodiscard]] QPushButton* sysinfoRefreshButton() const { return m_sysRefreshBtn; }

    // Direct execution helpers for tests
    QString evaluateCalc(const QString& expr);
    QString readFile(const QString& relPath);
    QString refreshSystemInfo();

signals:
    void attachToolOutputToChat(const QString& toolName, const QString& content);

private slots:
    void onCalcEvaluateClicked();
    void onCalcHistoryDoubleClicked(QListWidgetItem* item);
    void onFileBrowseClicked();
    void onFileReadClicked();
    void onSendFileToChatClicked();
    void onRefreshSysInfoClicked();

private:
    void setupUi();
    QWidget* createCalculatorTab();
    QWidget* createFileReaderTab();
    QWidget* createSystemDiagnosticsTab();

    std::shared_ptr<BackendCoordinator> m_coordinator;

    // Calculator widgets
    QLineEdit* m_calcInput{nullptr};
    QPushButton* m_calcEvalBtn{nullptr};
    QLabel* m_calcResultLabel{nullptr};
    QListWidget* m_calcHistoryList{nullptr};

    // File Reader widgets
    QLineEdit* m_filePathEdit{nullptr};
    QPushButton* m_fileBrowseBtn{nullptr};
    QPushButton* m_fileReadBtn{nullptr};
    QPushButton* m_fileSendChatBtn{nullptr};
    QLabel* m_fileMetricsLabel{nullptr};
    QLabel* m_workspaceRootLabel{nullptr};
    QTextBrowser* m_fileContentBrowser{nullptr};
    QString m_explicitlyAuthorizedPath;
    QString m_workspaceRoot{"."};

    // Diagnostics widgets
    QTextBrowser* m_sysInfoBrowser{nullptr};
    QPushButton* m_sysRefreshBtn{nullptr};
};

} // namespace flex::ui

#endif // FLEX_UI_TOOLS_VIEW_HPP

