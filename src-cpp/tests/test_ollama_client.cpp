#include "flex/ollama_client.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cassert>
#include <cstdlib>
#include <iostream>
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
// Deterministic In-Process Mock HTTP Server
// -----------------------------------------------------------------------------
class MockOllamaServer : public QObject {
    Q_OBJECT
public:
    explicit MockOllamaServer(QObject* parent = nullptr)
        : QObject(parent), m_server(new QTcpServer(this))
    {
        connect(m_server, &QTcpServer::newConnection, this, &MockOllamaServer::onNewConnection);
        m_server->listen(QHostAddress::LocalHost, 0); // Listen on an ephemeral port
    }

    [[nodiscard]] uint16_t port() const noexcept
    {
        return m_server->serverPort();
    }

    [[nodiscard]] std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port());
    }

    void setResponse(int statusCode, const std::string& statusText,
                     const std::string& body, const std::string& contentType = "application/json")
    {
        m_statusCode = statusCode;
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
        m_delayMs = 0;
    }

    void setDelay(int delayMs)
    {
        m_delayMs = delayMs;
    }

    [[nodiscard]] QByteArray lastRequestBody() const
    {
        return m_lastRequestBody;
    }

    [[nodiscard]] QString lastRequestPath() const
    {
        return m_lastRequestPath;
    }

    void reset()
    {
        m_lastRequestBody.clear();
        m_lastRequestMethod.clear();
        m_lastRequestPath.clear();
        m_delayMs = 0;
        m_isStreaming = false;
        m_chunks.clear();
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
                    QTimer::singleShot(m_delayMs, this, [this, socket]() {
                        sendNormalResponse(socket);
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
// Test 1: Valid Successful Response & Field Extraction
// -----------------------------------------------------------------------------
void test_valid_successful_response(MockOllamaServer& mock)
{
    std::cout << "[Test 1] Valid successful response...\n";
    mock.reset();

    std::string responsePayload =
        R"({
            "model": "qwen2.5-coder:3b",
            "created_at": "2026-10-28T12:00:00Z",
            "message": {
                "role": "assistant",
                "content": "Hello from mock Ollama!"
            },
            "done_reason": "stop",
            "done": true,
            "total_duration": 450000000,
            "load_duration": 50000000,
            "prompt_eval_count": 8,
            "eval_count": 16,
            "eval_duration": 400000000
        })";

    mock.setResponse(200, "OK", responsePayload);

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    cfg.model = "qwen2.5-coder:3b";
    flex::OllamaClient client(cfg);

    std::vector<flex::ChatMessage> messages = {
        {"user", "Hello there"}
    };

    flex::OllamaError err;
    flex::ChatResponse resp = client.chatSync(messages, std::nullopt, &err);

    EXPECT_FALSE(err.hasError());
    EXPECT_TRUE(resp.success);
    EXPECT_EQ(200, resp.statusCode);
    EXPECT_STR_EQ("Hello from mock Ollama!", resp.text);
    EXPECT_STR_EQ("qwen2.5-coder:3b", resp.model);
    EXPECT_EQ(450000000LL, resp.totalDurationNs);
    EXPECT_EQ(50000000LL, resp.loadDurationNs);
    EXPECT_EQ(8LL, resp.promptEvalCount);
    EXPECT_EQ(16LL, resp.evalCount);
    EXPECT_EQ(400000000LL, resp.evalDurationNs);
    EXPECT_STR_EQ("stop", resp.doneReason);

    // Verify request payload sent to server
    QJsonDocument reqDoc = QJsonDocument::fromJson(mock.lastRequestBody());
    EXPECT_TRUE(reqDoc.isObject());
    QJsonObject reqObj = reqDoc.object();
    EXPECT_STR_EQ("qwen2.5-coder:3b", reqObj["model"].toString().toStdString());
    EXPECT_FALSE(reqObj["stream"].toBool());
}

// -----------------------------------------------------------------------------
// Test 2: Multiple Messages with Correct Roles
// -----------------------------------------------------------------------------
void test_multiple_messages_with_roles(MockOllamaServer& mock)
{
    std::cout << "[Test 2] Multiple conversation messages with roles...\n";
    mock.reset();

    std::string responsePayload =
        R"({
            "model": "qwen2.5-coder:3b",
            "message": {"role": "assistant", "content": "Acknowledged history."},
            "done": true
        })";

    mock.setResponse(200, "OK", responsePayload);

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    std::vector<flex::ChatMessage> messages = {
        {"system", "You are an expert C++ developer."},
        {"user", "Explain RAII."},
        {"assistant", "RAII binds life of resources to object lifetime."},
        {"user", "Give an example."}
    };

    flex::ChatResponse resp = client.chatSync(messages);
    EXPECT_TRUE(resp.success);
    EXPECT_STR_EQ("Acknowledged history.", resp.text);

    // Verify all 4 messages in request body
    QJsonDocument reqDoc = QJsonDocument::fromJson(mock.lastRequestBody());
    QJsonObject reqObj = reqDoc.object();
    QJsonArray msgsArr = reqObj["messages"].toArray();
    EXPECT_EQ(4, msgsArr.size());

    EXPECT_STR_EQ("system", msgsArr[0].toObject()["role"].toString().toStdString());
    EXPECT_STR_EQ("You are an expert C++ developer.", msgsArr[0].toObject()["content"].toString().toStdString());
    EXPECT_STR_EQ("user", msgsArr[1].toObject()["role"].toString().toStdString());
    EXPECT_STR_EQ("assistant", msgsArr[2].toObject()["role"].toString().toStdString());
    EXPECT_STR_EQ("user", msgsArr[3].toObject()["role"].toString().toStdString());
}

// -----------------------------------------------------------------------------
// Test 3: Unicode and Multiline Content
// -----------------------------------------------------------------------------
void test_unicode_and_multiline(MockOllamaServer& mock)
{
    std::cout << "[Test 3] Unicode emojis, Japanese, and multiline code...\n";
    mock.reset();

    std::string japaneseReply = "こんにちは！🚀 宇宙の探求。";
    std::string responsePayload =
        R"({
            "model": "qwen2.5-coder:3b",
            "message": {"role": "assistant", "content": ")" + japaneseReply + R"("},
            "done": true
        })";

    mock.setResponse(200, "OK", responsePayload);

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    std::string multilinePrompt =
        "#include <iostream>\n"
        "int main() {\n"
        "    // Unicode test: 🌌✨\n"
        "    return 0;\n"
        "}";

    std::vector<flex::ChatMessage> messages = {
        {"user", multilinePrompt}
    };

    flex::ChatResponse resp = client.chatSync(messages);
    EXPECT_TRUE(resp.success);
    EXPECT_STR_EQ(japaneseReply, resp.text);

    // Verify multiline body was cleanly escaped in JSON
    QJsonDocument reqDoc = QJsonDocument::fromJson(mock.lastRequestBody());
    QJsonArray msgsArr = reqDoc.object()["messages"].toArray();
    EXPECT_STR_EQ(multilinePrompt, msgsArr[0].toObject()["content"].toString().toStdString());
}

// -----------------------------------------------------------------------------
// Test 4: Empty and Invalid Requests
// -----------------------------------------------------------------------------
void test_empty_and_invalid_requests(MockOllamaServer& mock)
{
    std::cout << "[Test 4] Empty messages and invalid role rejection...\n";
    mock.reset();

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    // 4a. Empty messages vector
    flex::OllamaError errEmpty;
    flex::ChatResponse respEmpty = client.chatSync({}, std::nullopt, &errEmpty);
    EXPECT_FALSE(respEmpty.success);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::InvalidInput), static_cast<int>(errEmpty.code));

    // 4b. Invalid role string
    flex::OllamaError errRole;
    std::vector<flex::ChatMessage> invalidRoleMsgs = {
        {"superadmin", "Invalid role message"}
    };
    flex::ChatResponse respRole = client.chatSync(invalidRoleMsgs, std::nullopt, &errRole);
    EXPECT_FALSE(respRole.success);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::InvalidInput), static_cast<int>(errRole.code));
}

// -----------------------------------------------------------------------------
// Test 5: Malformed JSON Response
// -----------------------------------------------------------------------------
void test_malformed_json_response(MockOllamaServer& mock)
{
    std::cout << "[Test 5] Malformed JSON response handling...\n";
    mock.reset();

    mock.setResponse(200, "OK", "{ invalid json truncated...");

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    flex::OllamaError err;
    flex::ChatResponse resp = client.chatSync({{"user", "Hi"}}, std::nullopt, &err);

    EXPECT_FALSE(resp.success);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::MalformedJson), static_cast<int>(err.code));
}

// -----------------------------------------------------------------------------
// Test 6: Missing or Unexpected Response Fields
// -----------------------------------------------------------------------------
void test_missing_or_unexpected_fields(MockOllamaServer& mock)
{
    std::cout << "[Test 6] Missing message.content payload handling...\n";
    mock.reset();

    // 6a. Missing "message" object completely
    mock.setResponse(200, "OK", R"({"model": "qwen2.5-coder:3b", "done": true})");

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    flex::OllamaError err;
    flex::ChatResponse resp = client.chatSync({{"user", "Hi"}}, std::nullopt, &err);

    EXPECT_FALSE(resp.success);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::UnexpectedPayload), static_cast<int>(err.code));

    // 6b. Missing "content" inside "message"
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant"}, "done": true})");
    flex::OllamaError errNoContent;
    flex::ChatResponse respNoContent = client.chatSync({{"user", "Hi"}}, std::nullopt, &errNoContent);

    EXPECT_FALSE(respNoContent.success);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::UnexpectedPayload), static_cast<int>(errNoContent.code));
}

// -----------------------------------------------------------------------------
// Test 7: HTTP 500 Server Error
// -----------------------------------------------------------------------------
void test_http_server_error(MockOllamaServer& mock)
{
    std::cout << "[Test 7] HTTP 500 server error handling...\n";
    mock.reset();

    mock.setResponse(500, "Internal Server Error", R"({"error": "cuda out of memory"})");

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    flex::OllamaError err;
    flex::ChatResponse resp = client.chatSync({{"user", "Run GPU task"}}, std::nullopt, &err);

    EXPECT_FALSE(resp.success);
    EXPECT_EQ(500, err.httpStatus);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::HttpError), static_cast<int>(err.code));
}

// -----------------------------------------------------------------------------
// Test 8: HTTP 404 Model Not Found
// -----------------------------------------------------------------------------
void test_model_not_found(MockOllamaServer& mock)
{
    std::cout << "[Test 8] HTTP 404 model not found handling...\n";
    mock.reset();

    mock.setResponse(404, "Not Found", R"({"error": "model 'nonexistent:latest' not found, try pulling it first"})");

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    cfg.model = "nonexistent:latest";
    flex::OllamaClient client(cfg);

    flex::OllamaError err;
    flex::ChatResponse resp = client.chatSync({{"user", "Hi"}}, std::nullopt, &err);

    EXPECT_FALSE(resp.success);
    EXPECT_EQ(404, err.httpStatus);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::ModelNotFound), static_cast<int>(err.code));
}

// -----------------------------------------------------------------------------
// Test 9: Connection Refused / Daemon Unreachable
// -----------------------------------------------------------------------------
void test_connection_refusal()
{
    std::cout << "[Test 9] Connection refusal on closed port...\n";

    flex::OllamaConfig cfg;
    // Closed port on localhost
    cfg.baseUrl = "http://127.0.0.1:58493";
    cfg.timeoutSeconds = 1.0;
    flex::OllamaClient client(cfg);

    flex::OllamaError err;
    flex::ChatResponse resp = client.chatSync({{"user", "Ping"}}, std::nullopt, &err);

    EXPECT_FALSE(resp.success);
    bool isRefusedOrTimeout = (err.code == flex::OllamaErrorCode::NetworkError ||
                               err.code == flex::OllamaErrorCode::Timeout);
    EXPECT_TRUE(isRefusedOrTimeout);
}

// -----------------------------------------------------------------------------
// Test 10: Timeout Handling
// -----------------------------------------------------------------------------
void test_timeout_behavior(MockOllamaServer& mock)
{
    std::cout << "[Test 10] Request timeout handling...\n";
    mock.reset();

    // Configure server to delay response by 500ms
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Too late"}, "done": true})");
    mock.setDelay(500);

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    cfg.timeoutSeconds = 0.1; // 100ms timeout
    flex::OllamaClient client(cfg);

    flex::OllamaError err;
    flex::ChatResponse resp = client.chatSync({{"user", "Slow query"}}, std::nullopt, &err);

    EXPECT_FALSE(resp.success);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::Timeout), static_cast<int>(err.code));
}

// -----------------------------------------------------------------------------
// Test 11: Streaming Responses (NDJSON Chunks)
// -----------------------------------------------------------------------------
void test_streaming_responses(MockOllamaServer& mock)
{
    std::cout << "[Test 11] Streaming NDJSON response assembly...\n";
    mock.reset();

    std::vector<std::string> streamChunks = {
        "{\"model\":\"qwen2.5-coder:3b\",\"message\":{\"role\":\"assistant\",\"content\":\"Hello \"},\"done\":false}\n",
        "{\"model\":\"qwen2.5-coder:3b\",\"message\":{\"role\":\"assistant\",\"content\":\"World!\"},\"done\":false}\n",
        "{\"model\":\"qwen2.5-coder:3b\",\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"done\":true,\"total_duration\":12345,\"eval_count\":2}\n"
    };

    mock.setStreamingResponse(streamChunks);

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    std::vector<std::string> receivedTokens;
    flex::OllamaError err;

    flex::ChatResponse finalResp = client.chatStreamSync(
        {{"user", "Stream test"}},
        [&](const std::string& tokenChunk) {
            receivedTokens.push_back(tokenChunk);
        },
        std::nullopt,
        &err
    );

    EXPECT_FALSE(err.hasError());
    EXPECT_TRUE(finalResp.success);
    EXPECT_STR_EQ("Hello World!", finalResp.text);
    EXPECT_EQ(2, static_cast<int>(receivedTokens.size()));
    EXPECT_STR_EQ("Hello ", receivedTokens[0]);
    EXPECT_STR_EQ("World!", receivedTokens[1]);
    EXPECT_EQ(12345LL, finalResp.totalDurationNs);
    EXPECT_EQ(2LL, finalResp.evalCount);
}

// -----------------------------------------------------------------------------
// Test 12: Malformed Streaming Data
// -----------------------------------------------------------------------------
void test_streaming_malformed_chunk(MockOllamaServer& mock)
{
    std::cout << "[Test 12] Malformed chunk during streaming...\n";
    mock.reset();

    std::vector<std::string> brokenChunks = {
        "{\"model\":\"qwen2.5-coder:3b\",\"message\":{\"role\":\"assistant\",\"content\":\"Start...\"},\"done\":false}\n",
        "{ INVALID JSON CHUNK IN STREAM }\n"
    };

    mock.setStreamingResponse(brokenChunks);

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    flex::OllamaError err;
    flex::ChatResponse finalResp = client.chatStreamSync(
        {{"user", "Stream test"}},
        nullptr,
        std::nullopt,
        &err
    );

    EXPECT_FALSE(finalResp.success);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::MalformedJson), static_cast<int>(err.code));
}

// -----------------------------------------------------------------------------
// Test 13: Request Cancellation
// -----------------------------------------------------------------------------
void test_request_cancellation(MockOllamaServer& mock)
{
    std::cout << "[Test 13] Request cancellation...\n";
    mock.reset();

    mock.setDelay(1000); // 1 second delay
    mock.setResponse(200, "OK", R"({"message": {"role": "assistant", "content": "Done"}, "done": true})");

    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    cfg.timeoutSeconds = 5.0;
    flex::OllamaClient client(cfg);

    bool errorCalled = false;
    flex::OllamaError reportedError;
    QEventLoop loop;

    uint64_t reqId = client.chatAsync(
        {{"user", "Cancel me"}},
        [&](const flex::ChatResponse&) {
            loop.quit();
        },
        [&](const flex::OllamaError& err) {
            errorCalled = true;
            reportedError = err;
            loop.quit();
        }
    );

    EXPECT_TRUE(reqId > 0);

    // Cancel after 50ms
    QTimer::singleShot(50, &client, [&]() {
        bool cancelled = client.cancelRequest(reqId);
        EXPECT_TRUE(cancelled);
    });

    loop.exec();

    EXPECT_TRUE(errorCalled);
    EXPECT_EQ(static_cast<int>(flex::OllamaErrorCode::Cancelled), static_cast<int>(reportedError.code));
}

// -----------------------------------------------------------------------------
// Test 14: Safe Client Cleanup
// -----------------------------------------------------------------------------
void test_safe_cleanup(MockOllamaServer& mock)
{
    std::cout << "[Test 14] Safe destruction and cleanup with in-flight requests...\n";
    mock.reset();
    mock.setDelay(500);

    {
        flex::OllamaConfig cfg;
        cfg.baseUrl = mock.url();
        auto client = std::make_unique<flex::OllamaClient>(cfg);

        // Start multiple requests
        client->chatAsync({{"user", "1"}}, nullptr, nullptr);
        client->chatAsync({{"user", "2"}}, nullptr, nullptr);
        client->chatAsync({{"user", "3"}}, nullptr, nullptr);

        // Client destructs here; should cleanly cancel all in-flight requests without crash
    }

    // Process any deferred delete events
    QCoreApplication::processEvents();
    EXPECT_TRUE(true); // Reached safely without crash or use-after-free
}

// -----------------------------------------------------------------------------
// Test 15: Health Check and Tags Listing
// -----------------------------------------------------------------------------
void test_health_and_tags_helpers(MockOllamaServer& mock)
{
    std::cout << "[Test 15] Health check and model listing helpers...\n";
    mock.reset();

    // 15a. Health check
    mock.setResponse(200, "OK", R"({"version": "0.4.6"})");
    flex::OllamaConfig cfg;
    cfg.baseUrl = mock.url();
    flex::OllamaClient client(cfg);

    EXPECT_TRUE(client.checkHealthSync());

    // 15b. List models and availability
    mock.setResponse(200, "OK", R"({
        "models": [
            {"name": "qwen2.5-coder:3b"},
            {"name": "llama3.2:1b"}
        ]
    })");

    auto models = client.listModelsSync();
    EXPECT_EQ(2, static_cast<int>(models.size()));
    EXPECT_STR_EQ("llama3.2:1b", models[0]);
    EXPECT_STR_EQ("qwen2.5-coder:3b", models[1]);

    EXPECT_TRUE(client.isModelAvailableSync("qwen2.5-coder:3b"));
    EXPECT_TRUE(client.isModelAvailableSync("llama3.2:1b"));
    EXPECT_FALSE(client.isModelAvailableSync("nonexistent-model"));
}

// -----------------------------------------------------------------------------
// Test 16: Opt-in Live Smoke Test (Disabled by default)
// -----------------------------------------------------------------------------
void test_opt_in_live_smoke()
{
    std::cout << "[Test 16] Opt-in live Ollama smoke test...\n";

    const char* envRun = std::getenv("FLEX_RUN_OLLAMA_TESTS");
    if (!envRun || std::string_view(envRun) != "1") {
        std::cout << "  (Skipped: Live Ollama tests are disabled by default. "
                  << "Set FLEX_RUN_OLLAMA_TESTS=1 to run live tests)\n";
        return;
    }

    flex::OllamaClient client;
    bool healthy = client.checkHealthSync(2.0);
    std::cout << "  Live Ollama health check: " << (healthy ? "ONLINE" : "OFFLINE") << "\n";
    if (healthy) {
        auto models = client.listModelsSync(3.0);
        std::cout << "  Live models available: " << models.size() << "\n";
    }
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "========================================================\n";
    std::cout << " FLEX 1.0 Native C++ Ollama Client Test Suite (Stage 3)\n";
    std::cout << "========================================================\n\n";

    MockOllamaServer mock;

    test_valid_successful_response(mock);
    test_multiple_messages_with_roles(mock);
    test_unicode_and_multiline(mock);
    test_empty_and_invalid_requests(mock);
    test_malformed_json_response(mock);
    test_missing_or_unexpected_fields(mock);
    test_http_server_error(mock);
    test_model_not_found(mock);
    test_connection_refusal();
    test_timeout_behavior(mock);
    test_streaming_responses(mock);
    test_streaming_malformed_chunk(mock);
    test_request_cancellation(mock);
    test_safe_cleanup(mock);
    test_health_and_tags_helpers(mock);
    test_opt_in_live_smoke();

    std::cout << "\n========================================================\n";
    std::cout << "Test Summary:\n";
    std::cout << "  Total Assertions : " << g_tests_run << "\n";
    std::cout << "  Passed           : " << g_tests_passed << "\n";
    std::cout << "  Failed           : " << g_tests_failed << "\n";
    std::cout << "========================================================\n";

    if (g_tests_failed == 0) {
        std::cout << "Result: SUCCESS (All native C++ Ollama client tests passed!)\n";
        return 0;
    }

    std::cout << "Result: FAILURE (" << g_tests_failed << " assertion(s) failed!)\n";
    return 1;
}

#include "test_ollama_client.moc"

