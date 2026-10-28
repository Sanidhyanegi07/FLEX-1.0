#include "flex/ollama_client.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace flex {

// -----------------------------------------------------------------------------
// OllamaConfig Implementation
// -----------------------------------------------------------------------------

OllamaConfig OllamaConfig::fromEnvironment()
{
    OllamaConfig cfg;

    const char* envUrl = std::getenv("FLEX_OLLAMA_BASE_URL");
    if (!envUrl) {
        envUrl = std::getenv("FLEX_OLLAMA_HOST");
    }
    if (!envUrl) {
        envUrl = std::getenv("FLEX_OLLAMA_URL");
    }
    if (envUrl && std::string_view(envUrl).length() > 0) {
        cfg.baseUrl = envUrl;
    }

    const char* envModel = std::getenv("FLEX_OLLAMA_MODEL");
    if (envModel && std::string_view(envModel).length() > 0) {
        cfg.model = envModel;
    }

    const char* envTimeout = std::getenv("FLEX_OLLAMA_TIMEOUT");
    if (envTimeout) {
        try {
            double t = std::stod(envTimeout);
            if (t > 0.0) {
                cfg.timeoutSeconds = t;
            }
        } catch (...) {
        }
    }

    const char* envTemp = std::getenv("FLEX_OLLAMA_TEMPERATURE");
    if (envTemp) {
        try {
            double temp = std::stod(envTemp);
            if (temp >= 0.0 && temp <= 2.0) {
                cfg.options.temperature = temp;
            }
        } catch (...) {
        }
    }

    const char* envCtx = std::getenv("FLEX_OLLAMA_NUM_CTX");
    if (!envCtx) {
        envCtx = std::getenv("FLEX_OLLAMA_CONTEXT_WINDOW");
    }
    if (envCtx) {
        try {
            int ctx = std::stoi(envCtx);
            if (ctx >= 512 && ctx <= 131072) {
                cfg.options.numCtx = ctx;
            }
        } catch (...) {
        }
    }

    return cfg;
}

// -----------------------------------------------------------------------------
// OllamaClientImpl
// -----------------------------------------------------------------------------

struct ActiveRequest {
    uint64_t id{0};
    QNetworkReply* reply{nullptr};
    QTimer* timer{nullptr};
    bool isStreaming{false};
    bool isUserCancelled{false};
    bool isTimedOut{false};

    std::function<void(const ChatResponse&)> onComplete;
    std::function<void(const OllamaError&)> onError;
    std::function<void(const std::string&)> onToken;

    QByteArray streamBuffer;
    ChatResponse accumulatedResponse;
};

class OllamaClientImpl {
public:
    explicit OllamaClientImpl(OllamaClient* q, const OllamaConfig& config)
        : m_q(q), m_config(config), m_nam(new QNetworkAccessManager(q))
    {
    }

    ~OllamaClientImpl()
    {
        cancelAll();
    }

    [[nodiscard]] const OllamaConfig& config() const noexcept
    {
        return m_config;
    }

    void setConfig(const OllamaConfig& config)
    {
        m_config = config;
    }

    uint64_t chat(
        const std::vector<ChatMessage>& messages,
        bool isStreaming,
        std::function<void(const std::string&)> onToken,
        std::function<void(const ChatResponse&)> onComplete,
        std::function<void(const OllamaError&)> onError,
        const std::optional<double>& timeoutOverride)
    {
        // 1. Validate inputs
        if (messages.empty()) {
            reportErrorImmediately(
                OllamaErrorCode::InvalidInput, 0,
                "ChatMessage list must not be empty.", onError);
            return 0;
        }

        for (size_t i = 0; i < messages.size(); ++i) {
            const auto& msg = messages[i];
            if (!isValidRole(msg.role)) {
                reportErrorImmediately(
                    OllamaErrorCode::InvalidInput, 0,
                    "Invalid message role '" + msg.role + "' at index " + std::to_string(i),
                    onError);
                return 0;
            }
        }

        if (m_config.model.empty()) {
            reportErrorImmediately(
                OllamaErrorCode::InvalidInput, 0,
                "Model name must not be empty.", onError);
            return 0;
        }

        // 2. Build JSON payload
        QJsonObject root;
        root["model"] = QString::fromStdString(m_config.model);
        root["stream"] = isStreaming;

        QJsonArray msgsArr;
        for (const auto& msg : messages) {
            QJsonObject mObj;
            mObj["role"] = QString::fromStdString(msg.role);
            mObj["content"] = QString::fromStdString(msg.content);
            msgsArr.append(mObj);
        }
        root["messages"] = msgsArr;

        QJsonObject optionsObj;
        optionsObj["temperature"] = m_config.options.temperature;
        if (m_config.options.numCtx.has_value()) {
            optionsObj["num_ctx"] = m_config.options.numCtx.value();
        }
        root["options"] = optionsObj;

        QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);

        // 3. Format URL and Request
        QString base = QString::fromStdString(m_config.baseUrl).trimmed();
        while (base.endsWith('/')) {
            base.chop(1);
        }
        QUrl url(base + "/api/chat");

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json");

        // 4. Dispatch Request
        uint64_t reqId = m_nextRequestId++;
        QNetworkReply* reply = m_nam->post(req, body);

        auto activeReq = std::make_unique<ActiveRequest>();
        activeReq->id = reqId;
        activeReq->reply = reply;
        activeReq->isStreaming = isStreaming;
        activeReq->onComplete = std::move(onComplete);
        activeReq->onError = std::move(onError);
        activeReq->onToken = std::move(onToken);
        activeReq->accumulatedResponse.model = m_config.model;

        // Setup Timeout Timer
        double effectiveTimeout = timeoutOverride.value_or(m_config.timeoutSeconds);
        if (effectiveTimeout <= 0.0) {
            effectiveTimeout = 30.0;
        }

        QTimer* timer = new QTimer(reply);
        timer->setSingleShot(true);
        timer->setInterval(static_cast<int>(effectiveTimeout * 1000.0));
        activeReq->timer = timer;

        ActiveRequest* activeReqPtr = activeReq.get();
        (void)activeReqPtr;
        m_activeRequests[reqId] = std::move(activeReq);

        QObject::connect(timer, &QTimer::timeout, m_q, [this, reqId]() {
            handleTimeout(reqId);
        });
        timer->start();

        // Connect Network Signals
        if (isStreaming) {
            QObject::connect(reply, &QNetworkReply::readyRead, m_q, [this, reqId]() {
                handleReadyReadStream(reqId);
            });
        }

        QObject::connect(reply, &QNetworkReply::finished, m_q, [this, reqId]() {
            handleFinished(reqId);
        });

        return reqId;
    }

    bool cancelRequest(uint64_t requestId)
    {
        auto it = m_activeRequests.find(requestId);
        if (it == m_activeRequests.end()) {
            return false;
        }

        ActiveRequest* req = it->second.get();
        req->isUserCancelled = true;
        if (req->timer) {
            req->timer->stop();
        }
        if (req->reply) {
            req->reply->abort();
        }

        OllamaError err{OllamaErrorCode::Cancelled, 0, "Request was cancelled by caller."};
        if (req->onError) {
            req->onError(err);
        }
        emit m_q->requestError(requestId, err);

        if (req->reply) {
            req->reply->deleteLater();
        }
        m_activeRequests.erase(it);
        return true;
    }

    void cancelAll()
    {
        std::vector<uint64_t> ids;
        ids.reserve(m_activeRequests.size());
        for (const auto& [id, _] : m_activeRequests) {
            ids.push_back(id);
        }
        for (uint64_t id : ids) {
            cancelRequest(id);
        }
    }

private:
    void reportErrorImmediately(
        OllamaErrorCode code, int status, const std::string& msg,
        const std::function<void(const OllamaError&)>& onError)
    {
        OllamaError err{code, status, msg};
        if (onError) {
            onError(err);
        }
        emit m_q->requestError(0, err);
    }

    void handleTimeout(uint64_t reqId)
    {
        auto it = m_activeRequests.find(reqId);
        if (it == m_activeRequests.end()) {
            return;
        }

        ActiveRequest* req = it->second.get();
        req->isTimedOut = true;
        if (req->reply) {
            req->reply->abort();
        }
    }

    void handleReadyReadStream(uint64_t reqId)
    {
        auto it = m_activeRequests.find(reqId);
        if (it == m_activeRequests.end()) {
            return;
        }

        ActiveRequest* req = it->second.get();
        if (!req->reply) {
            return;
        }

        req->streamBuffer.append(req->reply->readAll());

        while (true) {
            int newlineIndex = req->streamBuffer.indexOf('\n');
            if (newlineIndex < 0) {
                break;
            }

            QByteArray line = req->streamBuffer.left(newlineIndex).trimmed();
            req->streamBuffer.remove(0, newlineIndex + 1);

            if (line.isEmpty()) {
                continue;
            }

            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(line, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                // Malformed JSON encountered mid-stream
                OllamaError err{
                    OllamaErrorCode::MalformedJson, 200,
                    "Failed to parse streaming JSON chunk: " + parseErr.errorString().toStdString()
                };
                if (req->onError) {
                    req->onError(err);
                }
                emit m_q->requestError(reqId, err);
                if (req->timer) req->timer->stop();
                req->reply->abort();
                req->reply->deleteLater();
                m_activeRequests.erase(it);
                return;
            }

            QJsonObject obj = doc.object();

            // Extract message chunk
            if (obj.contains("message") && obj["message"].isObject()) {
                QJsonObject msgObj = obj["message"].toObject();
                QString content = msgObj.value("content").toString();
                if (!content.isEmpty()) {
                    req->accumulatedResponse.text += content.toStdString();
                    if (req->onToken) {
                        req->onToken(content.toStdString());
                    }
                    emit m_q->streamToken(reqId, content);
                }
            }

            // Extract completion metadata if done == true
            if (obj.value("done").toBool(false)) {
                req->accumulatedResponse.success = true;
                req->accumulatedResponse.model = obj.value("model").toString(QString::fromStdString(m_config.model)).toStdString();
                req->accumulatedResponse.totalDurationNs = obj.value("total_duration").toVariant().toLongLong();
                req->accumulatedResponse.loadDurationNs = obj.value("load_duration").toVariant().toLongLong();
                req->accumulatedResponse.promptEvalCount = obj.value("prompt_eval_count").toVariant().toLongLong();
                req->accumulatedResponse.evalCount = obj.value("eval_count").toVariant().toLongLong();
                req->accumulatedResponse.evalDurationNs = obj.value("eval_duration").toVariant().toLongLong();
                req->accumulatedResponse.doneReason = obj.value("done_reason").toString().toStdString();
            }
        }
    }

    void handleFinished(uint64_t reqId)
    {
        auto it = m_activeRequests.find(reqId);
        if (it == m_activeRequests.end()) {
            return;
        }

        std::unique_ptr<ActiveRequest> req = std::move(it->second);
        m_activeRequests.erase(it);

        if (req->timer) {
            req->timer->stop();
        }

        QNetworkReply* reply = req->reply;
        if (!reply) {
            return;
        }

        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 1. Check for cancellation / timeout
        if (req->isUserCancelled) {
            // Already reported during cancelRequest
            reply->deleteLater();
            return;
        }

        if (req->isTimedOut) {
            OllamaError err{
                OllamaErrorCode::Timeout, httpStatus,
                "Inference request timed out waiting for Ollama response."
            };
            if (req->onError) req->onError(err);
            emit m_q->requestError(reqId, err);
            reply->deleteLater();
            return;
        }

        // 2. Check for network or HTTP level errors
        if (reply->error() != QNetworkReply::NoError) {
            QByteArray errBody = reply->readAll();
            std::string errDetail = errBody.isEmpty()
                ? reply->errorString().toStdString()
                : QString::fromUtf8(errBody).trimmed().toStdString();

            OllamaErrorCode errCode = OllamaErrorCode::NetworkError;
            if (httpStatus == 404) {
                errCode = OllamaErrorCode::ModelNotFound;
                errDetail = "Model '" + m_config.model + "' not found on host (HTTP 404): " + errDetail;
            } else if (httpStatus >= 400) {
                errCode = OllamaErrorCode::HttpError;
            } else if (reply->error() == QNetworkReply::ConnectionRefusedError) {
                errCode = OllamaErrorCode::NetworkError;
                errDetail = "Ollama daemon unreachable (connection refused): " + errDetail;
            } else if (reply->error() == QNetworkReply::TimeoutError) {
                errCode = OllamaErrorCode::Timeout;
            }

            OllamaError err{errCode, httpStatus, errDetail};
            if (req->onError) req->onError(err);
            emit m_q->requestError(reqId, err);
            reply->deleteLater();
            return;
        }

        // 3. Process Success Payload
        if (req->isStreaming) {
            // Streaming finished successfully
            req->accumulatedResponse.statusCode = httpStatus;
            req->accumulatedResponse.success = true;
            if (req->onComplete) {
                req->onComplete(req->accumulatedResponse);
            }
            emit m_q->requestFinished(reqId, req->accumulatedResponse);
        } else {
            // Non-streaming JSON response parsing
            QByteArray rawBytes = reply->readAll();
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(rawBytes, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                OllamaError err{
                    OllamaErrorCode::MalformedJson, httpStatus,
                    "Failed to parse JSON response: " + parseErr.errorString().toStdString()
                };
                if (req->onError) req->onError(err);
                emit m_q->requestError(reqId, err);
                reply->deleteLater();
                return;
            }

            QJsonObject rootObj = doc.object();
            if (!rootObj.contains("message") || !rootObj["message"].isObject()) {
                OllamaError err{
                    OllamaErrorCode::UnexpectedPayload, httpStatus,
                    "Response is missing required 'message' object."
                };
                if (req->onError) req->onError(err);
                emit m_q->requestError(reqId, err);
                reply->deleteLater();
                return;
            }

            QJsonObject msgObj = rootObj["message"].toObject();
            if (!msgObj.contains("content")) {
                OllamaError err{
                    OllamaErrorCode::UnexpectedPayload, httpStatus,
                    "Response message object is missing 'content' field."
                };
                if (req->onError) req->onError(err);
                emit m_q->requestError(reqId, err);
                reply->deleteLater();
                return;
            }

            ChatResponse resp;
            resp.success = true;
            resp.statusCode = httpStatus;
            resp.text = msgObj.value("content").toString().toStdString();
            resp.model = rootObj.value("model").toString(QString::fromStdString(m_config.model)).toStdString();
            resp.totalDurationNs = rootObj.value("total_duration").toVariant().toLongLong();
            resp.loadDurationNs = rootObj.value("load_duration").toVariant().toLongLong();
            resp.promptEvalCount = rootObj.value("prompt_eval_count").toVariant().toLongLong();
            resp.evalCount = rootObj.value("eval_count").toVariant().toLongLong();
            resp.evalDurationNs = rootObj.value("eval_duration").toVariant().toLongLong();
            resp.doneReason = rootObj.value("done_reason").toString().toStdString();
            resp.rawJson = QString::fromUtf8(rawBytes).toStdString();

            if (req->onComplete) {
                req->onComplete(resp);
            }
            emit m_q->requestFinished(reqId, resp);
        }

        reply->deleteLater();
    }

    OllamaClient* m_q{nullptr};
    OllamaConfig m_config;
    QNetworkAccessManager* m_nam{nullptr};
    uint64_t m_nextRequestId{1};
    std::unordered_map<uint64_t, std::unique_ptr<ActiveRequest>> m_activeRequests;
};

// -----------------------------------------------------------------------------
// OllamaClient Public Methods
// -----------------------------------------------------------------------------

OllamaClient::OllamaClient(const OllamaConfig& config, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<OllamaClientImpl>(this, config))
{
}

OllamaClient::~OllamaClient() = default;

const OllamaConfig& OllamaClient::config() const
{
    return m_impl->config();
}

void OllamaClient::setConfig(const OllamaConfig& config)
{
    m_impl->setConfig(config);
}

void OllamaClient::setBaseUrl(const std::string& url)
{
    auto cfg = m_impl->config();
    cfg.baseUrl = url;
    m_impl->setConfig(cfg);
}

void OllamaClient::setModel(const std::string& model)
{
    auto cfg = m_impl->config();
    cfg.model = model;
    m_impl->setConfig(cfg);
}

void OllamaClient::setTimeoutSeconds(double seconds)
{
    auto cfg = m_impl->config();
    cfg.timeoutSeconds = seconds;
    m_impl->setConfig(cfg);
}

uint64_t OllamaClient::chatAsync(
    const std::vector<ChatMessage>& messages,
    std::function<void(const ChatResponse&)> onComplete,
    std::function<void(const OllamaError&)> onError,
    const std::optional<double>& timeoutOverride)
{
    return m_impl->chat(messages, false, nullptr, std::move(onComplete), std::move(onError), timeoutOverride);
}

uint64_t OllamaClient::chatStreamAsync(
    const std::vector<ChatMessage>& messages,
    std::function<void(const std::string& tokenChunk)> onToken,
    std::function<void(const ChatResponse& finalResponse)> onComplete,
    std::function<void(const OllamaError&)> onError,
    const std::optional<double>& timeoutOverride)
{
    return m_impl->chat(messages, true, std::move(onToken), std::move(onComplete), std::move(onError), timeoutOverride);
}

bool OllamaClient::cancelRequest(uint64_t requestId)
{
    return m_impl->cancelRequest(requestId);
}

void OllamaClient::cancelAll()
{
    m_impl->cancelAll();
}

ChatResponse OllamaClient::chatSync(
    const std::vector<ChatMessage>& messages,
    const std::optional<double>& timeoutOverride,
    OllamaError* outError)
{
    ChatResponse result;
    OllamaError localErr;
    QEventLoop loop;

    chatAsync(
        messages,
        [&](const ChatResponse& resp) {
            result = resp;
            loop.quit();
        },
        [&](const OllamaError& err) {
            localErr = err;
            result.errorMessage = err.message;
            result.statusCode = err.httpStatus;
            loop.quit();
        },
        timeoutOverride
    );

    loop.exec();

    if (outError) {
        *outError = localErr;
    }
    return result;
}

ChatResponse OllamaClient::chatStreamSync(
    const std::vector<ChatMessage>& messages,
    std::function<void(const std::string& tokenChunk)> onToken,
    const std::optional<double>& timeoutOverride,
    OllamaError* outError)
{
    ChatResponse result;
    OllamaError localErr;
    QEventLoop loop;

    chatStreamAsync(
        messages,
        std::move(onToken),
        [&](const ChatResponse& resp) {
            result = resp;
            loop.quit();
        },
        [&](const OllamaError& err) {
            localErr = err;
            result.errorMessage = err.message;
            result.statusCode = err.httpStatus;
            loop.quit();
        },
        timeoutOverride
    );

    loop.exec();

    if (outError) {
        *outError = localErr;
    }
    return result;
}

bool OllamaClient::checkHealthSync(double timeoutSeconds)
{
    QString base = QString::fromStdString(m_impl->config().baseUrl).trimmed();
    while (base.endsWith('/')) {
        base.chop(1);
    }
    QUrl url(base + "/api/version");

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = nam.get(req);
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(static_cast<int>(timeoutSeconds * 1000.0));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start();

    loop.exec();

    bool healthy = false;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject() && doc.object().contains("version")) {
            healthy = true;
        }
    }
    reply->deleteLater();
    return healthy;
}

std::vector<std::string> OllamaClient::listModelsSync(double timeoutSeconds)
{
    QString base = QString::fromStdString(m_impl->config().baseUrl).trimmed();
    while (base.endsWith('/')) {
        base.chop(1);
    }
    QUrl url(base + "/api/tags");

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = nam.get(req);
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(static_cast<int>(timeoutSeconds * 1000.0));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start();

    loop.exec();

    std::vector<std::string> models;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject()) {
            QJsonArray arr = doc.object().value("models").toArray();
            for (const auto& item : arr) {
                if (item.isObject()) {
                    QString name = item.toObject().value("name").toString();
                    if (!name.isEmpty()) {
                        models.push_back(name.toStdString());
                    }
                }
            }
        }
    }
    reply->deleteLater();
    std::sort(models.begin(), models.end());
    return models;
}

bool OllamaClient::isModelAvailableSync(const std::string& modelName, double timeoutSeconds)
{
    if (modelName.empty()) {
        return false;
    }
    auto models = listModelsSync(timeoutSeconds);
    std::string target = modelName;
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    for (const auto& m : models) {
        std::string cur = m;
        std::transform(cur.begin(), cur.end(), cur.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (cur == target) {
            return true;
        }
        if (target.find(':') == std::string::npos && cur == target + ":latest") {
            return true;
        }
    }
    return false;
}

} // namespace flex

#include "moc_ollama_client.cpp"
