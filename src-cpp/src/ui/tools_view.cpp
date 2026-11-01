#include "flex/ui/tools_view.hpp"
#include "flex/ui/theme.hpp"
#include "tool_engine.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace flex::ui {

ToolsView::ToolsView(
    std::shared_ptr<BackendCoordinator> coordinator,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinator(std::move(coordinator))
{
    setupUi();
    refreshSystemInfo();
}

void ToolsView::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(14);

    // Header
    auto* headerBox = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(headerBox);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto* titleLbl = new QLabel("⚙️ Native Tools & Diagnostics", headerBox);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: 700; color: #F8FAFC;");

    auto* subLbl = new QLabel("Safe native C11 execution environment: Expression Calculator, Sandboxed File Reader, and System Diagnostics.", headerBox);
    subLbl->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextMuted));

    headerLayout->addWidget(titleLbl);
    headerLayout->addWidget(subLbl);
    mainLayout->addWidget(headerBox);

    auto* tabWidget = new QTabWidget(this);
    tabWidget->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; background-color: %2; border-radius: 8px; }"
        "QTabBar::tab { background: %3; color: %4; padding: 8px 16px; margin-right: 4px; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "QTabBar::tab:selected { background: %5; color: #F8FAFC; font-weight: bold; }"
    ).arg(Theme::Border, Theme::BgPanel, Theme::BgInput, Theme::TextMuted, Theme::AccentPurple));

    tabWidget->addTab(createCalculatorTab(), "🔢 Calculator");
    tabWidget->addTab(createFileReaderTab(), "📂 Workspace Reader");
    tabWidget->addTab(createSystemDiagnosticsTab(), "🖥️ System Diagnostics");

    mainLayout->addWidget(tabWidget, 1);
}

QWidget* ToolsView::createCalculatorTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* descLbl = new QLabel("Native C11 mathematical expression parser supporting decimals, parentheses, and precedence.", tab);
    descLbl->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextMuted));
    layout->addWidget(descLbl);

    auto* inputRow = new QHBoxLayout();
    m_calcInput = new QLineEdit(tab);
    m_calcInput->setPlaceholderText("Enter expression (e.g. (12.5 + 7.5) * 4 - 15 / 3)...");
    m_calcInput->setStyleSheet(QString(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px; padding: 8px; color: %3; font-size: 13px; }"
    ).arg(Theme::BgInput, Theme::Border, Theme::TextPrimary));
    connect(m_calcInput, &QLineEdit::returnPressed, this, &ToolsView::onCalcEvaluateClicked);
    inputRow->addWidget(m_calcInput, 1);

    m_calcEvalBtn = new QPushButton("Evaluate", tab);
    m_calcEvalBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 6px; padding: 8px 18px; font-weight: bold; }"
        "QPushButton:hover { background: %2; }"
    ).arg(Theme::SelectionBg, Theme::BorderFocus));
    connect(m_calcEvalBtn, &QPushButton::clicked, this, &ToolsView::onCalcEvaluateClicked);
    inputRow->addWidget(m_calcEvalBtn);
    layout->addLayout(inputRow);

    m_calcResultLabel = new QLabel("Result: Ready", tab);
    m_calcResultLabel->setStyleSheet(QString("font-size: 15px; font-weight: bold; color: %1; padding: 6px;").arg(Theme::TextAccentCyan));
    layout->addWidget(m_calcResultLabel);

    auto* histHeader = new QLabel("Calculation History (Double-click to reuse):", tab);
    histHeader->setStyleSheet("font-size: 12px; font-weight: bold; color: #E2E8F0;");
    layout->addWidget(histHeader);

    m_calcHistoryList = new QListWidget(tab);
    m_calcHistoryList->setStyleSheet(QString(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 6px; color: %3; font-family: monospace; }"
        "QListWidget::item { padding: 4px; }"
    ).arg(Theme::BgInput, Theme::Border, Theme::TextPrimary));
    connect(m_calcHistoryList, &QListWidget::itemDoubleClicked, this, &ToolsView::onCalcHistoryDoubleClicked);
    layout->addWidget(m_calcHistoryList, 1);

    return tab;
}

QWidget* ToolsView::createFileReaderTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* descLbl = new QLabel("Sandboxed Workspace Reader: Enforces traversal protection, validates extensions, and analyzes code metrics.", tab);
    descLbl->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextMuted));
    layout->addWidget(descLbl);

    auto* pathRow = new QHBoxLayout();
    m_filePathEdit = new QLineEdit(tab);
    m_filePathEdit->setPlaceholderText("Workspace relative path (e.g. CMakeLists.txt or docs/spec.md)...");
    m_filePathEdit->setStyleSheet(QString(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px; padding: 8px; color: %3; }"
    ).arg(Theme::BgInput, Theme::Border, Theme::TextPrimary));
    connect(m_filePathEdit, &QLineEdit::returnPressed, this, &ToolsView::onFileReadClicked);
    pathRow->addWidget(m_filePathEdit, 1);

    m_fileBrowseBtn = new QPushButton("Browse...", tab);
    m_fileBrowseBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 8px 14px; }"
    ).arg(Theme::BgInput, Theme::TextPrimary, Theme::Border));
    connect(m_fileBrowseBtn, &QPushButton::clicked, this, &ToolsView::onFileBrowseClicked);
    pathRow->addWidget(m_fileBrowseBtn);

    m_fileReadBtn = new QPushButton("Read File", tab);
    m_fileReadBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 6px; padding: 8px 16px; font-weight: bold; }"
    ).arg(Theme::SelectionBg));
    connect(m_fileReadBtn, &QPushButton::clicked, this, &ToolsView::onFileReadClicked);
    pathRow->addWidget(m_fileReadBtn);

    layout->addLayout(pathRow);

    m_fileMetricsLabel = new QLabel("Status: Ready", tab);
    m_fileMetricsLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::TextAccentViolet));
    layout->addWidget(m_fileMetricsLabel);

    m_fileContentBrowser = new QTextBrowser(tab);
    m_fileContentBrowser->setStyleSheet(QString(
        "QTextBrowser { background: %1; border: 1px solid %2; border-radius: 6px; color: %3; font-family: monospace; font-size: 12px; }"
    ).arg(Theme::BgInput, Theme::Border, Theme::TextPrimary));
    layout->addWidget(m_fileContentBrowser, 1);

    m_fileSendChatBtn = new QPushButton("💬 Attach File Content to Home Chat", tab);
    m_fileSendChatBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 6px; padding: 8px; font-weight: bold; }"
    ).arg(Theme::SelectionBg));
    connect(m_fileSendChatBtn, &QPushButton::clicked, this, &ToolsView::onSendFileToChatClicked);
    layout->addWidget(m_fileSendChatBtn);

    return tab;
}

QWidget* ToolsView::createSystemDiagnosticsTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* row = new QHBoxLayout();
    auto* descLbl = new QLabel("Live System & Platform Architecture Diagnostics:", tab);
    descLbl->setStyleSheet("font-size: 13px; font-weight: bold; color: #F8FAFC;");
    row->addWidget(descLbl);
    row->addStretch();

    m_sysRefreshBtn = new QPushButton("🔄 Refresh Diagnostics", tab);
    m_sysRefreshBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 6px; padding: 6px 14px; font-weight: bold; }"
    ).arg(Theme::SelectionBg));
    connect(m_sysRefreshBtn, &QPushButton::clicked, this, &ToolsView::onRefreshSysInfoClicked);
    row->addWidget(m_sysRefreshBtn);
    layout->addLayout(row);

    m_sysInfoBrowser = new QTextBrowser(tab);
    m_sysInfoBrowser->setStyleSheet(QString(
        "QTextBrowser { background: %1; border: 1px solid %2; border-radius: 6px; color: %3; font-family: monospace; font-size: 12px; padding: 10px; }"
    ).arg(Theme::BgInput, Theme::Border, Theme::TextPrimary));
    layout->addWidget(m_sysInfoBrowser, 1);

    return tab;
}

QString ToolsView::evaluateCalc(const QString& expr)
{
    char buf[FLEX_TOOL_MAX_RESULT_BUF];
    flex_tool_exec_meta_t meta;
    flex_tool_status_t st = flex_tool_calc_eval(expr.toUtf8().constData(), buf, sizeof(buf), &meta);

    QString resultStr = QString::fromUtf8(buf);
    if (m_calcResultLabel) {
        if (st == FLEX_TOOL_STATUS_OK) {
            m_calcResultLabel->setText(QString("✓ %1").arg(QString::fromUtf8(meta.result_summary)));
            m_calcResultLabel->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(Theme::StatusSuccess));
            if (m_calcHistoryList) {
                m_calcHistoryList->insertItem(0, QString("%1 = %2").arg(expr, QString::fromUtf8(meta.result_summary)));
            }
        } else {
            m_calcResultLabel->setText(QString("✕ %1").arg(resultStr));
            m_calcResultLabel->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(Theme::StatusError));
        }
    }
    return resultStr;
}

QString ToolsView::readFile(const QString& relPath)
{
    char buf[FLEX_TOOL_MAX_RESULT_BUF];
    flex_tool_exec_meta_t meta;
    flex_tool_status_t st = flex_tool_read_file(".", relPath.toUtf8().constData(), 0, buf, sizeof(buf), &meta);

    QString text = QString::fromUtf8(buf);
    if (m_fileMetricsLabel && m_fileContentBrowser) {
        if (st == FLEX_TOOL_STATUS_OK) {
            m_fileMetricsLabel->setText(QString("✓ %1").arg(QString::fromUtf8(meta.result_summary)));
            m_fileMetricsLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusSuccess));
            m_fileContentBrowser->setPlainText(text);
        } else {
            m_fileMetricsLabel->setText(QString("✕ %1").arg(text));
            m_fileMetricsLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusError));
            m_fileContentBrowser->setPlainText(text);
        }
    }
    return text;
}

QString ToolsView::refreshSystemInfo()
{
    char buf[FLEX_TOOL_MAX_RESULT_BUF];
    flex_tool_exec_meta_t meta;
    flex_tool_system_info("1.0.0", buf, sizeof(buf), &meta);

    QString info = QString::fromUtf8(buf);

    // Append subsystem health if coordinator is connected
    if (m_coordinator) {
        info += "\n\n--- Subsystem Live Diagnostics ---\n";
        int64_t convCount = (m_coordinator->storage()) ? m_coordinator->storage()->getConversationCount() : 0;
        int64_t msgCount = (m_coordinator->storage()) ? m_coordinator->storage()->getTotalMessageCount() : 0;
        info += QString("SQLite Storage: %1 conversations, %2 messages\n").arg(convCount).arg(msgCount);

        int64_t docCount = 0, chunkCount = 0;
        m_coordinator->getKnowledgeStats(&docCount, &chunkCount);
        info += QString("Knowledge Base: %1 documents, %2 chunks indexed\n").arg(docCount).arg(chunkCount);

        if (m_coordinator->ollamaClient()) {
            info += QString("Ollama Config: Endpoint %1 | Model %2\n")
                .arg(QString::fromStdString(m_coordinator->ollamaClient()->config().baseUrl))
                .arg(QString::fromStdString(m_coordinator->ollamaClient()->config().model));
        }
    }

    if (m_sysInfoBrowser) {
        m_sysInfoBrowser->setPlainText(info);
    }
    return info;
}

void ToolsView::onCalcEvaluateClicked()
{
    if (!m_calcInput) return;
    evaluateCalc(m_calcInput->text().trimmed());
}

void ToolsView::onCalcHistoryDoubleClicked(QListWidgetItem* item)
{
    if (!item || !m_calcInput) return;
    QString text = item->text();
    int idx = text.indexOf(" = ");
    if (idx != -1) {
        m_calcInput->setText(text.left(idx));
    }
}

void ToolsView::onFileBrowseClicked()
{
    QString file = QFileDialog::getOpenFileName(
        this,
        "Select Workspace File to Read",
        ".",
        "Supported Files (*.txt *.md *.c *.h *.cpp *.hpp *.json *.ini *.cmake);;All Files (*.*)"
    );
    if (!file.isEmpty() && m_filePathEdit) {
        m_filePathEdit->setText(file);
        onFileReadClicked();
    }
}

void ToolsView::onFileReadClicked()
{
    if (!m_filePathEdit) return;
    readFile(m_filePathEdit->text().trimmed());
}

void ToolsView::onSendFileToChatClicked()
{
    if (!m_fileContentBrowser || m_fileContentBrowser->toPlainText().isEmpty()) return;
    emit attachToolOutputToChat("workspace_read_file", m_fileContentBrowser->toPlainText());
}

void ToolsView::onRefreshSysInfoClicked()
{
    refreshSystemInfo();
}

} // namespace flex::ui
