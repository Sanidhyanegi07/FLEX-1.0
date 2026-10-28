#include "flex/backend.hpp"

#include <QCoreApplication>
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

    [[nodiscard]] uint16_t port() const
    {
        return m_server->serverPort();
    }

    [[nodiscard]] std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port());
    }

    void reset()
    {
        m_statusCode = 200;
        m_statusText = "OK";
        m_responseBody.clear();
        m_contentType = "application/json";
        m_isStreaming = false;
        m_chunks.clear();
        m_delayMs = 0;
        m_lastRequestBody.clear();
        m_lastRequestMethod.clear();
        m_lastRequestPath.clear();
    }

    void setResponse(int code, const std::string& statusText, const std::string& body,
                     const std::string& contentType = "application/json")
    {
        m_statusCode = code;
        m_statusText = statusText;
        m_responseBody = body;
        m_contentType = contentType;
        m_isStreaming = false;
        m_chunks.clear();
    }

    void setStreamingResponse(const std::vector<std::string>& chunks)
    {
        m_isStreaming = true;
        m_chunks = chunks;
        m_statusCode = 200;
        m_statusText = "OK";
    }

    void setDelay(int delayMs)
    {
        m_delayMs = delayMs;
    }

    [[nodiscard]] const QByteArray& lastRequestBody() const
    {
        return m_lastRequestBody;
    }

private slots:
    void onNewConnection()
    {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray data = socket->readAll();
            int headerEnd = data.indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
                QString headerPart = QString::fromUtf8(data.left(headerEnd));
                QStringList lines = headerPart.split("\r\n");
                if (!lines.isEmpty()) {
                    QStringList reqLine = lines[0].split(' ');
                    if (reqLine.size() >= 2) {
                        m_lastRequestMethod = reqLine[0];
                        m_lastRequestPath = reqLine[1];
                    }
                }
                m_lastRequestBody = data.mid(headerEnd + 4);

                if (m_delayMs > 0 && !m_isStreaming) {
                    QPointer<QTcpSocket> safeSocket = socket;
                    QTimer::singleShot(m_delayMs, this, [this, safeSocket]() {
                        if (safeSocket) {
                            sendNormalResponse(safeSocket.data());
                        }
                    });
                } else if (m_isStreaming) {
                    sendStreamingResponse(socket);
                } else {
                    sendNormalResponse(socket);
                }
            }
        });
    }

private:
    void sendNormalResponse(QTcpSocket* socket)
    {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        QByteArray resp;
        resp.append(QString("HTTP/1.1 %1 %2\r\n").arg(m_statusCode).arg(QString::fromStdString(m_statusText)).toUtf8());
        resp.append(QString("Content-Type: %1\r\n").arg(QString::fromStdString(m_contentType)).toUtf8());
        resp.append(QString("Content-Length: %1\r\n").arg(m_responseBody.size()).toUtf8());
        resp.append("Connection: close\r\n\r\n");
        resp.append(QByteArray::fromStdString(m_responseBody));

        socket->write(resp);
        socket->flush();
        socket->disconnectFromHost();
    }

    void sendStreamingResponse(QTcpSocket* socket)
    {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        QByteArray resp;
        resp.append("HTTP/1.1 200 OK\r\n");
        resp.append("Content-Type: application/x-ndjson\r\n");
        resp.append("Connection: close\r\n\r\n");
        for (const auto& chunk : m_chunks) {
            resp.append(QByteArray::fromStdString(chunk));
        }
        socket->write(resp);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer* m_server{nullptr};
    int m_statusCode{200};
    std::string m_statusText{"OK"};
    std::string m_responseBody;
    std::string m_contentType{"application/json"};
    bool m_isStreaming{false};
    std::vector<std::string> m_chunks;
    int m_delayMs{0};

    QByteArray m_lastRequestBody;
    QString m_lastRequestMethod;
    QString m_lastRequestPath;
};

// -----------------------------------------------------------------------------
// Temporary Database Guard for Test Isolation
// -----------------------------------------------------------------------------

class TempDbGuard {
public:
    TempDbGuard()
    {
        m_path = QDir::tempPath().toStdString() + "/flex_test_backend_" +
                 QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() + ".db";
        QFile::remove(QString::fromStdString(m_path));
    }

    ~TempDbGuard()
    {
        QFile::remove(QString::fromStdString(m_path));
    }

    [[nodiscard]] const std::string& path() const noexcept
    {
        return m_path;
    }

private:
    std::string m_path;
};

// -----------------------------------------------------------------------------
// Test 1: Valid Chat Request & End-to-End Execution
// -----------------------------------------------------------------------------
void test_valid_chat_request(MockOllamaServer& mock)
{
    std::cout << "[Test 1] Valid chat request and structured result...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({
        "model": "qwen2.5-coder:3b",
        "message": {"role": "assistant", "content": "I am FLEX 1.0, ready to assist."},
        "done": true,
        "total_duration": 420000000,
        "eval_count": 12
    })");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.message = "Hello, what is your name?";

    flex::ChatResult result = coordinator.sendChatSync(req);

    EXPECT_TRUE(result.success);
    EXPECT_STR_EQ("I am FLEX 1.0, ready to assist.", result.responseText);
    EXPECT_STR_EQ("Hello, what is your name?", result.userPrompt);
    EXPECT_STR_EQ("qwen2.5-coder:3b", result.model);
    EXPECT_FALSE(result.conversationId.empty());
    EXPECT_FALSE(result.userMessageId.empty());
    EXPECT_FALSE(result.assistantMessageId.empty());
    EXPECT_TRUE(result.operationId > 0);
    EXPECT_EQ(420000000LL, result.totalDurationNs);
    EXPECT_EQ(12LL, result.evalCount);

    // C core must be cleanly back in IDLE
    EXPECT_TRUE(coordinator.coreSession().isIdle());
    EXPECT_FALSE(coordinator.coreSession().isProcessing());
}

// -----------------------------------------------------------------------------
// Test 2: Creation of a New Conversation
// -----------------------------------------------------------------------------
void test_creation_of_new_conversation(MockOllamaServer& mock)
{
    std::cout << "[Test 2] Creation of a new conversation when none provided...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Created."}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.message = "Start a fresh topic";

    flex::ChatResult res = coordinator.sendChatSync(req);

    EXPECT_TRUE(res.success);
    EXPECT_FALSE(res.conversationId.empty());

    auto conv = coordinator.getConversation(res.conversationId);
    EXPECT_TRUE(conv.has_value());
    EXPECT_STR_EQ(res.conversationId, conv->id);
}

// -----------------------------------------------------------------------------
// Test 3: Request Using an Existing Conversation
// -----------------------------------------------------------------------------
void test_request_using_existing_conversation(MockOllamaServer& mock)
{
    std::cout << "[Test 3] Request explicitly targeting an existing conversation ID...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Continuing conversation."}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    std::string cid = coordinator.createConversation("Engineering Project");
    EXPECT_FALSE(cid.empty());

    flex::ChatRequest req;
    req.conversationId = cid;
    req.message = "Design review item 1";

    flex::ChatResult res = coordinator.sendChatSync(req);

    EXPECT_TRUE(res.success);
    EXPECT_STR_EQ(cid, res.conversationId);

    auto messages = coordinator.getConversationMessages(cid);
    EXPECT_EQ(2, static_cast<int>(messages.size()));
}

// -----------------------------------------------------------------------------
// Test 4: User and Assistant Message Persistence
// -----------------------------------------------------------------------------
void test_user_and_assistant_message_persistence(MockOllamaServer& mock)
{
    std::cout << "[Test 4] Persistent database verification of user and assistant records...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Response content verified."}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.message = "Save this inquiry";

    flex::ChatResult res = coordinator.sendChatSync(req);
    EXPECT_TRUE(res.success);

    auto msgs = coordinator.getConversationMessages(res.conversationId);
    EXPECT_EQ(2, static_cast<int>(msgs.size()));

    // Verify User Record
    EXPECT_STR_EQ("user", msgs[0].role);
    EXPECT_STR_EQ("Save this inquiry", msgs[0].content);
    EXPECT_STR_EQ(res.userMessageId, msgs[0].id);
    EXPECT_EQ(0LL, msgs[0].sequenceNum);

    // Verify Assistant Record
    EXPECT_STR_EQ("assistant", msgs[1].role);
    EXPECT_STR_EQ("Response content verified.", msgs[1].content);
    EXPECT_STR_EQ(res.assistantMessageId, msgs[1].id);
    EXPECT_EQ(1LL, msgs[1].sequenceNum);
}

// -----------------------------------------------------------------------------
// Test 5: Conversation History Ordering and System Prompt Context
// -----------------------------------------------------------------------------
void test_conversation_history_ordering(MockOllamaServer& mock)
{
    std::cout << "[Test 5] Conversation history ordering in outgoing Ollama payload...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Turn 1 reply."}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    std::string cid = coordinator.createConversation("History Test");

    // Turn 1
    flex::ChatRequest req1;
    req1.conversationId = cid;
    req1.message = "First message";
    auto res1 = coordinator.sendChatSync(req1);
    EXPECT_TRUE(res1.success);

    // Turn 2
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Turn 2 reply."}, "done": true})");
    flex::ChatRequest req2;
    req2.conversationId = cid;
    req2.message = "Second message";
    auto res2 = coordinator.sendChatSync(req2);
    EXPECT_TRUE(res2.success);

    // Inspect request sent for Turn 2
    QJsonDocument doc = QJsonDocument::fromJson(mock.lastRequestBody());
    EXPECT_TRUE(doc.isObject());
    QJsonArray msgsArr = doc.object()["messages"].toArray();

    // Expected: [system, user(First), assistant(Turn 1), user(Second)]
    EXPECT_EQ(4, msgsArr.size());
    EXPECT_STR_EQ("system", msgsArr[0].toObject()["role"].toString().toStdString());
    EXPECT_STR_EQ("user", msgsArr[1].toObject()["role"].toString().toStdString());
    EXPECT_STR_EQ("First message", msgsArr[1].toObject()["content"].toString().toStdString());
    EXPECT_STR_EQ("assistant", msgsArr[2].toObject()["role"].toString().toStdString());
    EXPECT_STR_EQ("Turn 1 reply.", msgsArr[2].toObject()["content"].toString().toStdString());
    EXPECT_STR_EQ("user", msgsArr[3].toObject()["role"].toString().toStdString());
    EXPECT_STR_EQ("Second message", msgsArr[3].toObject()["content"].toString().toStdString());
}

// -----------------------------------------------------------------------------
// Test 6: Multiple Sequential Chat Requests
// -----------------------------------------------------------------------------
void test_multiple_sequential_chat_requests(MockOllamaServer& mock)
{
    std::cout << "[Test 6] Sequential chat execution and state machine stability...\n";
    mock.reset();

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);
    std::string cid = coordinator.createConversation("Sequential Test");

    for (int i = 1; i <= 3; ++i) {
        mock.setResponse(200, "OK", "{\"message\": {\"role\": \"assistant\", \"content\": \"Reply " + std::to_string(i) + "\"}, \"done\": true}");
        flex::ChatRequest req;
        req.conversationId = cid;
        req.message = "User step " + std::to_string(i);

        flex::ChatResult res = coordinator.sendChatSync(req);
        EXPECT_TRUE(res.success);
        EXPECT_STR_EQ("Reply " + std::to_string(i), res.responseText);
        EXPECT_TRUE(coordinator.coreSession().isIdle());
    }

    EXPECT_EQ(6LL, coordinator.getMessageCount(cid));
}

// -----------------------------------------------------------------------------
// Test 7: Empty or Invalid User Input Rejection
// -----------------------------------------------------------------------------
void test_empty_or_invalid_user_input(MockOllamaServer& mock)
{
    std::cout << "[Test 7] Empty and whitespace-only user input rejection...\n";
    mock.reset();

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    std::vector<std::string> invalidInputs = {"", "   ", "\t\t", "\n\r  \n"};
    for (const auto& input : invalidInputs) {
        flex::ChatRequest req;
        req.message = input;

        flex::BackendError err;
        flex::ChatResult res = coordinator.sendChatSync(req, &err);

        EXPECT_FALSE(res.success);
        EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::InvalidInput), static_cast<int>(err.category));
        EXPECT_TRUE(coordinator.coreSession().isIdle());
    }
}

// -----------------------------------------------------------------------------
// Test 8: Non-Existent Conversation ID Handling
// -----------------------------------------------------------------------------
void test_invalid_conversation_id(MockOllamaServer& mock)
{
    std::cout << "[Test 8] Rejection of non-existent conversation IDs...\n";
    mock.reset();

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.conversationId = "nonexistent-conversation-uuid-999";
    req.message = "Hello there";

    flex::BackendError err;
    flex::ChatResult res = coordinator.sendChatSync(req, &err);

    EXPECT_FALSE(res.success);
    EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::InvalidInput), static_cast<int>(err.category));
    EXPECT_TRUE(coordinator.coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 9: Ollama HTTP 500 Error
// -----------------------------------------------------------------------------
void test_ollama_http_error(MockOllamaServer& mock)
{
    std::cout << "[Test 9] Ollama HTTP 500 server error handling...\n";
    mock.reset();
    mock.setResponse(500, "Internal Server Error", R"({"error": "cuda out of memory"})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.message = "Generate large model tensor";

    flex::BackendError err;
    flex::ChatResult res = coordinator.sendChatSync(req, &err);

    EXPECT_FALSE(res.success);
    EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::AiHttpError), static_cast<int>(err.category));
    EXPECT_EQ(500, err.statusCode);
    EXPECT_TRUE(coordinator.coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 10: Ollama Timeout and Daemon Unreachable Handling
// -----------------------------------------------------------------------------
void test_ollama_timeout_and_unreachable(MockOllamaServer& mock)
{
    std::cout << "[Test 10] Ollama timeout and connection refusal handling...\n";
    mock.reset();

    // 10a: Timeout
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Late"}, "done": true})");
    mock.setDelay(400); // 400ms delay

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    cfg.timeoutSeconds = 0.1; // 100ms timeout
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.message = "Query that exceeds timeout";
    req.timeoutSeconds = 0.1;

    flex::BackendError errTimeout;
    flex::ChatResult resTimeout = coordinator.sendChatSync(req, &errTimeout);

    EXPECT_FALSE(resTimeout.success);
    EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::AiTimeout), static_cast<int>(errTimeout.category));
    EXPECT_TRUE(coordinator.coreSession().isIdle());

    // 10b: Connection Refusal on closed port
    flex::OllamaConfig closedCfg;
    closedCfg.baseUrl = "http://127.0.0.1:58499";
    closedCfg.timeoutSeconds = 1.0;
    auto closedClient = std::make_shared<flex::OllamaClient>(closedCfg);
    flex::BackendCoordinator unreachableCoordinator(storage, closedClient);

    flex::BackendError errRefused;
    flex::ChatResult resRefused = unreachableCoordinator.sendChatSync(req, &errRefused);

    EXPECT_FALSE(resRefused.success);
    bool isNetOrTimeout = (errRefused.category == flex::BackendErrorCategory::AiNetworkError ||
                           errRefused.category == flex::BackendErrorCategory::AiTimeout);
    EXPECT_TRUE(isNetOrTimeout);
    EXPECT_TRUE(unreachableCoordinator.coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 11: Malformed AI Response
// -----------------------------------------------------------------------------
void test_malformed_ai_response(MockOllamaServer& mock)
{
    std::cout << "[Test 11] Malformed JSON response from AI daemon...\n";
    mock.reset();
    mock.setResponse(200, "OK", "{ invalid json content not closing properly...");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.message = "Request with corrupt AI response";

    flex::BackendError err;
    flex::ChatResult res = coordinator.sendChatSync(req, &err);

    EXPECT_FALSE(res.success);
    EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::AiMalformedResponse), static_cast<int>(err.category));
    EXPECT_TRUE(coordinator.coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 12: Storage Write Failure Handling
// -----------------------------------------------------------------------------
void test_storage_write_failure(MockOllamaServer& mock)
{
    std::cout << "[Test 12] Storage failure handling when database is closed...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Ok"}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    // Intentionally close storage connection
    storage->close();

    flex::ChatRequest req;
    req.message = "Write to closed database";

    flex::BackendError err;
    flex::ChatResult res = coordinator.sendChatSync(req, &err);

    EXPECT_FALSE(res.success);
    EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::StorageError), static_cast<int>(err.category));
    EXPECT_TRUE(coordinator.coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 13: User Message Preserved on AI Failure (No Fabricated Assistant Msg)
// -----------------------------------------------------------------------------
void test_ai_failure_after_user_message_saved(MockOllamaServer& mock)
{
    std::cout << "[Test 13] User message preservation and unpolluted assistant state upon AI failure...\n";
    mock.reset();
    mock.setResponse(500, "Internal Server Error", R"({"error": "daemon crashed"})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);
    std::string cid = coordinator.createConversation("Failure Topic");

    flex::ChatRequest req;
    req.conversationId = cid;
    req.message = "Critical prompt that fails on server";

    flex::ChatResult res = coordinator.sendChatSync(req);
    EXPECT_FALSE(res.success);

    // Verify storage state: User prompt IS preserved, but assistant message was NOT fabricated or persisted
    auto msgs = coordinator.getConversationMessages(cid);
    EXPECT_EQ(1, static_cast<int>(msgs.size()));
    EXPECT_STR_EQ("user", msgs[0].role);
    EXPECT_STR_EQ("Critical prompt that fails on server", msgs[0].content);
}

// -----------------------------------------------------------------------------
// Test 14: Request Cancellation
// -----------------------------------------------------------------------------
void test_request_cancellation(MockOllamaServer& mock)
{
    std::cout << "[Test 14] Request cancellation and C core cleanup...\n";
    mock.reset();
    mock.setDelay(1000); // 1000ms delay
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Done"}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    cfg.timeoutSeconds = 5.0;
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    bool errorCalled = false;
    flex::BackendError capturedErr;
    QEventLoop loop;

    flex::ChatRequest req;
    req.message = "Cancel this operation";

    uint64_t reqId = coordinator.sendChatAsync(
        req,
        [&](const flex::ChatResult&) {
            loop.quit();
        },
        [&](const flex::BackendError& err) {
            errorCalled = true;
            capturedErr = err;
            loop.quit();
        }
    );

    EXPECT_TRUE(reqId > 0);

    // Cancel after 40ms
    QTimer::singleShot(40, &coordinator, [&]() {
        bool cancelled = coordinator.cancelRequest(reqId);
        EXPECT_TRUE(cancelled);
    });

    loop.exec();

    EXPECT_TRUE(errorCalled);
    EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::Cancelled), static_cast<int>(capturedErr.category));
    EXPECT_TRUE(coordinator.coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 15: Correct Operation Completion on Success
// -----------------------------------------------------------------------------
void test_operation_completion_on_success(MockOllamaServer& mock)
{
    std::cout << "[Test 15] Verifying single clean C operation completion on success...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Clean success"}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    flex::ChatRequest req;
    req.message = "Perform turn";

    flex::ChatResult res = coordinator.sendChatSync(req);
    EXPECT_TRUE(res.success);

    // State must be IDLE and repeated finish must return ERROR_NOT_PROCESSING
    EXPECT_TRUE(coordinator.coreSession().isIdle());
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, coordinator.coreSession().finishOperation());
}

// -----------------------------------------------------------------------------
// Test 16: Correct Operation Completion on Every Failure Path
// -----------------------------------------------------------------------------
void test_operation_completion_on_every_failure_path(MockOllamaServer& mock)
{
    std::cout << "[Test 16] Operation lifecycle finished on every failure branch...\n";
    mock.reset();

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    // 16a. Empty input (never started)
    coordinator.sendChatSync({""});
    EXPECT_TRUE(coordinator.coreSession().isIdle());

    // 16b. HTTP 404 (started and finished)
    mock.setResponse(404, "Not Found", R"({"error": "model not found"})");
    coordinator.sendChatSync({"Prompt 404"});
    EXPECT_TRUE(coordinator.coreSession().isIdle());

    // 16c. HTTP 500 (started and finished)
    mock.setResponse(500, "Server Error", R"({"error": "server error"})");
    coordinator.sendChatSync({"Prompt 500"});
    EXPECT_TRUE(coordinator.coreSession().isIdle());

    // 16d. Malformed JSON (started and finished)
    mock.setResponse(200, "OK", "bad json {");
    coordinator.sendChatSync({"Prompt malformed"});
    EXPECT_TRUE(coordinator.coreSession().isIdle());
}

// -----------------------------------------------------------------------------
// Test 17: Prevention of Duplicate Writes During Retries
// -----------------------------------------------------------------------------
void test_duplicate_prevention_on_retries(MockOllamaServer& mock)
{
    std::cout << "[Test 17] Duplicate prevention when retrying with userMessageId...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Attempt success."}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);
    std::string cid = coordinator.createConversation("Retry Topic");

    std::string customId = "client-msg-uuid-999";

    // First attempt
    flex::ChatRequest req1;
    req1.conversationId = cid;
    req1.message = "Same content";
    req1.userMessageId = customId;

    auto res1 = coordinator.sendChatSync(req1);
    EXPECT_TRUE(res1.success);
    EXPECT_STR_EQ(customId, res1.userMessageId);

    // Retry with identical custom userMessageId
    flex::ChatRequest req2;
    req2.conversationId = cid;
    req2.message = "Same content";
    req2.userMessageId = customId;

    auto res2 = coordinator.sendChatSync(req2);
    EXPECT_TRUE(res2.success);
    EXPECT_STR_EQ(customId, res2.userMessageId);

    // Messages in database should not duplicate the user message
    auto msgs = coordinator.getConversationMessages(cid);
    int userMsgCount = 0;
    for (const auto& m : msgs) {
        if (m.role == "user" && m.id == customId) {
            userMsgCount++;
        }
    }
    EXPECT_EQ(1, userMsgCount);
}

// -----------------------------------------------------------------------------
// Test 18: Context Limits and Large Input Handling (Bypassing C 1024 Limit)
// -----------------------------------------------------------------------------
void test_context_limits_and_large_input_handling(MockOllamaServer& mock)
{
    std::cout << "[Test 18] Large input handling exceeding C 1024-byte limit & context window capping...\n";
    mock.reset();
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Large payload processed."}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    // 18a: Large prompt: 4000 characters (> 1024 byte C core parser limit!)
    std::string largePrompt(4000, 'A');
    flex::ChatRequest req;
    req.message = largePrompt;

    flex::ChatResult res = coordinator.sendChatSync(req);

    // Must succeed without FLEX_STATUS_INPUT_TOO_LONG error because C core lifecycle is decoupled from payload
    EXPECT_TRUE(res.success);
    EXPECT_EQ(4000, static_cast<int>(res.userPrompt.size()));
    EXPECT_STR_EQ("Large payload processed.", res.responseText);
    EXPECT_TRUE(coordinator.coreSession().isIdle());

    // 18b: Context limits: verify request sends at most maxHistoryMessages
    std::string cid = res.conversationId;
    flex::ChatRequest req2;
    req2.conversationId = cid;
    req2.message = "Short prompt";
    req2.maxHistoryMessages = 2; // Only 2 recent history messages

    coordinator.sendChatSync(req2);

    QJsonDocument doc = QJsonDocument::fromJson(mock.lastRequestBody());
    QJsonArray msgsArr = doc.object()["messages"].toArray();
    // 1 system prompt + at most 2 history/current messages
    EXPECT_TRUE(msgsArr.size() <= 3);
}

// -----------------------------------------------------------------------------
// Test 19: Unicode, Emojis, and Multiline Code
// -----------------------------------------------------------------------------
void test_unicode_and_multiline_messages(MockOllamaServer& mock)
{
    std::cout << "[Test 19] Unicode emojis, Japanese characters, and multiline code blocks...\n";
    mock.reset();
    std::string aiResponse = "🤖 解析完了: コードは正常です。";
    mock.setResponse(200, "OK", "{\"message\": {\"role\": \"assistant\", \"content\": \"" + aiResponse + "\"}, \"done\": true}");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    std::string multilinePrompt =
        "// Task: Verify unicode ✨🚀\n"
        "#include <iostream>\n"
        "int main() {\n"
        "    std::cout << \"こんにちは世界!\\n\";\n"
        "    return 0;\n"
        "}";

    flex::ChatRequest req;
    req.message = multilinePrompt;

    flex::ChatResult res = coordinator.sendChatSync(req);

    EXPECT_TRUE(res.success);
    EXPECT_STR_EQ(multilinePrompt, res.userPrompt);
    EXPECT_STR_EQ(aiResponse, res.responseText);

    auto msgs = coordinator.getConversationMessages(res.conversationId);
    EXPECT_EQ(2, static_cast<int>(msgs.size()));
    EXPECT_STR_EQ(multilinePrompt, msgs[0].content);
    EXPECT_STR_EQ(aiResponse, msgs[1].content);
}

// -----------------------------------------------------------------------------
// Test 20: Safe Destruction While In-Flight
// -----------------------------------------------------------------------------
void test_safe_destruction_while_in_flight(MockOllamaServer& mock)
{
    std::cout << "[Test 20] Safe destruction and cleanup with in-flight requests...\n";
    mock.reset();
    mock.setDelay(500);

    TempDbGuard db;
    {
        auto storage = std::make_shared<flex::ConversationStorage>(db.path());
        flex::OllamaConfig cfg;
        cfg.baseUrl = mock.url();
        auto client = std::make_shared<flex::OllamaClient>(cfg);

        auto coordinator = std::make_unique<flex::BackendCoordinator>(storage, client);

        flex::ChatRequest req;
        req.message = "Pending query";
        coordinator->sendChatAsync(req, nullptr, nullptr);

        // Coordinator is destroyed here with pending request
    }

    QCoreApplication::processEvents();
    EXPECT_TRUE(true); // Reached safely without crash or assertion failure
}

// -----------------------------------------------------------------------------
// Test 21: Concurrent Request Rejection and Orphan Prevention
// -----------------------------------------------------------------------------
void test_concurrent_request_rejection_and_no_orphans(MockOllamaServer& mock)
{
    std::cout << "[Test 21] Concurrent request rejection by C core and orphan prevention...\n";
    mock.reset();
    mock.setDelay(200); // 200ms delay for Request 1
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Request 1 reply"}, "done": true})");

    TempDbGuard db;
    auto storage = std::make_shared<flex::ConversationStorage>(db.path());
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    auto client = std::make_shared<flex::OllamaClient>(cfg);

    flex::BackendCoordinator coordinator(storage, client);

    QEventLoop loop;
    bool req1Finished = false;
    flex::ChatResult req1Result;

    // Start Request 1 (in-flight)
    flex::ChatRequest req1;
    req1.message = "Request 1 prompt";

    uint64_t id1 = coordinator.sendChatAsync(
        req1,
        [&](const flex::ChatResult& res) {
            req1Finished = true;
            req1Result = res;
            loop.quit();
        },
        [&](const flex::BackendError&) {
            req1Finished = true;
            loop.quit();
        }
    );
    EXPECT_TRUE(id1 > 0);
    EXPECT_TRUE(coordinator.coreSession().isProcessing());

    // Record conversation count after Request 1 began
    int64_t convCountBefore = storage->getConversationCount();

    // Now attempt Request 2 while Request 1 is still in-flight
    flex::ChatRequest req2;
    req2.message = "Request 2 concurrent prompt";

    flex::BackendError req2Err;
    flex::ChatResult req2Res = coordinator.sendChatSync(req2, &req2Err);

    // Request 2 MUST be rejected with CoreRejected because C core is busy
    EXPECT_FALSE(req2Res.success);
    EXPECT_EQ(static_cast<int>(flex::BackendErrorCategory::CoreRejected), static_cast<int>(req2Err.category));

    // Request 2 MUST NOT have created an orphaned conversation
    int64_t convCountAfter = storage->getConversationCount();
    EXPECT_EQ(convCountBefore, convCountAfter);

    // Wait for Request 1 to settle cleanly
    if (!req1Finished) {
        loop.exec();
    }

    EXPECT_TRUE(req1Result.success);
    EXPECT_STR_EQ("Request 1 reply", req1Result.responseText);
    EXPECT_TRUE(coordinator.coreSession().isIdle());
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "========================================================\n";
    std::cout << " FLEX 1.0 Native C++ Backend Integration Test Suite     \n";
    std::cout << " Stage 4: C Core + Ollama Client + SQLite Storage       \n";
    std::cout << "========================================================\n\n";

    MockOllamaServer mock;

    test_valid_chat_request(mock);
    test_creation_of_new_conversation(mock);
    test_request_using_existing_conversation(mock);
    test_user_and_assistant_message_persistence(mock);
    test_conversation_history_ordering(mock);
    test_multiple_sequential_chat_requests(mock);
    test_empty_or_invalid_user_input(mock);
    test_invalid_conversation_id(mock);
    test_ollama_http_error(mock);
    test_ollama_timeout_and_unreachable(mock);
    test_malformed_ai_response(mock);
    test_storage_write_failure(mock);
    test_ai_failure_after_user_message_saved(mock);
    test_request_cancellation(mock);
    test_operation_completion_on_success(mock);
    test_operation_completion_on_every_failure_path(mock);
    test_duplicate_prevention_on_retries(mock);
    test_context_limits_and_large_input_handling(mock);
    test_unicode_and_multiline_messages(mock);
    test_safe_destruction_while_in_flight(mock);
    test_concurrent_request_rejection_and_no_orphans(mock);

    std::cout << "\n========================================================\n";
    std::cout << "Test Summary:\n";
    std::cout << "  Total Assertions : " << g_tests_run << "\n";
    std::cout << "  Passed           : " << g_tests_passed << "\n";
    std::cout << "  Failed           : " << g_tests_failed << "\n";
    std::cout << "========================================================\n";

    if (g_tests_failed == 0) {
        std::cout << "Result: SUCCESS (All native C++ backend integration tests passed!)\n";
        return 0;
    }

    std::cout << "Result: FAILURE (" << g_tests_failed << " assertion(s) failed!)\n";
    return 1;
}

#include "test_backend.moc"
