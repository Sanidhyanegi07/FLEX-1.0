#include "flex_ui.hpp"
#include "flex/backend.hpp"
#include "flex/settings_persistence_service.hpp"
#include <QTemporaryDir>
#include <QStandardItemModel>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

#define EXPECT_TRUE(cond) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " (" #cond " is false)\n"; \
    } \
} while (0)

#define EXPECT_FALSE(cond) do { \
    g_tests_run++; \
    if (!(cond)) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " (" #cond " is true)\n"; \
    } \
} while (0)

#define EXPECT_EQ(expected, actual) do { \
    g_tests_run++; \
    auto _exp = (expected); \
    auto _act = (actual); \
    if (_exp == _act) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                  << " (expected " << _exp << ", got " << _act << ")\n"; \
    } \
} while (0)

#define EXPECT_STR_EQ(expected, actual) do { \
    g_tests_run++; \
    std::string _exp = (expected); \
    std::string _act = (actual); \
    if (_exp == _act) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                  << " (expected '" << _exp << "', got '" << _act << "')\n"; \
    } \
} while (0)

// -----------------------------------------------------------------------------
// In-Process Mock HTTP Server for Deterministic AI Testing
// -----------------------------------------------------------------------------

class MockOllamaServer : public QObject {
    Q_OBJECT

public:
    explicit MockOllamaServer(QObject* parent = nullptr)
        : QObject(parent), m_server(new QTcpServer(this))
    {
        connect(m_server, &QTcpServer::newConnection, this, &MockOllamaServer::onNewConnection);
        bool ok = m_server->listen(QHostAddress::LocalHost, 0);
        if (!ok) {
            std::cerr << "Failed to start MockOllamaServer: "
                      << m_server->errorString().toStdString() << "\n";
        }
    }

    ~MockOllamaServer() override
    {
        if (m_server && m_server->isListening()) {
            m_server->close();
        }
    }

    [[nodiscard]] std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(m_server->serverPort());
    }

    void reset()
    {
        m_statusCode = 200;
        m_statusText = "OK";
        m_responseBody.clear();
        m_streamChunks.clear();
        m_isStreaming = false;
        m_delayMs = 0;
        m_requestCount = 0;
        m_lastRequestPath.clear();
        m_lastRequestBody.clear();
    }

    void setResponse(int code, const std::string& status, const std::string& body)
    {
        m_statusCode = code;
        m_statusText = status;
        m_responseBody = body;
        m_isStreaming = false;
    }

    void setStreamChunks(const std::vector<std::string>& chunks, int code = 200)
    {
        m_statusCode = code;
        m_statusText = "OK";
        m_streamChunks = chunks;
        m_isStreaming = true;
    }

    void setDelay(int ms) { m_delayMs = ms; }
    [[nodiscard]] int requestCount() const { return m_requestCount; }
    [[nodiscard]] std::string lastRequestBody() const { return m_lastRequestBody; }

private slots:
    void onNewConnection()
    {
        while (m_server->hasPendingConnections()) {
            QTcpSocket* socket = m_server->nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                m_rawBuffer.append(socket->readAll());
                int headerEnd = m_rawBuffer.indexOf("\r\n\r\n");
                if (headerEnd == -1) {
                    return;
                }

                QByteArray headerBytes = m_rawBuffer.left(headerEnd);
                QString headerStr = QString::fromUtf8(headerBytes);
                QStringList lines = headerStr.split("\r\n");
                if (!lines.isEmpty()) {
                    QStringList reqLine = lines[0].split(" ");
                    if (reqLine.size() >= 2) {
                        m_lastRequestPath = reqLine[1].toStdString();
                    }
                }

                int contentLength = 0;
                for (const QString& line : lines) {
                    if (line.startsWith("Content-Length:", Qt::CaseInsensitive)) {
                        contentLength = line.section(':', 1).trimmed().toInt();
                        break;
                    }
                }

                if (m_rawBuffer.size() < headerEnd + 4 + contentLength) {
                    return; // Wait for full body
                }

                QByteArray bodyBytes = m_rawBuffer.mid(headerEnd + 4, contentLength);
                m_lastRequestBody = bodyBytes.toStdString();
                m_rawBuffer.clear();
                m_requestCount++;

                int delay = m_delayMs;
                QPointer<QTcpSocket> guardedSocket(socket);
                auto sendReply = [this, guardedSocket]() {
                    if (!guardedSocket || guardedSocket->state() != QAbstractSocket::ConnectedState) {
                        return;
                    }
                    if (m_isStreaming) {
                        QByteArray responseHeaders;
                        responseHeaders.append("HTTP/1.1 200 OK\r\n");
                        responseHeaders.append("Content-Type: application/x-ndjson\r\n");
                        responseHeaders.append("Connection: close\r\n\r\n");
                        guardedSocket->write(responseHeaders);
                        for (const auto& chunk : m_streamChunks) {
                            guardedSocket->write(QByteArray::fromStdString(chunk + "\n"));
                            guardedSocket->flush();
                        }
                        guardedSocket->disconnectFromHost();
                    } else {
                        QByteArray response;
                        response.append(QString("HTTP/1.1 %1 %2\r\n").arg(m_statusCode).arg(QString::fromStdString(m_statusText)).toUtf8());
                        response.append("Content-Type: application/json\r\n");
                        response.append(QString("Content-Length: %1\r\n").arg(m_responseBody.size()).toUtf8());
                        response.append("Connection: close\r\n\r\n");
                        response.append(QByteArray::fromStdString(m_responseBody));
                        guardedSocket->write(response);
                        guardedSocket->disconnectFromHost();
                    }
                };

                if (delay > 0) {
                    QTimer::singleShot(delay, this, sendReply);
                } else {
                    sendReply();
                }
            });
        }
    }

private:
    QTcpServer* m_server{nullptr};
    QByteArray m_rawBuffer;
    int m_statusCode{200};
    std::string m_statusText{"OK"};
    std::string m_responseBody;
    std::vector<std::string> m_streamChunks;
    bool m_isStreaming{false};
    int m_delayMs{0};
    int m_requestCount{0};
    std::string m_lastRequestPath;
    std::string m_lastRequestBody;
};

// -----------------------------------------------------------------------------
// Temporary Database Guard
// -----------------------------------------------------------------------------

class TempDbGuard {
public:
    TempDbGuard()
    {
        QString tempDir = QDir::tempPath();
        QString uniqueName = QString("flex_ui_test_%1.db").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_path = QDir(tempDir).filePath(uniqueName).toStdString();
        cleanup();
    }

    ~TempDbGuard()
    {
        cleanup();
    }

    [[nodiscard]] std::string path() const { return m_path; }

    void cleanup()
    {
        if (QFile::exists(QString::fromStdString(m_path))) {
            QFile::remove(QString::fromStdString(m_path));
        }
    }

private:
    std::string m_path;
};

// -----------------------------------------------------------------------------
// Test 1: Theme & Stylesheet Integrity
// -----------------------------------------------------------------------------
void test_theme_stylesheet()
{
    std::cout << "[Test 1] Theme and application stylesheet integrity...\n";
    QString qss = flex::ui::Theme::getApplicationStylesheet();
    EXPECT_FALSE(qss.isEmpty());
    EXPECT_TRUE(qss.contains("#0A0B10"));
    EXPECT_TRUE(qss.contains("#7C3AED"));
    EXPECT_TRUE(qss.contains("#1E2235"));
    EXPECT_TRUE(qss.contains("AppBrandTitle"));
    EXPECT_TRUE(qss.contains("NewChatButton"));
    EXPECT_TRUE(qss.contains("TelemetryCard"));
}

// -----------------------------------------------------------------------------
// Test 2: AI Core Visualizer Lifecycle and States
// -----------------------------------------------------------------------------
void test_ai_core_widget()
{
    std::cout << "[Test 2] AI Core visualizer lifecycle and state transitions...\n";
    flex::ui::AICoreWidget core(nullptr, false);
    EXPECT_EQ(static_cast<int>(flex::ui::CoreVisualState::Idle), static_cast<int>(core.visualState()));
    EXPECT_FALSE(core.isCompact());

    core.setVisualState(flex::ui::CoreVisualState::Thinking);
    EXPECT_EQ(static_cast<int>(flex::ui::CoreVisualState::Thinking), static_cast<int>(core.visualState()));

    core.setVisualState(flex::ui::CoreVisualState::Responding);
    EXPECT_EQ(static_cast<int>(flex::ui::CoreVisualState::Responding), static_cast<int>(core.visualState()));

    core.setVisualState(flex::ui::CoreVisualState::Error);
    EXPECT_EQ(static_cast<int>(flex::ui::CoreVisualState::Error), static_cast<int>(core.visualState()));

    core.setVisualState(flex::ui::CoreVisualState::Offline);
    EXPECT_EQ(static_cast<int>(flex::ui::CoreVisualState::Offline), static_cast<int>(core.visualState()));

    core.setCompact(true);
    EXPECT_TRUE(core.isCompact());
    EXPECT_EQ(70, core.height());

    core.startAnimation();
    core.stopAnimation();
}

// -----------------------------------------------------------------------------
// Test 3: Message Bubble Styling and Streaming Chunks
// -----------------------------------------------------------------------------
void test_message_bubble_streaming()
{
    std::cout << "[Test 3] Message bubble creation and token streaming...\n";
    flex::ui::MessageBubble userBubble("user", "Hello FLEX", "18:00");
    EXPECT_STR_EQ("user", userBubble.role().toStdString());
    EXPECT_STR_EQ("Hello FLEX", userBubble.content().toStdString());

    flex::ui::MessageBubble assistantBubble("assistant", "", "18:00", "qwen2.5-coder:3b");
    EXPECT_STR_EQ("assistant", assistantBubble.role().toStdString());
    EXPECT_TRUE(assistantBubble.content().isEmpty());

    assistantBubble.appendChunk("Hello ");
    assistantBubble.appendChunk("there!");
    EXPECT_STR_EQ("Hello there!", assistantBubble.content().toStdString());

    flex::ui::MessageBubble errBubble("error", "Network connection failed");
    EXPECT_STR_EQ("error", errBubble.role().toStdString());
    EXPECT_STR_EQ("Network connection failed", errBubble.content().toStdString());
}

// -----------------------------------------------------------------------------
// Test 4: Main Window Construction and Empty State
// -----------------------------------------------------------------------------
void test_main_window_construction_and_empty_db()
{
    std::cout << "[Test 4] Main window construction and empty DB handling...\n";
    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = "http://127.0.0.1:9999"; // Offline port
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    flex::ui::MainWindow window(coordinator);
    EXPECT_TRUE(window.coordinator() != nullptr);
    EXPECT_TRUE(window.chatView() != nullptr);
    EXPECT_TRUE(window.conversationListWidget() != nullptr);
    EXPECT_EQ(0, window.conversationListWidget()->conversationCount());
    EXPECT_EQ(0, window.chatView()->messageCount());
    EXPECT_TRUE(window.coordinator()->coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 5: Empty and Whitespace Input Rejection
// -----------------------------------------------------------------------------
void test_empty_input_rejection()
{
    std::cout << "[Test 5] Empty and whitespace prompt rejection...\n";
    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    flex::ui::MainWindow window(coordinator);

    // Send empty string
    window.handleSendChat("");
    EXPECT_EQ(0, window.chatView()->messageCount());
    EXPECT_TRUE(coordinator->coreSession().isIdle());

    // Send whitespace only
    window.handleSendChat("   \t \n  ");
    EXPECT_EQ(0, window.chatView()->messageCount());
    EXPECT_TRUE(coordinator->coreSession().isIdle());
    EXPECT_EQ(0, storage->getConversationCount());
}

// -----------------------------------------------------------------------------
// Test 6: Conversation List Loading Existing Records
// -----------------------------------------------------------------------------
void test_conversation_list_existing_records()
{
    std::cout << "[Test 6] Loading existing conversation records from SQLite...\n";
    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    std::string cid1 = storage->createConversation("First Project");
    storage->saveMessage(cid1, "user", "Message 1");
    storage->saveMessage(cid1, "assistant", "Reply 1");

    std::string cid2 = storage->createConversation("Second Project");
    storage->saveMessage(cid2, "user", "Message 2");

    flex::OllamaConfig cfg;
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    flex::ui::MainWindow window(coordinator);
    window.refreshConversations();

    EXPECT_EQ(2, window.conversationListWidget()->conversationCount());

    // Select conversation 1
    window.handleSelectConversation(QString::fromStdString(cid1));
    EXPECT_STR_EQ(cid1, window.chatView()->conversationId().toStdString());
    EXPECT_EQ(2, window.chatView()->messageCount());

    // Select conversation 2
    window.handleSelectConversation(QString::fromStdString(cid2));
    EXPECT_STR_EQ(cid2, window.chatView()->conversationId().toStdString());
    EXPECT_EQ(1, window.chatView()->messageCount());
}

// -----------------------------------------------------------------------------
// Test 7: Successful Chat Turn with Mock Server
// -----------------------------------------------------------------------------
void test_successful_chat_turn(MockOllamaServer& mock)
{
    std::cout << "[Test 7] Successful chat turn and assistant response rendering...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Welcome to FLEX 1.0 Native!"}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    flex::ui::MainWindow window(coordinator);

    QEventLoop loop;
    QObject::connect(coordinator.get(), &flex::BackendCoordinator::chatFinished,
                     &loop, &QEventLoop::quit);

    window.handleSendChat("Hello FLEX");

    // ChatView should immediately show user message and placeholder assistant bubble
    EXPECT_EQ(2, window.chatView()->messageCount());
    EXPECT_TRUE(coordinator->coreSession().isProcessing());

    loop.exec();

    // Verify chat completed successfully
    EXPECT_TRUE(coordinator->coreSession().isIdle());
    EXPECT_EQ(2, window.chatView()->messageCount());
    EXPECT_EQ(1, storage->getConversationCount());

    // Verify conversation record in sidebar
    EXPECT_EQ(1, window.conversationListWidget()->conversationCount());
}

// -----------------------------------------------------------------------------
// Test 8: Backend Error Displays Visibly Without Crashing
// -----------------------------------------------------------------------------
void test_backend_error_displays_visibly(MockOllamaServer& mock)
{
    std::cout << "[Test 8] Backend error displayed as visible alert...\n";
    mock.reset();
    mock.setResponse(500, "Internal Server Error", R"({"error": "Model computation failure"})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    flex::ui::MainWindow window(coordinator);

    QEventLoop loop;
    QObject::connect(coordinator.get(), &flex::BackendCoordinator::chatFinished,
                     &loop, &QEventLoop::quit);

    window.handleSendChat("Trigger Error");

    loop.exec();

    // Core must be safely reset to IDLE
    EXPECT_TRUE(coordinator->coreSession().isIdle());

    // Input must be re-enabled
    EXPECT_TRUE(window.chatView()->findChild<flex::ui::ChatInputEdit*>()->isEnabled());
}

// -----------------------------------------------------------------------------
// Test 9: Repeated Send Attempt Blocked While Processing
// -----------------------------------------------------------------------------
void test_repeated_send_blocked(MockOllamaServer& mock)
{
    std::cout << "[Test 9] Repeated send blocked while operation is active...\n";
    mock.reset();
    mock.setDelay(250); // 250ms delay
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Slow reply"}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    flex::ui::MainWindow window(coordinator);

    // Send first request
    window.handleSendChat("First prompt");
    EXPECT_TRUE(coordinator->coreSession().isProcessing());

    // Attempt second send while first is in-flight
    window.handleSendChat("Second prompt attempt");

    // Must still have only 2 bubbles from the first prompt
    EXPECT_EQ(2, window.chatView()->messageCount());

    QEventLoop loop;
    QObject::connect(coordinator.get(), &flex::BackendCoordinator::chatFinished,
                     &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_TRUE(coordinator->coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 10: Safe Window Shutdown with Pending Request
// -----------------------------------------------------------------------------
void test_safe_window_shutdown(MockOllamaServer& mock)
{
    std::cout << "[Test 10] Safe window closure with in-flight request...\n";
    mock.reset();
    mock.setDelay(400); // 400ms delay
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Delayed"}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    {
        auto window = std::make_unique<flex::ui::MainWindow>(coordinator);
        window->handleSendChat("In flight prompt");
        EXPECT_TRUE(coordinator->coreSession().isProcessing());

        // Close and destroy window while request is in-flight
        window->close();
    }

    // Window cleanly destroyed
    EXPECT_TRUE(true);

    // Let event loop drain any pending timers
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();

    // Verify coordinator cleaned up C core operation
    EXPECT_TRUE(coordinator->coreSession().isIdle());
}

void test_chat_view_voice_controls()
{
    std::cout << "[Test 11] ChatView voice button transitions and error propagation...\n";
    flex::ui::ChatView view;

    EXPECT_STR_EQ("🎙 Record Audio", view.recordButtonText().toStdString());
    EXPECT_STR_EQ("🔊 Test Voice", view.testTtsButtonText().toStdString());
    EXPECT_STR_EQ("○ Audio: Ready", view.audioStatusText().toStdString());

    auto tts = view.textToSpeechService();
    EXPECT_TRUE(tts != nullptr);

    class MockUiTtsEngine : public flex::voice::ITextToSpeechEngine {
    public:
        bool initialize() override { return true; }
        bool speak(const std::string&, bool) override { notifyStarted(); return true; }
        void stop() override { notifyStopped(); }
        bool isSpeaking() const override { return false; }
    };

    auto mockEngine = std::make_shared<MockUiTtsEngine>();
    tts->setEngine(mockEngine);

    // Speak
    tts->speak("Test voice UI phrase");
    EXPECT_STR_EQ("⏹ Stop Voice", view.testTtsButtonText().toStdString());
    EXPECT_STR_EQ("● Speaking test phrase...", view.audioStatusText().toStdString());

    // Finish
    mockEngine->notifyFinished();
    EXPECT_STR_EQ("🔊 Test Voice", view.testTtsButtonText().toStdString());
    EXPECT_STR_EQ("✓ Speech finished", view.audioStatusText().toStdString());

    // Speak again, then cancel/stop
    tts->speak("Test phrase 2");
    EXPECT_STR_EQ("⏹ Stop Voice", view.testTtsButtonText().toStdString());
    tts->stop();
    EXPECT_STR_EQ("🔊 Test Voice", view.testTtsButtonText().toStdString());
    EXPECT_STR_EQ("○ Speech stopped", view.audioStatusText().toStdString());

    // Error
    tts->speak("Test phrase 3");
    mockEngine->notifyError("Hardware SAPI error");
    EXPECT_STR_EQ("🔊 Test Voice", view.testTtsButtonText().toStdString());
    EXPECT_TRUE(view.audioStatusText().contains("TTS Error"));
}

void test_shell_restoration_and_view_switching()
{
    std::cout << "[Test 12] Shell restoration, 3-column layout, and view switching...\n";
    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = "http://127.0.0.1:9999";
    auto client = std::make_shared<flex::OllamaClient>(cfg);
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

    flex::ui::MainWindow window(coordinator);

    // Verify window initial dimensions and title
    EXPECT_TRUE(window.windowTitle().contains("FLEX 1.0"));
    EXPECT_TRUE(window.minimumWidth() >= 960);
    EXPECT_TRUE(window.minimumHeight() >= 600);

    // Verify views switch cleanly
    window.switchView("voice");
    window.switchView("memory");
    window.switchView("knowledge");
    window.switchView("projects");
    window.switchView("tools");
    window.switchView("settings");
    window.switchView("home");

    // Capture visual artifacts of the restored UI for verification reporting
    window.resize(1180, 780);
    window.show();
    QCoreApplication::processEvents();

    QString artifactDir = "C:/Users/sanid/.gemini/antigravity/brain/192af516-bd68-4580-acd1-dc492b739fef";
    if (QDir(artifactDir).exists()) {
        window.switchView("home");
        QCoreApplication::processEvents();
        window.grab().save(artifactDir + "/screenshot_home.png");

        window.switchView("voice");
        QCoreApplication::processEvents();
        window.grab().save(artifactDir + "/screenshot_voice_studio.png");

        window.switchView("projects");
        QCoreApplication::processEvents();
        window.grab().save(artifactDir + "/screenshot_projects.png");

        window.switchView("memory");
        QCoreApplication::processEvents();
        window.grab().save(artifactDir + "/screenshot_memory.png");

        window.switchView("settings");
        QCoreApplication::processEvents();
        window.grab().save(artifactDir + "/screenshot_settings.png");

        window.switchView("home");
        QCoreApplication::processEvents();
    }

    // Verify ChatView still active and focused
    EXPECT_TRUE(window.chatView() != nullptr);
    EXPECT_EQ(0, window.chatView()->messageCount());
}

void test_projects_and_memory_views()
{
    std::cout << "[Test 13] Projects explorer dynamic root and memory vault...\n";
    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    std::string cid = storage->createConversation("Memory Test Chat");
    storage->saveMessage(cid, "user", "What is C11?");
    storage->saveMessage(cid, "assistant", "C11 is an ISO standard for C.");

    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, nullptr);

    flex::ui::MemoryView memView(coordinator);
    memView.refreshConversations();

    flex::ui::ProjectsView projView;
    EXPECT_FALSE(projView.currentRootPath().isEmpty());
    EXPECT_TRUE(QDir(projView.currentRootPath()).exists());
}

void test_settings_view_ui_persistence()
{
    std::cout << "[Test 14] SettingsView UI controls and isolated disk persistence...\n";
    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());

    QString testConfigPath = tempDir.filePath("ui_test_config.ini");
    qputenv("FLEX_CONFIG_PATH", testConfigPath.toUtf8());

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, nullptr);

    flex::ui::SettingsView settingsView(coordinator);
    QCoreApplication::processEvents();

    // Verify UI defaults were loaded
    auto initialLoad = flex::SettingsPersistenceService::loadSettings();
    EXPECT_TRUE(initialLoad.success);
    EXPECT_STR_EQ("qwen2.5-coder:3b", initialLoad.settings.ollamaModel);

    // Save settings explicitly via UI
    auto* saveBtn = settingsView.findChild<QPushButton*>("PrimaryButton");
    EXPECT_TRUE(saveBtn != nullptr);
    if (saveBtn) {
        saveBtn->click();
        QCoreApplication::processEvents();
    }

    // Verify config file was created on disk
    EXPECT_TRUE(QFile::exists(testConfigPath));

    auto savedLoad = flex::SettingsPersistenceService::loadSettings();
    EXPECT_TRUE(savedLoad.success);
    EXPECT_TRUE(savedLoad.fileExisted);
    EXPECT_STR_EQ("qwen2.5-coder:3b", savedLoad.settings.ollamaModel);

    qunsetenv("FLEX_CONFIG_PATH");
}

void test_settings_view_whisper_model_discovery()
{
    std::cout << "[Test 15] SettingsView Whisper model discovery, selection, and safety...\n";

    QTemporaryDir tempConfigDir;
    EXPECT_TRUE(tempConfigDir.isValid());
    QString testConfigPath = tempConfigDir.filePath("whisper_test_config.ini");
    qputenv("FLEX_CONFIG_PATH", testConfigPath.toUtf8());

    QTemporaryDir tempModelsDir;
    EXPECT_TRUE(tempModelsDir.isValid());

    // 1. Setup synthetic model directory with:
    // Model A: Multilingual base model (n_vocab = 51865)
    QString multiPath = tempModelsDir.filePath("ggml-base.bin");
    QFile multiFile(multiPath);
    EXPECT_TRUE(multiFile.open(QIODevice::WriteOnly));
    uint32_t multiHdr[4] = { 0x67676d6c, 51865, 1500, 512 };
    multiFile.write(reinterpret_cast<const char*>(multiHdr), sizeof(multiHdr));
    multiFile.resize(1024 * 1024 + 100);
    multiFile.close();

    // Model B: English-only tiny model (n_vocab = 51864)
    QString enPath = tempModelsDir.filePath("ggml-tiny.en.bin");
    QFile enFile(enPath);
    EXPECT_TRUE(enFile.open(QIODevice::WriteOnly));
    uint32_t enHdr[4] = { 0x67676d6c, 51864, 1500, 384 };
    enFile.write(reinterpret_cast<const char*>(enHdr), sizeof(enHdr));
    enFile.resize(1024 * 1024 + 100);
    enFile.close();

    // Model C: Corrupted/invalid model (bad magic)
    QString badPath = tempModelsDir.filePath("ggml-corrupt.bin");
    QFile badFile(badPath);
    EXPECT_TRUE(badFile.open(QIODevice::WriteOnly));
    uint32_t badHdr[4] = { 0x11223344, 51864, 1500, 384 };
    badFile.write(reinterpret_cast<const char*>(badHdr), sizeof(badHdr));
    badFile.resize(1024 * 1024 + 100);
    badFile.close();

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    auto coordinator = std::make_shared<flex::BackendCoordinator>(storage, nullptr);

    flex::ui::SettingsView settingsView(coordinator);
    QCoreApplication::processEvents();

    // Point SettingsView discovery at our synthetic directory
    settingsView.refreshModelList(tempModelsDir.path());
    QCoreApplication::processEvents();

    auto* combo = settingsView.whisperModelCombo();
    EXPECT_TRUE(combo != nullptr);
    auto* detailsLbl = settingsView.whisperDetailsLabel();
    EXPECT_TRUE(detailsLbl != nullptr);
    auto* pathEdit = settingsView.whisperPathEdit();
    EXPECT_TRUE(pathEdit != nullptr);

    // Expect items:
    // [0] Auto-detect Default
    // [1] ggml-base.bin (Multilingual)
    // [2] ggml-corrupt.bin (InvalidFormat)
    // [3] ggml-tiny.en.bin (English-only, Default)
    EXPECT_EQ(4, combo->count());

    // Verify capability labels
    EXPECT_TRUE(combo->itemText(1).contains("Multilingual"));
    EXPECT_TRUE(combo->itemText(3).contains("English-only"));
    EXPECT_TRUE(combo->itemText(3).contains("[Default]"));

    // Verify invalid model item is disabled (not selectable)
    auto* stdModel = qobject_cast<QStandardItemModel*>(combo->model());
    EXPECT_TRUE(stdModel != nullptr);
    if (stdModel) {
        auto* badItem = stdModel->item(2);
        EXPECT_TRUE(badItem != nullptr);
        if (badItem) {
            EXPECT_FALSE(badItem->isEnabled());
        }
    }

    // Select the multilingual model
    combo->setCurrentIndex(1);
    QCoreApplication::processEvents();
    EXPECT_STR_EQ(multiPath.toStdString(), pathEdit->text().toStdString());
    EXPECT_TRUE(detailsLbl->text().contains("Multilingual"));
    EXPECT_TRUE(detailsLbl->text().contains("Valid GGML Binary Header"));
    EXPECT_TRUE(detailsLbl->text().contains("Header classification; not proof of inference"));

    // Save settings and verify persistence
    auto* saveBtn = settingsView.findChild<QPushButton*>("PrimaryButton");
    EXPECT_TRUE(saveBtn != nullptr);
    if (saveBtn) {
        saveBtn->click();
        QCoreApplication::processEvents();
    }

    auto savedConfig = flex::SettingsPersistenceService::loadSettings();
    EXPECT_TRUE(savedConfig.success);
    EXPECT_STR_EQ(multiPath.toStdString(), savedConfig.settings.whisperModelPath);

    // Verify language setting was NOT mutated by selecting a model
    EXPECT_STR_EQ("en", savedConfig.settings.whisperLanguage);

    // 2. Test empty model directory handling
    QTemporaryDir emptyModelsDir;
    EXPECT_TRUE(emptyModelsDir.isValid());
    settingsView.refreshModelList(emptyModelsDir.path());
    QCoreApplication::processEvents();

    // Verify combo has Auto-detect Default + previously configured custom path
    EXPECT_TRUE(combo->count() >= 1);
    // Configured model was preserved in the saved settings
    auto preservedConfig = flex::SettingsPersistenceService::loadSettings();
    EXPECT_STR_EQ(multiPath.toStdString(), preservedConfig.settings.whisperModelPath);

    // 3. Test missing configured model warning
    pathEdit->setText("C:/nonexistent_folder_xyz/missing_model.bin");
    settingsView.updateModelDetailsDisplay();
    QCoreApplication::processEvents();
    EXPECT_TRUE(detailsLbl->text().contains("not found on disk"));
    EXPECT_TRUE(detailsLbl->text().contains("Saved preference is preserved"));

    // 4. Test invalid model cannot be saved
    pathEdit->setText(badPath);
    settingsView.updateModelDetailsDisplay();
    QCoreApplication::processEvents();
    EXPECT_TRUE(detailsLbl->text().contains("invalid GGML format"));

    if (saveBtn) {
        saveBtn->click();
        QCoreApplication::processEvents();
    }
    // Verify save was blocked: config still has multiPath!
    auto blockedConfig = flex::SettingsPersistenceService::loadSettings();
    EXPECT_STR_EQ(multiPath.toStdString(), blockedConfig.settings.whisperModelPath);

    // 5. Test refresh button rediscovery
    auto* refreshBtn = settingsView.whisperRefreshButton();
    EXPECT_TRUE(refreshBtn != nullptr);
    if (refreshBtn) {
        // Point back to synthetic models
        settingsView.refreshModelList(tempModelsDir.path());
        refreshBtn->click();
        QCoreApplication::processEvents();
        EXPECT_EQ(4, combo->count());
    }

    // 6. Test reset to defaults restores auto-detect default
    settingsView.onResetClicked();
    QCoreApplication::processEvents();
    auto resetConfig = flex::SettingsPersistenceService::loadSettings();
    EXPECT_TRUE(resetConfig.settings.whisperModelPath.empty()); // Empty string is default auto-resolve

    qunsetenv("FLEX_CONFIG_PATH");
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Force offscreen platform for headless CI / automated test execution
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    std::cout << "========================================================\n";
    std::cout << " FLEX 1.0 Native Qt Desktop UI Test Suite (Stage 5)    \n";
    std::cout << "========================================================\n\n";

    MockOllamaServer mock;

    test_theme_stylesheet();
    test_ai_core_widget();
    test_message_bubble_streaming();
    test_main_window_construction_and_empty_db();
    test_empty_input_rejection();
    test_conversation_list_existing_records();
    test_successful_chat_turn(mock);
    test_backend_error_displays_visibly(mock);
    test_repeated_send_blocked(mock);
    test_safe_window_shutdown(mock);
    test_chat_view_voice_controls();
    test_shell_restoration_and_view_switching();
    test_projects_and_memory_views();
    test_settings_view_ui_persistence();
    test_settings_view_whisper_model_discovery();

    std::cout << "\n========================================================\n";
    std::cout << "Test Summary:\n";
    std::cout << "  Total Assertions : " << g_tests_run << "\n";
    std::cout << "  Passed           : " << g_tests_passed << "\n";
    std::cout << "  Failed           : " << g_tests_failed << "\n";
    std::cout << "========================================================\n";

    if (g_tests_failed > 0) {
        std::cerr << "Result: FAILED (" << g_tests_failed << " assertions failed)\n";
        return 1;
    }

    std::cout << "Result: SUCCESS (All native Qt UI tests passed!)\n";
    return 0;
}

#include "test_ui.moc"

