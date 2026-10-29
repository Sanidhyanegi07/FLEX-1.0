#include "flex/ui/settings_view.hpp"
#include "flex/settings_persistence_service.hpp"
#include "flex/voice/whisper_model_resolver.hpp"
#include "flex/ui/theme.hpp"

#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>

namespace flex::ui {

SettingsView::SettingsView(
    std::shared_ptr<BackendCoordinator> coordinator,
    QWidget* parent)
    : SettingsView(std::move(coordinator), nullptr, nullptr, nullptr, parent)
{
}

SettingsView::SettingsView(
    std::shared_ptr<BackendCoordinator> coordinator,
    std::shared_ptr<voice::AudioCaptureService> audioCapture,
    std::shared_ptr<voice::TextToSpeechService> tts,
    std::shared_ptr<voice::SpeechToTextService> stt,
    QWidget* parent)
    : QWidget(parent)
    , m_coordinator(std::move(coordinator))
    , m_audioCapture(std::move(audioCapture))
    , m_tts(std::move(tts))
    , m_stt(std::move(stt))
{
    setupUi();
    loadCurrentConfig();
}

void SettingsView::setAudioCaptureService(std::shared_ptr<voice::AudioCaptureService> service)
{
    m_audioCapture = std::move(service);
    refreshDeviceList();
}

void SettingsView::setTextToSpeechService(std::shared_ptr<voice::TextToSpeechService> service)
{
    m_tts = std::move(service);
    if (m_tts && m_ttsRateSpin && m_ttsVolumeSpin) {
        m_ttsRateSpin->setValue(m_tts->rate());
        m_ttsVolumeSpin->setValue(m_tts->volume());
    }
}

void SettingsView::setSpeechToTextService(std::shared_ptr<voice::SpeechToTextService> service)
{
    m_stt = std::move(service);
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
    layout->setSpacing(20);

    // Section Header
    auto* headerBox = new QWidget(container);
    auto* headerLayout = new QVBoxLayout(headerBox);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    auto* titleLbl = new QLabel("🛠️ Settings & Hardware Preferences", headerBox);
    titleLbl->setStyleSheet("font-size: 18px; font-weight: 700; color: #F8FAFC;");

    auto* subLbl = new QLabel("Configure Ollama daemon parameters, microphone hardware, SAPI speech rate/volume, and persistent storage.", headerBox);
    subLbl->setStyleSheet("font-size: 12px; color: #94A3B8;");

    headerLayout->addWidget(titleLbl);
    headerLayout->addWidget(subLbl);
    layout->addWidget(headerBox);

    // Card 1: Ollama Inference Parameters
    auto* formCard = new QFrame(container);
    formCard->setObjectName("UserProfileCard");
    auto* formLayout = new QFormLayout(formCard);
    formLayout->setContentsMargins(20, 20, 20, 20);
    formLayout->setSpacing(14);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

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

    m_modelEdit = new QLineEdit("qwen2.5-coder:3b", formCard);
    formLayout->addRow("Active Model Name:", m_modelEdit);

    m_tempSpin = new QDoubleSpinBox(formCard);
    m_tempSpin->setRange(0.0, 2.0);
    m_tempSpin->setSingleStep(0.05);
    m_tempSpin->setValue(0.2);
    formLayout->addRow("Inference Temperature:", m_tempSpin);

    m_numCtxSpin = new QSpinBox(formCard);
    m_numCtxSpin->setRange(256, 131072);
    m_numCtxSpin->setSingleStep(1024);
    m_numCtxSpin->setValue(4096);
    formLayout->addRow("Context Window (tokens):", m_numCtxSpin);

    m_timeoutSpin = new QSpinBox(formCard);
    m_timeoutSpin->setRange(1, 300);
    m_timeoutSpin->setValue(30);
    formLayout->addRow("Request Timeout (s):", m_timeoutSpin);

    layout->addWidget(formCard);

    // Card 2: Audio & Speech Hardware Preferences
    auto* hwCard = new QFrame(container);
    hwCard->setObjectName("UserProfileCard");
    auto* hwLayout = new QFormLayout(hwCard);
    hwLayout->setContentsMargins(20, 20, 20, 20);
    hwLayout->setSpacing(14);
    hwLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_micCombo = new QComboBox(hwCard);
    m_micCombo->setCursor(Qt::PointingHandCursor);
    hwLayout->addRow("Microphone Input Device:", m_micCombo);

    m_micStatusLabel = new QLabel("Active: Default Audio Device", hwCard);
    m_micStatusLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
    hwLayout->addRow("", m_micStatusLabel);

    m_ttsRateSpin = new QSpinBox(hwCard);
    m_ttsRateSpin->setRange(-10, 10);
    m_ttsRateSpin->setValue(0);
    hwLayout->addRow("TTS Speech Rate (-10..+10):", m_ttsRateSpin);

    m_ttsVolumeSpin = new QSpinBox(hwCard);
    m_ttsVolumeSpin->setRange(0, 100);
    m_ttsVolumeSpin->setValue(100);
    hwLayout->addRow("TTS Speech Volume (0..100%):", m_ttsVolumeSpin);

    m_whisperPathEdit = new QLineEdit(hwCard);
    m_whisperPathEdit->setPlaceholderText("Auto-resolved: %USERPROFILE%\\.flex\\models\\whisper\\ggml-tiny.en.bin");
    hwLayout->addRow("Whisper Speech Model Path:", m_whisperPathEdit);

    m_whisperStatusLabel = new QLabel("○ Model status unverified", hwCard);
    m_whisperStatusLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
    hwLayout->addRow("", m_whisperStatusLabel);

    layout->addWidget(hwCard);

    // Action Row & Non-blocking Status Banner
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

    m_saveStatusLabel = new QLabel("", container);
    m_saveStatusLabel->setStyleSheet("font-size: 12px; font-weight: 600;");
    layout->addWidget(m_saveStatusLabel);

    layout->addStretch();
    scrollArea->setWidget(container);
    outerLayout->addWidget(scrollArea);

    refreshDeviceList();
}

void SettingsView::refreshDeviceList()
{
    if (!m_micCombo) return;

    QString currentSel = m_micCombo->currentData().toString();
    m_micCombo->clear();
    m_micCombo->addItem("System Default Audio Input", QString(""));

    if (m_audioCapture) {
        auto devices = m_audioCapture->availableInputDevices();
        for (const auto& dev : devices) {
            QString label = QString::fromStdString(dev.name);
            if (dev.isDefault) {
                label += " (Default)";
            }
            m_micCombo->addItem(label, QString::fromStdString(dev.id));
        }

        // Restore selection if matching
        if (!currentSel.isEmpty()) {
            int idx = m_micCombo->findData(currentSel);
            if (idx >= 0) {
                m_micCombo->setCurrentIndex(idx);
            }
        }

        std::string activeName = m_audioCapture->activeDeviceName();
        if (m_audioCapture->isFallbackActive()) {
            m_micStatusLabel->setText(QString("⚠ Fallback Active: %1 (Preferred device unavailable)")
                                          .arg(QString::fromStdString(activeName)));
            m_micStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusWarning));
        } else {
            m_micStatusLabel->setText(QString("Active Device: %1").arg(QString::fromStdString(activeName)));
            m_micStatusLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
        }
    }
}

void SettingsView::loadCurrentConfig()
{
    auto loadRes = SettingsPersistenceService::loadSettings();
    const auto& s = loadRes.settings;

    if (m_endpointEdit) m_endpointEdit->setText(QString::fromStdString(s.ollamaEndpoint));
    if (m_modelEdit) m_modelEdit->setText(QString::fromStdString(s.ollamaModel));
    if (m_tempSpin) m_tempSpin->setValue(s.ollamaTemperature);
    if (m_numCtxSpin) m_numCtxSpin->setValue(s.ollamaNumCtx);
    if (m_timeoutSpin) m_timeoutSpin->setValue(s.ollamaTimeout);

    refreshDeviceList();
    if (m_micCombo && !s.audioInputDevice.empty()) {
        int idx = m_micCombo->findData(QString::fromStdString(s.audioInputDevice));
        if (idx < 0) {
            idx = m_micCombo->findText(QString::fromStdString(s.audioInputDevice), Qt::MatchContains);
        }
        if (idx >= 0) {
            m_micCombo->setCurrentIndex(idx);
        }
    }

    if (m_ttsRateSpin) m_ttsRateSpin->setValue(s.ttsRate);
    if (m_ttsVolumeSpin) m_ttsVolumeSpin->setValue(s.ttsVolume);
    if (m_whisperPathEdit) m_whisperPathEdit->setText(QString::fromStdString(s.whisperModelPath));

    // Update Whisper status label truthfully
    if (m_whisperStatusLabel) {
        if (m_stt && m_stt->isModelLoaded()) {
            m_whisperStatusLabel->setText(QString("✓ Model loaded in memory: %1")
                                              .arg(QFileInfo(m_stt->loadedModelPath()).fileName()));
            m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusSuccess));
        } else {
            auto whisperRes = voice::WhisperModelResolver::resolveModelPath(QString::fromStdString(s.whisperModelPath));
            if (whisperRes.isFound()) {
                m_whisperStatusLabel->setText(QString("✓ Model ready on disk: %1")
                                                  .arg(QFileInfo(whisperRes.resolvedPath).fileName()));
                m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusSuccess));
            } else {
                m_whisperStatusLabel->setText("✕ Model not found. Run scripts/download_whisper_model.ps1");
                m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusError));
            }
        }
    }
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
    AppSettings newSettings;
    newSettings.ollamaEndpoint = m_endpointEdit->text().trimmed().toStdString();
    newSettings.ollamaModel = m_modelEdit->text().trimmed().toStdString();
    newSettings.ollamaTemperature = m_tempSpin->value();
    newSettings.ollamaNumCtx = m_numCtxSpin->value();
    newSettings.ollamaTimeout = m_timeoutSpin->value();

    if (m_micCombo) {
        QString devData = m_micCombo->currentData().toString();
        if (devData.isEmpty() && m_micCombo->currentIndex() > 0) {
            newSettings.audioInputDevice = m_micCombo->currentText().toStdString();
        } else {
            newSettings.audioInputDevice = devData.toStdString();
        }
    }

    if (m_ttsRateSpin) newSettings.ttsRate = m_ttsRateSpin->value();
    if (m_ttsVolumeSpin) newSettings.ttsVolume = m_ttsVolumeSpin->value();
    if (m_whisperPathEdit) newSettings.whisperModelPath = m_whisperPathEdit->text().trimmed().toStdString();

    auto saveRes = SettingsPersistenceService::saveSettings(newSettings);
    if (!saveRes.success) {
        m_saveStatusLabel->setText(QString("✕ Save Failed: %1").arg(QString::fromStdString(saveRes.errorMessage)));
        m_saveStatusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusError));
        return;
    }

    // Apply to runtime services
    if (m_coordinator && m_coordinator->ollamaClient()) {
        OllamaConfig cfg;
        cfg.baseUrl = newSettings.ollamaEndpoint;
        cfg.model = newSettings.ollamaModel;
        cfg.options.temperature = newSettings.ollamaTemperature;
        cfg.options.numCtx = newSettings.ollamaNumCtx;
        cfg.timeoutSeconds = newSettings.ollamaTimeout;
        m_coordinator->ollamaClient()->setConfig(cfg);
    }

    if (m_audioCapture) {
        m_audioCapture->setPreferredDeviceId(newSettings.audioInputDevice);
    }

    if (m_tts) {
        m_tts->setRate(newSettings.ttsRate);
        m_tts->setVolume(newSettings.ttsVolume);
    }

    if (m_stt && !newSettings.whisperModelPath.empty()) {
        m_stt->loadModel(QString::fromStdString(newSettings.whisperModelPath));
    }

    m_saveStatusLabel->setText("✓ Settings saved successfully to configuration file.");
    m_saveStatusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusSuccess));

    refreshDeviceList();
    emit settingsUpdated();
}

void SettingsView::onResetClicked()
{
    AppSettings defaults;
    m_endpointEdit->setText(QString::fromStdString(defaults.ollamaEndpoint));
    m_modelEdit->setText(QString::fromStdString(defaults.ollamaModel));
    m_tempSpin->setValue(defaults.ollamaTemperature);
    m_numCtxSpin->setValue(defaults.ollamaNumCtx);
    m_timeoutSpin->setValue(defaults.ollamaTimeout);
    if (m_micCombo) m_micCombo->setCurrentIndex(0);
    if (m_ttsRateSpin) m_ttsRateSpin->setValue(defaults.ttsRate);
    if (m_ttsVolumeSpin) m_ttsVolumeSpin->setValue(defaults.ttsVolume);
    if (m_whisperPathEdit) m_whisperPathEdit->clear();

    onSaveClicked();
}

} // namespace flex::ui
