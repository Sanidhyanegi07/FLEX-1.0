#include "flex/backend.hpp"
#include "request_validator.h"
#include "error_classifier.h"

#include <QCoreApplication>
#include <QEventLoop>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace flex {

const char* backendErrorCategoryToString(BackendErrorCategory category) noexcept
{
    switch (category) {
    case BackendErrorCategory::None:
        return "None";
    case BackendErrorCategory::InvalidInput:
        return "InvalidInput";
    case BackendErrorCategory::CoreRejected:
        return "CoreRejected";
    case BackendErrorCategory::StorageError:
        return "StorageError";
    case BackendErrorCategory::AiNetworkError:
        return "AiNetworkError";
    case BackendErrorCategory::AiTimeout:
        return "AiTimeout";
    case BackendErrorCategory::AiHttpError:
        return "AiHttpError";
    case BackendErrorCategory::AiMalformedResponse:
        return "AiMalformedResponse";
    case BackendErrorCategory::Cancelled:
        return "Cancelled";
    default:
        return "UnknownError";
    }
}

namespace {

BackendErrorCategory mapOllamaError(OllamaErrorCode code) noexcept
{
    switch (code) {
    case OllamaErrorCode::InvalidInput:
        return BackendErrorCategory::InvalidInput;
    case OllamaErrorCode::NetworkError:
        return BackendErrorCategory::AiNetworkError;
    case OllamaErrorCode::Timeout:
        return BackendErrorCategory::AiTimeout;
    case OllamaErrorCode::HttpError:
    case OllamaErrorCode::ModelNotFound:
        return BackendErrorCategory::AiHttpError;
    case OllamaErrorCode::MalformedJson:
    case OllamaErrorCode::UnexpectedPayload:
        return BackendErrorCategory::AiMalformedResponse;
    case OllamaErrorCode::Cancelled:
        return BackendErrorCategory::Cancelled;
    default:
        return BackendErrorCategory::AiNetworkError;
    }
}

std::string trimString(std::string_view str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(first, (last - first + 1)));
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// BackendCoordinatorImpl
// -----------------------------------------------------------------------------

struct ActiveChat {
    uint64_t id{0};
    uint64_t ollamaReqId{0};
    uint64_t coreOpId{0};
    bool coreStarted{false};

    std::string conversationId;
    std::string userMessageId;
    std::string userPrompt;
    std::string model;

    FlexCommandStatus parserStatus{FLEX_STATUS_OK};
    FlexStateStatus stateStatus{FLEX_STATE_OK};

    std::function<void(const ChatResult&)> onComplete;
    std::function<void(const BackendError&)> onError;
    std::function<void(const std::string&)> onToken;
};

class BackendCoordinatorImpl {
public:
    BackendCoordinatorImpl(
        BackendCoordinator* q,
        std::shared_ptr<ConversationStorage> storage,
        std::shared_ptr<OllamaClient> ollamaClient)
        : m_q(q),
          m_storage(std::move(storage)),
          m_ollama(std::move(ollamaClient))
    {
    }

    ~BackendCoordinatorImpl()
    {
        *m_alive = false;
        for (auto& [id, active] : m_activeChats) {
            if (active && active->coreStarted) {
                m_core.finishOperation();
                active->coreStarted = false;
            }
        }
        cancelAll();
        m_activeChats.clear();
    }

    [[nodiscard]] CoreSession& coreSession() noexcept
    {
        return m_core;
    }

    [[nodiscard]] const CoreSession& coreSession() const noexcept
    {
        return m_core;
    }

    [[nodiscard]] std::shared_ptr<ConversationStorage> storage() const noexcept
    {
        return m_storage;
    }

    [[nodiscard]] std::shared_ptr<OllamaClient> ollamaClient() const noexcept
    {
        return m_ollama;
    }

    uint64_t sendChat(
        const ChatRequest& request,
        std::function<void(const ChatResult&)> onComplete,
        std::function<void(const BackendError&)> onError,
        std::function<void(const std::string&)> onToken)
    {
        std::string cleanMsg = trimString(request.message);
        flex_prompt_validation_result_t val_res;
        flex_req_status_t vstatus = flex_validate_chat_prompt(cleanMsg.c_str(), 1024, &val_res);
        if (cleanMsg.empty() || vstatus == FLEX_REQ_ERR_NULL_PTR || vstatus == FLEX_REQ_ERR_EMPTY) {
            BackendError err{
                BackendErrorCategory::InvalidInput, 0,
                "User message cannot be empty or whitespace-only."
            };
            reportImmediateError(0, err, onError, onComplete);
            return 0;
        }

        if (!m_storage) {
            BackendError err{
                BackendErrorCategory::StorageError, 0,
                "Persistent storage subsystem is not configured or unavailable."
            };
            reportImmediateError(0, err, onError, onComplete);
            return 0;
        }

        // 1. Validate target conversation ID if explicitly provided
        std::string targetCid;
        bool isNewConversation = false;
        if (request.conversationId.has_value() && !request.conversationId.value().empty()) {
            targetCid = request.conversationId.value();
            try {
                auto existing = m_storage->getConversation(targetCid);
                if (!existing.has_value()) {
                    BackendError err{
                        BackendErrorCategory::InvalidInput, 0,
                        "Conversation ID '" + targetCid + "' not found."
                    };
                    reportImmediateError(0, err, onError, onComplete);
                    return 0;
                }
            } catch (const StorageError& exc) {
                BackendError err{
                    BackendErrorCategory::StorageError, 0,
                    "Failed to inspect conversation in storage: " + std::string(exc.what())
                };
                reportImmediateError(0, err, onError, onComplete);
                return 0;
            }
        }

        // 2. Authorize operation with C Core Lifecycle State Manager
        // Note: Decouple C core command authorization from large AI prompt payload
        // to strictly respect the C core's 1024-byte input size limit.
        // We authorize with C core BEFORE creating any new conversation in storage
        // to prevent leaving orphaned empty conversations if C core rejects (e.g. ALREADY_PROCESSING).
        DispatchResult dispatchRes = m_core.dispatch("chat");
        if (!dispatchRes.isOk() || !dispatchRes.operationStarted()) {
            BackendErrorCategory cat = BackendErrorCategory::CoreRejected;
            std::string msg = "C core rejected dispatch: ";
            if (dispatchRes.stateStatus() == FLEX_STATE_ERROR_ALREADY_PROCESSING) {
                msg += "an operation is already in progress.";
            } else if (dispatchRes.stateStatus() == FLEX_STATE_ERROR_STOPPING) {
                msg += "application lifecycle is stopping.";
            } else {
                msg += CoreSession::stateStatusToString(dispatchRes.stateStatus());
            }
            BackendError err{cat, 0, msg};
            ChatResult res;
            res.success = false;
            res.conversationId = targetCid;
            res.userPrompt = cleanMsg;
            res.parserStatus = dispatchRes.parserStatus();
            res.stateStatus = dispatchRes.stateStatus();
            res.error = err;
            reportImmediateError(0, err, onError, onComplete, &res);
            return 0;
        }

        uint64_t coreOpId = dispatchRes.operationId();

        // 3. Resolve conversation ID (create new conversation if not provided)
        if (targetCid.empty()) {
            try {
                targetCid = m_storage->createConversation("New Chat");
                isNewConversation = true;
            } catch (const StorageError& exc) {
                m_core.finishOperation();
                BackendError err{
                    BackendErrorCategory::StorageError, 0,
                    "Failed to create new conversation in storage: " + std::string(exc.what())
                };
                ChatResult res;
                res.success = false;
                res.userPrompt = cleanMsg;
                res.operationId = coreOpId;
                res.parserStatus = dispatchRes.parserStatus();
                res.stateStatus = dispatchRes.stateStatus();
                res.error = err;
                reportImmediateError(0, err, onError, onComplete, &res);
                return 0;
            }
        }

        // 4. Persist user message to storage
        std::string userMsgId;
        try {
            userMsgId = m_storage->saveMessage(
                targetCid, "user", cleanMsg, std::nullopt, request.userMessageId, true
            );
        } catch (const StorageError& exc) {
            if (isNewConversation) {
                m_storage->deleteConversation(targetCid);
            }
            m_core.finishOperation();
            BackendError err{
                BackendErrorCategory::StorageError, 0,
                "Failed to persist user message: " + std::string(exc.what())
            };
            ChatResult res;
            res.success = false;
            res.conversationId = targetCid;
            res.userPrompt = cleanMsg;
            res.operationId = coreOpId;
            res.parserStatus = dispatchRes.parserStatus();
            res.stateStatus = dispatchRes.stateStatus();
            res.error = err;
            reportImmediateError(0, err, onError, onComplete, &res);
            return 0;
        }

        // 5. Assemble context messages from storage
        int histLimit = request.maxHistoryMessages > 0 ? request.maxHistoryMessages : 20;
        std::vector<ChatMessage> ollamaMessages;
        ollamaMessages.reserve(histLimit + 2);
        std::string sysPrompt = request.systemPrompt.value_or(BackendCoordinator::DEFAULT_SYSTEM_PROMPT);
        if (!sysPrompt.empty()) {
            ollamaMessages.push_back(ChatMessage{"system", sysPrompt});
        }
        try {
            auto historyRecords = m_storage->getMessages(targetCid, histLimit);
            for (auto& rec : historyRecords) {
                ollamaMessages.push_back(ChatMessage{std::move(rec.role), std::move(rec.content)});
            }
        } catch (const StorageError& exc) {
            m_core.finishOperation();
            BackendError err{
                BackendErrorCategory::StorageError, 0,
                "Failed to read conversation history: " + std::string(exc.what())
            };
            ChatResult res;
            res.success = false;
            res.conversationId = targetCid;
            res.userMessageId = userMsgId;
            res.userPrompt = cleanMsg;
            res.operationId = coreOpId;
            res.parserStatus = dispatchRes.parserStatus();
            res.stateStatus = dispatchRes.stateStatus();
            res.error = err;
            reportImmediateError(0, err, onError, onComplete, &res);
            return 0;
        }

        // 5. Setup active request tracking
        uint64_t reqId = m_nextRequestId++;
        auto active = std::make_unique<ActiveChat>();
        active->id = reqId;
        active->coreOpId = coreOpId;
        active->coreStarted = true;
        active->conversationId = targetCid;
        active->userMessageId = userMsgId;
        active->userPrompt = cleanMsg;
        active->model = request.model.value_or(m_ollama ? m_ollama->config().model : "qwen2.5-coder:3b");
        active->parserStatus = dispatchRes.parserStatus();
        active->stateStatus = dispatchRes.stateStatus();
        active->onComplete = std::move(onComplete);
        active->onError = std::move(onError);
        active->onToken = std::move(onToken);

        if (!m_ollama) {
            m_core.finishOperation();
            BackendError err{
                BackendErrorCategory::AiNetworkError, 0,
                "Ollama client subsystem is not configured."
            };
            ChatResult res;
            res.success = false;
            res.conversationId = targetCid;
            res.userMessageId = userMsgId;
            res.userPrompt = cleanMsg;
            res.operationId = coreOpId;
            res.error = err;
            reportImmediateError(0, err, onError, onComplete, &res);
            return 0;
        }

        if (request.model.has_value() && !request.model.value().empty()) {
            m_ollama->setModel(request.model.value());
        }

        // 6. Dispatch to Ollama Client
        auto onOllamaToken = [this, reqId, alive = m_alive](const std::string& tokenChunk) {
            if (!*alive) {
                return;
            }
            auto it = m_activeChats.find(reqId);
            if (it != m_activeChats.end() && it->second->onToken) {
                it->second->onToken(tokenChunk);
            }
            emit m_q->chatToken(reqId, QString::fromStdString(tokenChunk));
        };

        auto onOllamaComplete = [this, reqId, alive = m_alive](const ChatResponse& resp) {
            if (!*alive) {
                return;
            }
            handleOllamaComplete(reqId, resp);
        };

        auto onOllamaError = [this, reqId, alive = m_alive](const OllamaError& err) {
            if (!*alive) {
                return;
            }
            handleOllamaError(reqId, err);
        };

        // Register active request BEFORE dispatching to OllamaClient
        // so that synchronous completion or immediate error inside chatAsync
        // is cleanly captured and processed without orphan state.
        m_activeChats[reqId] = std::move(active);

        uint64_t ollamaReqId = 0;
        if (request.streaming) {
            ollamaReqId = m_ollama->chatStreamAsync(
                ollamaMessages,
                std::move(onOllamaToken),
                std::move(onOllamaComplete),
                std::move(onOllamaError),
                request.timeoutSeconds
            );
        } else {
            ollamaReqId = m_ollama->chatAsync(
                ollamaMessages,
                std::move(onOllamaComplete),
                std::move(onOllamaError),
                request.timeoutSeconds
            );
        }

        // Check if request is still active (not settled synchronously)
        auto it = m_activeChats.find(reqId);
        if (it != m_activeChats.end()) {
            it->second->ollamaReqId = ollamaReqId;
        }

        return reqId;
    }

    bool cancelRequest(uint64_t requestId)
    {
        auto it = m_activeChats.find(requestId);
        if (it == m_activeChats.end()) {
            return false;
        }

        uint64_t ollamaReqId = it->second->ollamaReqId;
        if (m_ollama && ollamaReqId > 0) {
            return m_ollama->cancelRequest(ollamaReqId);
        }
        return false;
    }

    void cancelAll()
    {
        std::vector<uint64_t> ids;
        ids.reserve(m_activeChats.size());
        for (const auto& [id, _] : m_activeChats) {
            ids.push_back(id);
        }
        for (uint64_t id : ids) {
            cancelRequest(id);
        }
    }

private:
    void reportImmediateError(
        uint64_t reqId,
        const BackendError& err,
        const std::function<void(const BackendError&)>& onError,
        const std::function<void(const ChatResult&)>& onComplete,
        const ChatResult* partialResult = nullptr)
    {
        ChatResult res;
        if (partialResult) {
            res = *partialResult;
        }
        res.success = false;
        res.error = err;

        if (onError) {
            onError(err);
        }
        if (onComplete) {
            onComplete(res);
        }
        emit m_q->chatError(reqId, err);
        emit m_q->chatFinished(reqId, res);
    }

    void handleOllamaComplete(uint64_t reqId, const ChatResponse& resp)
    {
        auto it = m_activeChats.find(reqId);
        if (it == m_activeChats.end()) {
            return;
        }

        std::unique_ptr<ActiveChat> active = std::move(it->second);
        m_activeChats.erase(it);

        // Guaranteed cleanup of C core operation
        if (active->coreStarted) {
            m_core.finishOperation();
            active->coreStarted = false;
        }

        // Persist assistant message in storage
        std::string asstMsgId;
        bool storageOk = true;
        std::string storageErrMsg;
        try {
            asstMsgId = m_storage->saveMessage(
                active->conversationId, "assistant", resp.text, resp.model, std::nullopt, false
            );
        } catch (const StorageError& exc) {
            storageOk = false;
            storageErrMsg = exc.what();
        }

        ChatResult result;
        result.conversationId = active->conversationId;
        result.userMessageId = active->userMessageId;
        result.userPrompt = active->userPrompt;
        result.responseText = resp.text;
        result.model = resp.model;
        result.operationId = active->coreOpId;
        result.parserStatus = active->parserStatus;
        result.stateStatus = active->stateStatus;
        result.totalDurationNs = resp.totalDurationNs;
        result.evalCount = resp.evalCount;

        if (storageOk) {
            result.success = true;
            result.assistantMessageId = asstMsgId;
            if (active->onComplete) {
                active->onComplete(result);
            }
            emit m_q->chatFinished(reqId, result);
        } else {
            result.success = false;
            BackendError bErr{
                BackendErrorCategory::StorageError, 0,
                "Failed to persist assistant response: " + storageErrMsg
            };
            result.error = bErr;
            if (active->onError) {
                active->onError(bErr);
            }
            if (active->onComplete) {
                active->onComplete(result);
            }
            emit m_q->chatError(reqId, bErr);
            emit m_q->chatFinished(reqId, result);
        }
    }

    void handleOllamaError(uint64_t reqId, const OllamaError& err)
    {
        auto it = m_activeChats.find(reqId);
        if (it == m_activeChats.end()) {
            return;
        }

        std::unique_ptr<ActiveChat> active = std::move(it->second);
        m_activeChats.erase(it);

        // Guaranteed cleanup of C core operation
        if (active->coreStarted) {
            m_core.finishOperation();
            active->coreStarted = false;
        }

        BackendError bErr{
            mapOllamaError(err.code),
            err.httpStatus,
            err.message
        };

        ChatResult result;
        result.success = false;
        result.conversationId = active->conversationId;
        result.userMessageId = active->userMessageId;
        result.userPrompt = active->userPrompt;
        result.model = active->model;
        result.operationId = active->coreOpId;
        result.parserStatus = active->parserStatus;
        result.stateStatus = active->stateStatus;
        result.error = bErr;

        if (active->onError) {
            active->onError(bErr);
        }
        if (active->onComplete) {
            active->onComplete(result);
        }
        emit m_q->chatError(reqId, bErr);
        emit m_q->chatFinished(reqId, result);
    }

    BackendCoordinator* m_q{nullptr};
    CoreSession m_core;
    std::shared_ptr<ConversationStorage> m_storage;
    std::shared_ptr<OllamaClient> m_ollama;

    std::atomic<uint64_t> m_nextRequestId{1};
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unordered_map<uint64_t, std::unique_ptr<ActiveChat>> m_activeChats;
};

// -----------------------------------------------------------------------------
// BackendCoordinator Public Methods
// -----------------------------------------------------------------------------

BackendCoordinator::BackendCoordinator(
    const std::optional<std::string>& dbPath,
    const OllamaConfig& config,
    QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<BackendCoordinatorImpl>(
          this,
          std::make_shared<ConversationStorage>(dbPath),
          std::make_shared<OllamaClient>(config, nullptr)))
{
}

BackendCoordinator::BackendCoordinator(
    std::shared_ptr<ConversationStorage> storage,
    std::shared_ptr<OllamaClient> ollamaClient,
    QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<BackendCoordinatorImpl>(
          this,
          std::move(storage),
          std::move(ollamaClient)))
{
}

BackendCoordinator::~BackendCoordinator() = default;

uint64_t BackendCoordinator::sendChatAsync(
    const ChatRequest& request,
    std::function<void(const ChatResult&)> onComplete,
    std::function<void(const BackendError&)> onError,
    std::function<void(const std::string& tokenChunk)> onToken)
{
    return m_impl->sendChat(request, std::move(onComplete), std::move(onError), std::move(onToken));
}

bool BackendCoordinator::cancelRequest(uint64_t requestId)
{
    return m_impl->cancelRequest(requestId);
}

void BackendCoordinator::cancelAll()
{
    m_impl->cancelAll();
}

ChatResult BackendCoordinator::sendChatSync(
    const ChatRequest& request,
    BackendError* outError)
{
    ChatResult result;
    BackendError localErr;
    bool finished = false;
    QEventLoop loop;

    uint64_t reqId = sendChatAsync(
        request,
        [&](const ChatResult& res) {
            result = res;
            finished = true;
            loop.quit();
        },
        [&](const BackendError& err) {
            localErr = err;
            result.success = false;
            result.error = err;
            finished = true;
            loop.quit();
        }
    );

    if (!finished && reqId > 0) {
        loop.exec();
    }

    if (outError) {
        *outError = localErr.hasError() ? localErr : result.error;
    }
    return result;
}

std::string BackendCoordinator::createConversation(const std::string& title)
{
    if (m_impl->storage()) {
        return m_impl->storage()->createConversation(title);
    }
    throw StorageError("Storage subsystem unavailable");
}

std::vector<ConversationRecord> BackendCoordinator::listConversations(int limit)
{
    if (m_impl->storage()) {
        return m_impl->storage()->listConversations(limit);
    }
    return {};
}

std::optional<ConversationRecord> BackendCoordinator::getConversation(const std::string& conversationId)
{
    if (m_impl->storage()) {
        return m_impl->storage()->getConversation(conversationId);
    }
    return std::nullopt;
}

void BackendCoordinator::updateConversationTitle(const std::string& conversationId, const std::string& title)
{
    if (m_impl->storage()) {
        m_impl->storage()->updateConversationTitle(conversationId, title);
    }
}

bool BackendCoordinator::deleteConversation(const std::string& conversationId)
{
    if (m_impl->storage()) {
        return m_impl->storage()->deleteConversation(conversationId);
    }
    return false;
}

std::vector<MessageRecord> BackendCoordinator::getConversationMessages(
    const std::string& conversationId,
    const std::optional<int>& limit)
{
    if (m_impl->storage()) {
        return m_impl->storage()->getMessages(conversationId, limit);
    }
    return {};
}

int64_t BackendCoordinator::getMessageCount(const std::string& conversationId)
{
    if (m_impl->storage()) {
        return m_impl->storage()->getMessageCount(conversationId);
    }
    return 0;
}

bool BackendCoordinator::checkHealthSync(double timeoutSeconds)
{
    if (m_impl->ollamaClient()) {
        return m_impl->ollamaClient()->checkHealthSync(timeoutSeconds);
    }
    return false;
}

std::vector<std::string> BackendCoordinator::listModelsSync(double timeoutSeconds)
{
    if (m_impl->ollamaClient()) {
        return m_impl->ollamaClient()->listModelsSync(timeoutSeconds);
    }
    return {};
}

bool BackendCoordinator::isModelAvailableSync(const std::string& modelName, double timeoutSeconds)
{
    if (m_impl->ollamaClient()) {
        return m_impl->ollamaClient()->isModelAvailableSync(modelName, timeoutSeconds);
    }
    return false;
}

CoreSession& BackendCoordinator::coreSession()
{
    return m_impl->coreSession();
}

const CoreSession& BackendCoordinator::coreSession() const
{
    return m_impl->coreSession();
}

std::shared_ptr<ConversationStorage> BackendCoordinator::storage() const
{
    return m_impl->storage();
}

std::shared_ptr<OllamaClient> BackendCoordinator::ollamaClient() const
{
    return m_impl->ollamaClient();
}

} // namespace flex

#include "moc_backend.cpp"

