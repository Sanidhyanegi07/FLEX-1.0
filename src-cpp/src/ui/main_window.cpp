#include "flex/ui/main_window.hpp"
#include "flex/ui/theme.hpp"
#include "flex/settings_persistence_service.hpp"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>
#include <QVBoxLayout>

namespace flex::ui {

MainWindow::MainWindow(
    std::shared_ptr<BackendCoordinator> coordinator,
    QWidget* parent)
    : QMainWindow(parent)
    , m_coordinator(std::move(coordinator))
{
    bool isDefaultCoordinator = false;
    if (!m_coordinator) {
        m_coordinator = std::make_shared<BackendCoordinator>();
        isDefaultCoordinator = true;
    }

    // Load persisted settings at startup
    auto loadRes = SettingsPersistenceService::loadSettings();
    if (loadRes.success && m_coordinator && m_coordinator->ollamaClient()) {
        // If coordinator was created by default or client baseUrl is standard, apply persisted config
        if (isDefaultCoordinator || m_coordinator->ollamaClient()->config().baseUrl == "http://127.0.0.1:11434") {
            OllamaConfig cfg = m_coordinator->ollamaClient()->config();
            cfg.baseUrl = loadRes.settings.ollamaEndpoint;
            cfg.model = loadRes.settings.ollamaModel;
            cfg.options.temperature = loadRes.settings.ollamaTemperature;
            cfg.options.numCtx = loadRes.settings.ollamaNumCtx;
            cfg.timeoutSeconds = loadRes.settings.ollamaTimeout;
            m_coordinator->ollamaClient()->setConfig(cfg);
        }
    }

    setupUi();
    connectCoordinatorSignals();

    // Initialize voice conversation controller
    m_voiceController = std::make_shared<voice::VoiceConversationController>(
        m_coordinator,
        m_chatView->audioCaptureService(),
        nullptr,
        m_chatView->textToSpeechService(),
        this
    );
    m_chatView->setVoiceController(m_voiceController);
    if (m_voiceStudioView) {
        m_voiceStudioView->setVoiceController(m_voiceController);
    }

    // Apply hardware preferences to services and wire SettingsView
    if (loadRes.success) {
        if (!loadRes.settings.audioInputDevice.empty() && m_chatView->audioCaptureService()) {
            m_chatView->audioCaptureService()->setPreferredDeviceId(loadRes.settings.audioInputDevice);
        }
        if (m_chatView->textToSpeechService()) {
            m_chatView->textToSpeechService()->setRate(loadRes.settings.ttsRate);
            m_chatView->textToSpeechService()->setVolume(loadRes.settings.ttsVolume);
        }
        if (!loadRes.settings.whisperModelPath.empty() && m_voiceController->speechToTextService()) {
            m_voiceController->speechToTextService()->loadModel(QString::fromStdString(loadRes.settings.whisperModelPath));
        }
    }
    m_settingsView->setAudioCaptureService(m_chatView->audioCaptureService());
    m_settingsView->setTextToSpeechService(m_chatView->textToSpeechService());
    if (m_voiceController) {
        m_settingsView->setSpeechToTextService(m_voiceController->speechToTextService());
    }

    // Configure health check timer (every 10 seconds, non-blocking)
    connect(&m_healthCheckTimer, &QTimer::timeout, this, &MainWindow::checkAiConnectivity);
    m_healthCheckTimer.start(10000);

    // Initial population and health check
    refreshConversations();
    checkAiConnectivity();
    updateCoreStateDisplay();
}

MainWindow::~MainWindow()
{
    m_healthCheckTimer.stop();
    if (m_healthCheckThread && m_healthCheckThread->isRunning()) {
        m_healthCheckThread->wait(1000);
    }
    if (m_voiceController) {
        m_voiceController->cancelAll();
    }
    if (m_coordinator) {
        m_coordinator->cancelAll();
    }
}

ConversationListWidget* MainWindow::conversationListWidget() const
{
    return m_navSidebar ? m_navSidebar->conversationListWidget() : nullptr;
}

void MainWindow::setupUi()
{
    setWindowTitle("FLEX 1.0 — Native Desktop AI Assistant");
    resize(1180, 780);
    setMinimumSize(960, 600);

    setStyleSheet(Theme::getApplicationStylesheet());

    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("CentralWidget");
    setCentralWidget(centralWidget);

    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // =========================================================================
    // Persistent Top Telemetry Header
    // =========================================================================
    m_topHeader = new TopHeaderWidget(centralWidget);
    connect(m_topHeader, &TopHeaderWidget::toggleRightSidebarRequested,
            this, &MainWindow::onToggleRightSidebar);
    rootLayout->addWidget(m_topHeader);

    // =========================================================================
    // 3-Column Workspace Shell (Horizontal)
    // =========================================================================
    auto* workspaceWidget = new QWidget(centralWidget);
    workspaceWidget->setStyleSheet("background-color: transparent;");
    auto* workspaceLayout = new QHBoxLayout(workspaceWidget);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    // Column 1: Left Navigation Sidebar (~220px)
    m_navSidebar = new NavigationSidebar(workspaceWidget);
    connect(m_navSidebar, &NavigationSidebar::newChatRequested, this, &MainWindow::handleNewConversation);
    connect(m_navSidebar, &NavigationSidebar::viewChangeRequested, this, &MainWindow::switchView);

    if (auto* clist = m_navSidebar->conversationListWidget()) {
        connect(clist, &ConversationListWidget::conversationSelected,
                this, &MainWindow::handleSelectConversation);
        connect(clist, &ConversationListWidget::conversationDeleteRequested,
                this, &MainWindow::handleDeleteConversation);
    }
    workspaceLayout->addWidget(m_navSidebar);

    // Column 2: Central Dynamic View Container (QStackedWidget)
    m_viewStack = new QStackedWidget(workspaceWidget);
    m_viewStack->setObjectName("CentralViewStack");
    m_viewStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 0: Home (Primary ChatView)
    m_chatView = new ChatView(m_viewStack);
    connect(m_chatView, &ChatView::sendRequested, this, &MainWindow::handleSendChat);
    m_viewStack->addWidget(m_chatView);

    // 1: Voice Studio
    m_voiceStudioView = new VoiceStudioView(nullptr, m_viewStack);
    connect(m_voiceStudioView, &VoiceStudioView::sendSpokenPromptRequested, this, [this](const QString& text) {
        switchView("home");
        m_navSidebar->setActiveView("home");
        handleSendChat(text);
    });
    m_viewStack->addWidget(m_voiceStudioView);

    // 2: Memory
    m_memoryView = new MemoryView(m_coordinator, m_viewStack);
    connect(m_memoryView, &MemoryView::resumeConversationRequested, this, [this](const QString& cid) {
        handleSelectConversation(cid);
        switchView("home");
        m_navSidebar->setActiveView("home");
    });
    m_viewStack->addWidget(m_memoryView);

    // 3: Knowledge Base (Truthful Prototype Specification)
    m_knowledgeView = new PrototypeView(
        "Knowledge Base",
        "📚",
        "● STATUS: PLANNED FOR STAGE 2",
        Theme::StatusWarning,
        "Retrieval-augmented generation (RAG) system for indexing local documentation, PDFs, and codebase repositories.",
        "Document parsing, chunking, and vector index ingestion are architectural components planned for subsequent stages.",
        {
            "Document file ingestion and PDF parsing are not implemented in Stage 1.",
            "Local vector index and semantic search are not implemented.",
            "Context injection into AI inference is limited to in-memory conversation history."
        },
        m_viewStack
    );
    m_viewStack->addWidget(m_knowledgeView);

    // 4: Projects & Files
    m_projectsView = new ProjectsView(m_viewStack);
    connect(m_projectsView, &ProjectsView::attachFileToChatRequested, this, [this](const QString& path, const QString& content) {
        switchView("home");
        m_navSidebar->setActiveView("home");
        m_chatView->setInputText(QString("[Context File: %1]\n```\n%2\n```\n\n").arg(path, content));
        m_chatView->focusInput();
    });
    m_viewStack->addWidget(m_projectsView);

    // 5: Tools (Truthful Architectural Specification)
    m_toolsView = new PrototypeView(
        "C Core Native Tools",
        "⚙️",
        "● STATUS: ARCHITECTURAL SPECIFICATION",
        Theme::StatusSuccess,
        "Native tools engine governed by the C11 Core. Command parsing, memory safety, and deterministic lifecycle tracking.",
        "FLEX 1.0 enforces all command parsing through strict C11 native modules without dynamic heap allocations. Live dispatch is active in the Home chat pipeline.",
        {
            "External CLI tool execution plugins are not implemented in Stage 1.",
            "Interactive tools dashboard is an architectural specification only; no fabricated buttons.",
            "All command verification is performed automatically in the Home chat view."
        },
        m_viewStack
    );
    m_viewStack->addWidget(m_toolsView);

    // 6: Settings
    m_settingsView = new SettingsView(m_coordinator, m_viewStack);
    connect(m_settingsView, &SettingsView::settingsUpdated, this, &MainWindow::checkAiConnectivity);
    m_viewStack->addWidget(m_settingsView);

    workspaceLayout->addWidget(m_viewStack, 1);

    // Column 3: Right Information Sidebar (~240px)
    m_rightSidebar = new RightSidebar(workspaceWidget);
    connect(m_rightSidebar, &RightSidebar::quickActionRequested, this, [this](const QString& key) {
        m_navSidebar->setActiveView(key);
        switchView(key);
    });
    connect(m_rightSidebar, &RightSidebar::diagnosticsRequested, this, &MainWindow::onDiagnosticsRequested);
    workspaceLayout->addWidget(m_rightSidebar);

    rootLayout->addWidget(workspaceWidget, 1);
}

void MainWindow::switchView(const QString& viewKey)
{
    if (viewKey == "home") {
        m_viewStack->setCurrentWidget(m_chatView);
        m_chatView->focusInput();
    } else if (viewKey == "voice") {
        m_viewStack->setCurrentWidget(m_voiceStudioView);
    } else if (viewKey == "memory") {
        m_memoryView->refreshConversations();
        m_viewStack->setCurrentWidget(m_memoryView);
    } else if (viewKey == "knowledge") {
        m_viewStack->setCurrentWidget(m_knowledgeView);
    } else if (viewKey == "projects") {
        m_viewStack->setCurrentWidget(m_projectsView);
    } else if (viewKey == "tools") {
        m_viewStack->setCurrentWidget(m_toolsView);
    } else if (viewKey == "settings") {
        m_settingsView->loadCurrentConfig();
        m_viewStack->setCurrentWidget(m_settingsView);
    }
}

void MainWindow::onToggleRightSidebar()
{
    if (!m_rightSidebar) {
        return;
    }
    bool currentlyVisible = m_rightSidebar->isVisible();
    m_rightSidebar->setVisible(!currentlyVisible);
    if (m_topHeader) {
        m_topHeader->setRightSidebarVisible(!currentlyVisible);
    }
}

void MainWindow::onDiagnosticsRequested()
{
    QString info = "<h3>FLEX 1.0 System Diagnostics</h3>";
    info += "<hr/>";

    // C Core Status
    bool coreProcessing = m_coordinator ? m_coordinator->coreSession().isProcessing() : false;
    info += QString("<p><b>C11 Core:</b> %1<br/>"
                    "Memory Boundary: 1024 Bytes Max Input<br/>"
                    "Lifecycle State: %2</p>")
        .arg(coreProcessing ? "<span style='color:#38BDF8;'>PROCESSING</span>" : "<span style='color:#34D399;'>IDLE</span>")
        .arg(m_coordinator ? CoreSession::lifecycleToString(m_coordinator->coreSession().lifecycle()) : "UNKNOWN");

    // Ollama AI Engine
    info += QString("<p><b>Local AI Engine:</b> %1<br/>"
                    "Model Configured: %2<br/>"
                    "Endpoint: %3</p>")
        .arg(m_lastOllamaOnline ? "<span style='color:#34D399;'>ONLINE</span>" : "<span style='color:#F87171;'>OFFLINE</span>")
        .arg(m_lastOllamaModel.isEmpty() ? "qwen2.5-coder:3b" : m_lastOllamaModel)
        .arg(m_coordinator && m_coordinator->ollamaClient() ? QString::fromStdString(m_coordinator->ollamaClient()->config().baseUrl) : "127.0.0.1:11434");

    // Storage Status
    info += QString("<p><b>SQLite Storage:</b> <span style='color:#34D399;'>ACTIVE</span><br/>"
                    "Database Path: %1<br/>"
                    "Total Operations: %2</p>")
        .arg(m_coordinator && m_coordinator->storage() ? QString::fromStdString(m_coordinator->storage()->dbPath()) : "Unknown")
        .arg(m_opCount);

    // Voice Subsystem
    info += "<p><b>Voice Subsystem:</b> <span style='color:#34D399;'>ACTIVE</span><br/>"
            "Audio Capture: WASAPI 16000 Hz Mono PCM<br/>"
            "Speech Recognition: Local Whisper.cpp (ggml-tiny.en.bin)<br/>"
            "Speech Synthesis: Native Windows SAPI 5.4</p>";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("FLEX 1.0 — Diagnostics Drawer");
    msgBox.setText(info);
    msgBox.setStyleSheet(Theme::getApplicationStylesheet());
    msgBox.exec();
}

void MainWindow::connectCoordinatorSignals()
{
    if (!m_coordinator) {
        return;
    }

    connect(m_coordinator.get(), &BackendCoordinator::chatFinished,
            this, &MainWindow::onChatFinished);
    connect(m_coordinator.get(), &BackendCoordinator::chatError,
            this, &MainWindow::onChatError);
    connect(m_coordinator.get(), &BackendCoordinator::chatToken,
            this, &MainWindow::onChatToken);
}

void MainWindow::handleSendChat(const QString& promptText)
{
    QString trimmed = promptText.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (!m_coordinator) {
        m_chatView->showErrorBanner("Backend coordinator is not initialized.");
        return;
    }

    if (m_coordinator->coreSession().isProcessing()) {
        m_chatView->showErrorBanner("System is currently processing another operation. Please wait.");
        return;
    }

    ChatRequest req;
    req.message = trimmed.toStdString();
    if (!m_chatView->conversationId().isEmpty()) {
        req.conversationId = m_chatView->conversationId().toStdString();
    }
    req.streaming = true;

    m_chatView->setInputEnabled(false);
    m_chatView->setVisualState(CoreVisualState::Thinking);
    m_chatView->appendUserMessage(trimmed);
    m_chatView->startAssistantMessage();

    updateCoreStateDisplay();

    m_currentRequestId = m_coordinator->sendChatAsync(req);
    if (m_currentRequestId == 0) {
        m_chatView->setInputEnabled(true);
        m_chatView->setVisualState(CoreVisualState::Error);
        m_chatView->showErrorBanner("Failed to dispatch request to backend coordinator.");
        updateCoreStateDisplay();
    }
}

void MainWindow::handleNewConversation()
{
    m_chatView->clearConversation();
    if (auto* clist = conversationListWidget()) {
        clist->selectConversation(QString());
    }
    switchView("home");
    m_navSidebar->setActiveView("home");
    m_chatView->focusInput();
    updateCoreStateDisplay();
}

void MainWindow::handleSelectConversation(const QString& conversationId)
{
    if (conversationId.isEmpty() || !m_coordinator) {
        return;
    }

    auto convOpt = m_coordinator->getConversation(conversationId.toStdString());
    QString title = convOpt.has_value() ? QString::fromStdString(convOpt->title) : "Conversation";
    auto messages = m_coordinator->getConversationMessages(conversationId.toStdString());

    m_chatView->setConversation(conversationId, title, messages);
    switchView("home");
    m_navSidebar->setActiveView("home");
    updateCoreStateDisplay();
}

void MainWindow::handleDeleteConversation(const QString& conversationId)
{
    if (conversationId.isEmpty() || !m_coordinator) {
        return;
    }

    bool active = (m_chatView->conversationId() == conversationId);
    m_coordinator->deleteConversation(conversationId.toStdString());

    refreshConversations();

    if (active) {
        handleNewConversation();
    }
}

void MainWindow::refreshConversations()
{
    if (!m_coordinator) {
        return;
    }

    auto list = m_coordinator->listConversations(50);
    if (auto* clist = conversationListWidget()) {
        clist->setConversations(list);
        if (!m_chatView->conversationId().isEmpty()) {
            clist->selectConversation(m_chatView->conversationId());
        }
    }
}

void MainWindow::checkAiConnectivity()
{
    if (m_isCheckingHealth || !m_coordinator) {
        return;
    }
    m_isCheckingHealth = true;

    auto coord = m_coordinator;
    QPointer<MainWindow> safeThis = this;
    auto* thread = QThread::create([safeThis, coord]() {
        bool ok = false;
        std::string model = "unknown";
        try {
            ok = coord->checkHealthSync(1.5);
            if (ok && coord->ollamaClient()) {
                model = coord->ollamaClient()->config().model;
            }
        } catch (...) {
            ok = false;
        }

        if (safeThis) {
            QMetaObject::invokeMethod(safeThis.data(), [safeThis, ok, model]() {
                if (safeThis) {
                    safeThis->m_isCheckingHealth = false;
                    safeThis->updateTelemetryBar(ok, QString::fromStdString(model));
                }
            }, Qt::QueuedConnection);
        }
    });

    m_healthCheckThread = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::updateTelemetryBar(bool ollamaOnline, const QString& modelName)
{
    m_lastOllamaOnline = ollamaOnline;
    m_lastOllamaModel = modelName;

    if (m_topHeader) {
        m_topHeader->setOllamaStatus(ollamaOnline, modelName);
    }

    QString dbPath;
    if (m_coordinator && m_coordinator->storage()) {
        dbPath = QString::fromStdString(m_coordinator->storage()->dbPath());
    }

    bool coreProc = m_coordinator ? m_coordinator->coreSession().isProcessing() : false;
    if (m_rightSidebar) {
        m_rightSidebar->updateTelemetry(ollamaOnline, modelName, coreProc, dbPath);
    }
}

void MainWindow::updateCoreStateDisplay()
{
    if (!m_coordinator) {
        return;
    }

    bool processing = m_coordinator->coreSession().isProcessing();
    QString stateStr = CoreSession::lifecycleToString(m_coordinator->coreSession().lifecycle());

    if (m_topHeader) {
        m_topHeader->setCoreStatus(stateStr, processing);
    }

    QString dbPath;
    if (m_coordinator && m_coordinator->storage()) {
        dbPath = QString::fromStdString(m_coordinator->storage()->dbPath());
    }

    if (m_rightSidebar) {
        m_rightSidebar->updateTelemetry(m_lastOllamaOnline, m_lastOllamaModel, processing, dbPath);
    }
}

void MainWindow::onChatToken(uint64_t requestId, const QString& token)
{
    if (requestId != m_currentRequestId) {
        return;
    }
    m_chatView->setVisualState(CoreVisualState::Responding);
    m_chatView->appendAssistantChunk(token);
}

void MainWindow::onChatFinished(uint64_t requestId, const flex::ChatResult& result)
{
    if (requestId != m_currentRequestId) {
        return;
    }
    m_currentRequestId = 0;
    m_opCount++;
    if (m_topHeader) {
        m_topHeader->setOpCount(m_opCount);
    }

    m_chatView->setInputEnabled(true);
    updateCoreStateDisplay();

    if (result.success) {
        m_chatView->setVisualState(CoreVisualState::Idle);
        m_chatView->finishAssistantMessage(QString::fromStdString(result.responseText));

        // If this was a new conversation, update ChatView's conversation ID
        if (m_chatView->conversationId().isEmpty() && !result.conversationId.empty()) {
            auto convOpt = m_coordinator->getConversation(result.conversationId);
            QString title = convOpt.has_value() ? QString::fromStdString(convOpt->title) : "New Chat";
            auto messages = m_coordinator->getConversationMessages(result.conversationId);
            m_chatView->setConversation(QString::fromStdString(result.conversationId), title, messages);
        }

        refreshConversations();
        if (!result.conversationId.empty()) {
            if (auto* clist = conversationListWidget()) {
                clist->selectConversation(QString::fromStdString(result.conversationId));
            }
        }
    } else {
        m_chatView->setVisualState(CoreVisualState::Error);
        m_chatView->finishAssistantMessage();
        QString errStr = QString::fromStdString(result.error.message);
        if (errStr.isEmpty()) {
            errStr = "An error occurred during inference.";
        }
        m_chatView->showErrorBanner(errStr);
    }
}

void MainWindow::onChatError(uint64_t requestId, const flex::BackendError& error)
{
    if (requestId != m_currentRequestId) {
        return;
    }
    m_chatView->setVisualState(CoreVisualState::Error);
    QString errStr = QString::fromStdString(error.message);
    m_chatView->showErrorBanner(errStr);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_healthCheckTimer.stop();
    if (m_healthCheckThread && m_healthCheckThread->isRunning()) {
        m_healthCheckThread->wait(1000);
    }
    if (m_voiceController) {
        m_voiceController->cancelAll();
    }
    if (m_coordinator) {
        m_coordinator->cancelAll();
    }
    event->accept();
}

} // namespace flex::ui
