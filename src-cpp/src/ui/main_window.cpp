#include "flex/ui/main_window.hpp"
#include "flex/ui/theme.hpp"

#include <QCloseEvent>
#include <QHBoxLayout>
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
    if (!m_coordinator) {
        m_coordinator = std::make_shared<BackendCoordinator>();
    }

    setupUi();
    connectCoordinatorSignals();

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
    if (m_coordinator) {
        m_coordinator->cancelAll();
    }
}

void MainWindow::setupUi()
{
    setWindowTitle("FLEX 1.0 — Native Desktop AI Assistant");
    resize(1120, 760);
    setMinimumSize(960, 600);

    setStyleSheet(Theme::getApplicationStylesheet());

    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("CentralWidget");
    setCentralWidget(centralWidget);

    auto* rootLayout = new QHBoxLayout(centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, centralWidget);
    m_splitter->setHandleWidth(1);

    // =========================================================================
    // Left Sidebar
    // =========================================================================
    m_sidebarWidget = new QWidget(m_splitter);
    m_sidebarWidget->setObjectName("SidebarWidget");
    m_sidebarWidget->setMinimumWidth(240);
    m_sidebarWidget->setMaximumWidth(400);

    auto* sidebarLayout = new QVBoxLayout(m_sidebarWidget);
    sidebarLayout->setContentsMargins(14, 16, 14, 14);
    sidebarLayout->setSpacing(12);

    // Brand Header
    auto* brandHeader = new QWidget(m_sidebarWidget);
    auto* brandLayout = new QVBoxLayout(brandHeader);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(2);

    auto* brandTitle = new QLabel("FLEX 1.0", brandHeader);
    brandTitle->setObjectName("AppBrandTitle");

    auto* brandSub = new QLabel("NATIVE C/C++ AI ASSISTANT", brandHeader);
    brandSub->setObjectName("AppBrandSubtitle");

    brandLayout->addWidget(brandTitle);
    brandLayout->addWidget(brandSub);
    sidebarLayout->addWidget(brandHeader);

    // New Conversation Button
    m_newChatButton = new QPushButton("+ New Conversation", m_sidebarWidget);
    m_newChatButton->setObjectName("NewChatButton");
    m_newChatButton->setCursor(Qt::PointingHandCursor);
    connect(m_newChatButton, &QPushButton::clicked, this, &MainWindow::handleNewConversation);
    sidebarLayout->addWidget(m_newChatButton);

    // Conversation List Widget
    m_convList = new ConversationListWidget(m_sidebarWidget);
    connect(m_convList, &ConversationListWidget::conversationSelected,
            this, &MainWindow::handleSelectConversation);
    connect(m_convList, &ConversationListWidget::conversationDeleteRequested,
            this, &MainWindow::handleDeleteConversation);
    sidebarLayout->addWidget(m_convList, 1);

    // Telemetry & Status Card
    auto* telemetryCard = new QFrame(m_sidebarWidget);
    telemetryCard->setObjectName("TelemetryCard");
    auto* teleLayout = new QVBoxLayout(telemetryCard);
    teleLayout->setContentsMargins(8, 8, 8, 8);
    teleLayout->setSpacing(4);

    m_ollamaStatusLabel = new QLabel("○ Ollama: Checking...", telemetryCard);
    m_ollamaStatusLabel->setObjectName("TelemetryLabel");

    m_coreStatusLabel = new QLabel("● C Core: IDLE", telemetryCard);
    m_coreStatusLabel->setObjectName("TelemetryLabel");

    m_dbStatusLabel = new QLabel("● Storage: SQLite Native", telemetryCard);
    m_dbStatusLabel->setObjectName("TelemetryLabel");
    if (m_coordinator && m_coordinator->storage()) {
        m_dbStatusLabel->setToolTip(QString::fromStdString(m_coordinator->storage()->dbPath()));
    }

    teleLayout->addWidget(m_ollamaStatusLabel);
    teleLayout->addWidget(m_coreStatusLabel);
    teleLayout->addWidget(m_dbStatusLabel);

    sidebarLayout->addWidget(telemetryCard);

    // =========================================================================
    // Right Chat Workspace
    // =========================================================================
    m_chatView = new ChatView(m_splitter);
    connect(m_chatView, &ChatView::sendRequested, this, &MainWindow::handleSendChat);

    m_splitter->addWidget(m_sidebarWidget);
    m_splitter->addWidget(m_chatView);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({ 280, 840 });

    rootLayout->addWidget(m_splitter);
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
    m_convList->selectConversation(QString());
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
    m_convList->setConversations(list);

    if (!m_chatView->conversationId().isEmpty()) {
        m_convList->selectConversation(m_chatView->conversationId());
    }
}

void MainWindow::checkAiConnectivity()
{
    if (m_isCheckingHealth || !m_coordinator) {
        return;
    }
    m_isCheckingHealth = true;

    auto coord = m_coordinator;
    auto* thread = QThread::create([this, coord]() {
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

        QMetaObject::invokeMethod(this, [this, ok, model]() {
            m_isCheckingHealth = false;
            updateTelemetryBar(ok, QString::fromStdString(model));
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::updateTelemetryBar(bool ollamaOnline, const QString& modelName)
{
    if (ollamaOnline) {
        QString text = QString("● Ollama: Connected (%1)").arg(modelName.isEmpty() ? "online" : modelName);
        m_ollamaStatusLabel->setText(text);
        m_ollamaStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::StatusSuccess));
    } else {
        m_ollamaStatusLabel->setText("○ Ollama: Offline (127.0.0.1:11434)");
        m_ollamaStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::StatusOffline));
    }
}

void MainWindow::updateCoreStateDisplay()
{
    if (!m_coordinator) {
        return;
    }

    bool processing = m_coordinator->coreSession().isProcessing();
    if (processing) {
        m_coreStatusLabel->setText("● C Core: PROCESSING");
        m_coreStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TextAccentCyan));
    } else {
        m_coreStatusLabel->setText("● C Core: IDLE");
        m_coreStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::StatusSuccess));
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
            m_convList->selectConversation(QString::fromStdString(result.conversationId));
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
    if (m_coordinator) {
        m_coordinator->cancelAll();
    }
    event->accept();
}

} // namespace flex::ui

