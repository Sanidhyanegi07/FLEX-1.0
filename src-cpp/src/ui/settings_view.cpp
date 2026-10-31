#include "flex/ui/settings_view.hpp"
#include "flex/settings_persistence_service.hpp"
#include "flex/voice/whisper_model_resolver.hpp"
#include "flex/ui/theme.hpp"
#include "whisper_pipeline.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QProcessEnvironment>
#include <QScrollArea>
#include <QStandardItemModel>
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
    if (m_stt) {
        connect(m_stt.get(), &voice::SpeechToTextService::modelLoadStarted, this, [this](const QString& path) {
            if (m_whisperStatusLabel) {
                m_whisperStatusLabel->setText(QString("⏳ Loading model into memory: %1...").arg(QFileInfo(path).fileName()));
                m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusWarning));
            }
        });
        connect(m_stt.get(), &voice::SpeechToTextService::modelLoadFinished, this, [this](bool success, const QString& msg) {
            if (m_whisperStatusLabel) {
                if (success && m_stt) {
                    m_whisperStatusLabel->setText(QString("✓ Active in memory: %1")
                                                      .arg(QFileInfo(m_stt->loadedModelPath()).fileName()));
                    m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusSuccess));
                } else {
                    m_whisperStatusLabel->setText(QString("✕ Model load failed: %1").arg(msg));
                    m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusError));
                }
            }
        });
    }
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

    auto* modelRow = new QHBoxLayout();
    m_whisperModelCombo = new QComboBox(hwCard);
    m_whisperModelCombo->setCursor(Qt::PointingHandCursor);
    m_whisperModelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_whisperModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsView::onModelComboChanged);

    m_whisperRefreshBtn = new QPushButton("Refresh Models", hwCard);
    m_whisperRefreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_whisperRefreshBtn, &QPushButton::clicked, this, [this]() {
        refreshModelList(m_activeModelsDir);
    });

    modelRow->addWidget(m_whisperModelCombo, 1);
    modelRow->addWidget(m_whisperRefreshBtn);
    hwLayout->addRow("Speech-to-Text Model:", modelRow);

    m_whisperPathEdit = new QLineEdit(hwCard);
    m_whisperPathEdit->setPlaceholderText("Auto-resolved: %USERPROFILE%\\.flex\\models\\whisper\\ggml-tiny.en.bin");
    connect(m_whisperPathEdit, &QLineEdit::textEdited, this, &SettingsView::updateModelDetailsDisplay);
    hwLayout->addRow("Selected Model Path:", m_whisperPathEdit);

    m_whisperDetailsLabel = new QLabel("○ Model details not loaded", hwCard);
    m_whisperDetailsLabel->setWordWrap(true);
    m_whisperDetailsLabel->setStyleSheet("font-size: 11px; color: #CBD5E1; background: #0F172A; padding: 8px 12px; border-radius: 6px; border: 1px solid #334155;");
    hwLayout->addRow("Model Details:", m_whisperDetailsLabel);

    m_whisperStatusLabel = new QLabel("○ Engine status unverified", hwCard);
    m_whisperStatusLabel->setWordWrap(true);
    m_whisperStatusLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
    hwLayout->addRow("Engine Status:", m_whisperStatusLabel);

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

    if (m_whisperPathEdit) {
        m_whisperPathEdit->setText(QString::fromStdString(s.whisperModelPath));
    }
    refreshModelList();

    // Update Whisper status label truthfully
    if (m_whisperStatusLabel) {
        if (m_stt && m_stt->isModelLoaded()) {
            m_whisperStatusLabel->setText(QString("✓ Active in memory: %1")
                                              .arg(QFileInfo(m_stt->loadedModelPath()).fileName()));
            m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusSuccess));
        } else if (m_stt && m_stt->isModelLoading()) {
            m_whisperStatusLabel->setText("⏳ Loading model into memory...");
            m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusWarning));
        } else {
            auto whisperRes = voice::WhisperModelResolver::resolveModelPath(QString::fromStdString(s.whisperModelPath));
            if (whisperRes.isFound()) {
                m_whisperStatusLabel->setText(QString("○ Ready on disk (not loaded): %1")
                                                  .arg(QFileInfo(whisperRes.resolvedPath).fileName()));
                m_whisperStatusLabel->setStyleSheet("font-size: 11px; color: #94A3B8;");
            } else {
                m_whisperStatusLabel->setText("✕ Model not found. Run scripts/download_whisper_model.ps1");
                m_whisperStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::StatusError));
            }
        }
    }
}

void SettingsView::refreshModelList(const QString& customDir)
{
    if (!m_whisperModelCombo) return;

    if (!customDir.isEmpty()) {
        m_activeModelsDir = customDir;
    }

    QString envDir = QString::fromUtf8(qgetenv("FLEX_WHISPER_MODELS_DIR")).trimmed();
    QString targetDir = !m_activeModelsDir.isEmpty()
                            ? m_activeModelsDir
                            : (!envDir.isEmpty()
                                   ? envDir
                                   : voice::WhisperModelResolver::defaultModelsDirectory());

    QString previouslySelectedPath = m_whisperPathEdit ? m_whisperPathEdit->text().trimmed() : QString();

    m_whisperModelCombo->blockSignals(true);
    m_whisperModelCombo->clear();

    // Option 0: Auto-resolved Default Model
    m_whisperModelCombo->addItem(
        QString("Auto-detect Default Model (%1)").arg(voice::WhisperModelResolver::defaultModelFileName()),
        QString("")
    );

    auto discovered = voice::WhisperModelResolver::discoverInstalledModels(targetDir, previouslySelectedPath);
    auto* standardModel = qobject_cast<QStandardItemModel*>(m_whisperModelCombo->model());

    int selectedIdx = 0;
    int validCount = 0;

    for (const auto& info : discovered) {
        double sizeMb = static_cast<double>(info.fileSizeBytes) / (1024.0 * 1024.0);
        QString capStr = voice::WhisperModelResolver::whisperModelCapabilityToString(info.capability);
        QString statusStr = voice::WhisperModelResolver::modelValidityStatusToString(info.validity);

        QString itemText;
        if (info.isValid()) {
            validCount++;
            itemText = QString("%1 (%2 MB) [%3] - %4")
                           .arg(info.displayName)
                           .arg(QString::number(sizeMb, 'f', 1))
                           .arg(capStr)
                           .arg(statusStr);
            if (info.isDefaultModel) {
                itemText += " [Default]";
            }
        } else {
            itemText = QString("⚠ %1 (%2 MB) - %3")
                           .arg(info.fileName)
                           .arg(QString::number(sizeMb, 'f', 1))
                           .arg(statusStr);
        }

        m_whisperModelCombo->addItem(itemText, info.absolutePath);
        int itemRow = m_whisperModelCombo->count() - 1;

        if (!info.isValid() && standardModel) {
            auto* item = standardModel->item(itemRow);
            if (item) {
                item->setEnabled(false);
            }
        }

        if (!previouslySelectedPath.isEmpty()) {
            if (info.absolutePath.compare(previouslySelectedPath, Qt::CaseInsensitive) == 0 ||
                info.fileName.compare(previouslySelectedPath, Qt::CaseInsensitive) == 0) {
                selectedIdx = itemRow;
            }
        }
    }

    // If a custom path was configured but wasn't in targetDir
    if (!previouslySelectedPath.isEmpty() && selectedIdx == 0) {
        auto customInfo = voice::WhisperModelResolver::inspectModelFile(previouslySelectedPath);
        QString capStr = voice::WhisperModelResolver::whisperModelCapabilityToString(customInfo.capability);
        QString statusStr = voice::WhisperModelResolver::modelValidityStatusToString(customInfo.validity);
        QString customItemText;
        if (customInfo.isValid()) {
            customItemText = QString("[Custom] %1 [%2] - %3")
                                 .arg(customInfo.displayName)
                                 .arg(capStr)
                                 .arg(statusStr);
        } else {
            customItemText = QString("⚠ [Custom Missing/Invalid] %1 - %2")
                                 .arg(QFileInfo(previouslySelectedPath).fileName())
                                 .arg(statusStr);
        }
        m_whisperModelCombo->addItem(customItemText, customInfo.absolutePath.isEmpty() ? previouslySelectedPath : customInfo.absolutePath);
        selectedIdx = m_whisperModelCombo->count() - 1;

        if (!customInfo.isValid() && standardModel) {
            auto* item = standardModel->item(selectedIdx);
            if (item) {
                item->setEnabled(false);
            }
        }
    }

    m_whisperModelCombo->setCurrentIndex(selectedIdx);
    m_whisperModelCombo->blockSignals(false);

    updateModelDetailsDisplay();
}

void SettingsView::onModelComboChanged(int index)
{
    if (index < 0 || !m_whisperModelCombo) return;

    QString selectedPath = m_whisperModelCombo->currentData().toString();
    if (m_whisperPathEdit) {
        m_whisperPathEdit->setText(selectedPath);
    }
    updateModelDetailsDisplay();
}

void SettingsView::updateModelDetailsDisplay()
{
    if (!m_whisperDetailsLabel) return;

    QString currentPath = m_whisperPathEdit ? m_whisperPathEdit->text().trimmed() : QString();
    bool isAutoDefault = currentPath.isEmpty();
    QString inspectTarget = isAutoDefault ? voice::WhisperModelResolver::defaultModelPath() : currentPath;

    auto info = voice::WhisperModelResolver::inspectModelFile(inspectTarget);

    if (!info.isValid()) {
        QString warnMsg;
        if (info.validity == voice::ModelValidityStatus::Missing) {
            warnMsg = QString("⚠ Model file not found on disk:\n%1\nSaved preference is preserved. Run scripts/download_whisper_model.ps1 to download.")
                          .arg(inspectTarget);
        } else if (info.validity == voice::ModelValidityStatus::TooSmall) {
            warnMsg = QString("⚠ Model file is too small or incomplete (%1 bytes):\n%2\nMinimum 1MB required. Saved preference is preserved.")
                          .arg(info.fileSizeBytes)
                          .arg(inspectTarget);
        } else if (info.validity == voice::ModelValidityStatus::Unreadable) {
            warnMsg = QString("⚠ Model file is unreadable (permission denied):\n%1\nSaved preference is preserved.")
                          .arg(inspectTarget);
        } else {
            warnMsg = QString("⚠ Model file has invalid GGML format:\n%1\n%2")
                          .arg(inspectTarget)
                          .arg(info.diagnosticMessage);
        }
        m_whisperDetailsLabel->setText(warnMsg);
        m_whisperDetailsLabel->setStyleSheet(QString("font-size: 11px; color: %1; background: #0F172A; padding: 8px 12px; border-radius: 6px; border: 1px solid #7F1D1D;").arg(Theme::StatusWarning));
        return;
    }

    double sizeMb = static_cast<double>(info.fileSizeBytes) / (1024.0 * 1024.0);
    QString capStr = voice::WhisperModelResolver::whisperModelCapabilityToString(info.capability);
    QString defaultStr = info.isDefaultModel ? " [Default Model]" : "";
    QString selectedStr = isAutoDefault ? " [Auto-resolved Default]" : " [Explicitly Selected]";

    QString details = QString(
        "• Model: %1%2%3\n"
        "• Path: %4\n"
        "• Size: %5 MB (%6 bytes)\n"
        "• Capability Hint: %7 (Header classification; not proof of inference)\n"
        "• Status: Valid GGML Binary Header"
    ).arg(info.displayName)
     .arg(defaultStr)
     .arg(selectedStr)
     .arg(info.absolutePath)
     .arg(QString::number(sizeMb, 'f', 1))
     .arg(info.fileSizeBytes)
     .arg(capStr);

    m_whisperDetailsLabel->setText(details);
    m_whisperDetailsLabel->setStyleSheet("font-size: 11px; color: #CBD5E1; background: #0F172A; padding: 8px 12px; border-radius: 6px; border: 1px solid #334155;");
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

    QString chosenWhisperPath;
    if (m_whisperPathEdit) {
        chosenWhisperPath = m_whisperPathEdit->text().trimmed();
    }

    // Validate model selection before saving via C core policy
    char diagBuf[256] = {0};
    QByteArray pathBytes = chosenWhisperPath.toLocal8Bit();
    if (!flex_whisper_validate_model_selection(pathBytes.constData(), diagBuf, sizeof(diagBuf))) {
        m_saveStatusLabel->setText(QString("✕ Save Blocked: Selected model file is invalid (%1).")
                                       .arg(QString::fromUtf8(diagBuf)));
        m_saveStatusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusError));
        return;
    }

    newSettings.whisperModelPath = chosenWhisperPath.toStdString();

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

    bool sttBusy = false;
    if (m_stt) {
        if (m_stt->isBusy()) {
            sttBusy = true;
        } else {
            m_stt->loadModel(QString::fromStdString(newSettings.whisperModelPath));
        }

        if (!newSettings.whisperLanguage.empty()) {
            m_stt->setLanguage(QString::fromStdString(newSettings.whisperLanguage));
        }
        if (!newSettings.whisperPrompt.empty()) {
            m_stt->setInitialPrompt(QString::fromStdString(newSettings.whisperPrompt));
        }
    }

    if (sttBusy) {
        m_saveStatusLabel->setText("✓ Settings saved to disk. Speech-to-text service is currently transcribing; new model will take effect next session.");
        m_saveStatusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusWarning));
    } else {
        m_saveStatusLabel->setText("✓ Settings saved successfully to configuration file.");
        m_saveStatusLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(Theme::StatusSuccess));
    }

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
    if (m_whisperModelCombo) m_whisperModelCombo->setCurrentIndex(0);
    if (m_whisperPathEdit) m_whisperPathEdit->clear();
    updateModelDetailsDisplay();

    onSaveClicked();
}

} // namespace flex::ui
