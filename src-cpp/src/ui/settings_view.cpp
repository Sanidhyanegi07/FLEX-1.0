#include "flex/ui/settings_view.hpp"
#include "flex/ui/theme.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>

namespace flex::ui {

SettingsView::SettingsView(
    std::shared_ptr<BackendCoordinator> coordinator,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinator(std::move(coordinator))
{
    setupUi();
    loadCurrentConfig();
}

void SettingsView::setupUi()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");

    auto* container = new QWidget();
    container->setStyleSheet("background-color: transparent;");
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(36, 28, 36, 28);
    layout->setSpacing(18);

    // Header
    auto* headerBox = new QWidget(container);
    auto* headerLayout = new QVBoxLayout(headerBox);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto* titleLbl = new QLabel("🛠️ Settings & Ollama Configuration", headerBox);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: 700; color: #F8FAFC;");

    auto* subLbl = new QLabel("Configure local Ollama daemon connection parameters, active models, and inference limits.", headerBox);
    subLbl->setStyleSheet("font-size: 12px; color: #94A3B8;");

    headerLayout->addWidget(titleLbl);
    headerLayout->addWidget(subLbl);
    layout->addWidget(headerBox);

    // Form Card
    auto* formCard = new QFrame(container);
    formCard->setObjectName("UserProfileCard");
    auto* formLayout = new QFormLayout(formCard);
    formLayout->setContentsMargins(20, 20, 20, 20);
    formLayout->setSpacing(14);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // 1. Endpoint
    auto* endRow = new QHBoxLayout();
    m_endpointEdit = new QLineEdit("http://127.0.0.1:11434", formCard);
    m_testBtn = new QPushButton("Test Connection", formCard);
    m_testBtn->setCursor(Qt::PointingHandCursor);
    connect(m_testBtn, &QPushButton::clicked, this, &SettingsView::onTestConnectionClicked);

    endRow->addWidget(m_endpointEdit, 1);
    endRow->addWidget(m_testBtn);
    formLayout->addRow("Daemon Base URL:", endRow);

    m_testResultLabel = new QLabel("○ Connection not yet tested", formCard);
    m_testResultLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
    formLayout->addRow("", m_testResultLabel);

    // 2. Model Name
    m_modelEdit = new QLineEdit("qwen2.5-coder:3b", formCard);
    formLayout->addRow("Active Model Name:", m_modelEdit);

    // 3. Temperature
    m_tempSpin = new QDoubleSpinBox(formCard);
    m_tempSpin->setRange(0.0, 2.0);
    m_tempSpin->setSingleStep(0.05);
    m_tempSpin->setValue(0.7);
    formLayout->addRow("Inference Temperature:", m_tempSpin);

    // 4. Context Window
    m_numCtxSpin = new QSpinBox(formCard);
    m_numCtxSpin->setRange(512, 131072);
    m_numCtxSpin->setSingleStep(1024);
    m_numCtxSpin->setValue(4096);
    formLayout->addRow("Context Window (tokens):", m_numCtxSpin);

    // 5. Timeout
    m_timeoutSpin = new QSpinBox(formCard);
    m_timeoutSpin->setRange(5, 300);
    m_timeoutSpin->setValue(60);
    formLayout->addRow("Request Timeout (s):", m_timeoutSpin);

    layout->addWidget(formCard);

    // Button Row
    auto* btnRow = new QHBoxLayout();
    m_saveBtn = new QPushButton("Save Settings", container);
    m_saveBtn->setObjectName("PrimaryButton");
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsView::onSaveClicked);

    m_resetBtn = new QPushButton("Reset to Defaults", container);
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsView::onResetClicked);

    btnRow->addWidget(m_saveBtn);
    btnRow->addWidget(m_resetBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    layout->addStretch();
    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);
}

void SettingsView::loadCurrentConfig()
{
    if (!m_coordinator || !m_coordinator->ollamaClient()) {
        return;
    }
    const auto& cfg = m_coordinator->ollamaClient()->config();
    m_endpointEdit->setText(QString::fromStdString(cfg.baseUrl));
    m_modelEdit->setText(QString::fromStdString(cfg.model));
    m_tempSpin->setValue(cfg.options.temperature);
    m_numCtxSpin->setValue(cfg.options.numCtx.value_or(4096));
    m_timeoutSpin->setValue(static_cast<int>(cfg.timeoutSeconds));
}

void SettingsView::onTestConnectionClicked()
{
    if (!m_coordinator) {
        return;
    }

    m_testResultLabel->setText("⏳ Testing connection to daemon...");
    m_testResultLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusWarning));

    auto coord = m_coordinator;
    auto* thread = QThread::create([this, coord]() {
        bool ok = false;
        try {
            ok = coord->checkHealthSync(2.0);
        } catch (...) {
            ok = false;
        }

        QMetaObject::invokeMethod(this, [this, ok]() {
            if (ok) {
                m_testResultLabel->setText("✓ Connection successful (Daemon reachable)");
                m_testResultLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusSuccess));
            } else {
                m_testResultLabel->setText("✕ Connection failed (Make sure `ollama serve` is running)");
                m_testResultLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusError));
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SettingsView::onSaveClicked()
{
    if (!m_coordinator || !m_coordinator->ollamaClient()) {
        return;
    }

    OllamaConfig cfg;
    cfg.baseUrl = m_endpointEdit->text().trimmed().toStdString();
    cfg.model = m_modelEdit->text().trimmed().toStdString();
    cfg.options.temperature = m_tempSpin->value();
    cfg.options.numCtx = m_numCtxSpin->value();
    cfg.timeoutSeconds = m_timeoutSpin->value();

    m_coordinator->ollamaClient()->setConfig(cfg);
    emit settingsUpdated();

    QMessageBox::information(this, "Settings Saved", "Runtime inference parameters updated successfully.");
}

void SettingsView::onResetClicked()
{
    m_endpointEdit->setText("http://127.0.0.1:11434");
    m_modelEdit->setText("qwen2.5-coder:3b");
    m_tempSpin->setValue(0.7);
    m_numCtxSpin->setValue(4096);
    m_timeoutSpin->setValue(60);
    onSaveClicked();
}

} // namespace flex::ui
