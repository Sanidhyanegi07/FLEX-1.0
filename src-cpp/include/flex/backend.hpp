#ifndef FLEX_BACKEND_HPP
#define FLEX_BACKEND_HPP

#include "flex/core_session.hpp"
#include "flex/ollama_client.hpp"
#include "flex/storage.hpp"

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flex {

/**
 * High-level error classification for backend coordinator operations.
 */
enum class BackendErrorCategory {
    None = 0,
    InvalidInput,        // Empty input, missing or invalid conversation ID
    CoreRejected,        // C core rejected command parsing or lifecycle state transition
    StorageError,        // Database initialization, read, or write failure
    AiNetworkError,      // Ollama daemon unreachable or connection refused
    AiTimeout,           // Inference request timed out
    AiHttpError,         // HTTP 4xx or 5xx status from Ollama
    AiMalformedResponse, // Malformed JSON or missing required fields from AI response
    Cancelled            // Operation cancelled by user or caller
};

/**
 * Detailed error reported by backend operations.
 */
struct BackendError {
    BackendErrorCategory category{BackendErrorCategory::None};
    int statusCode{0};
    std::string message;

    [[nodiscard]] bool hasError() const noexcept {
        return category != BackendErrorCategory::None;
    }
};

/**
 * Chat request parameters submitted by caller or future Qt GUI.
 */
struct ChatRequest {
    std::string message;
    std::optional<std::string> conversationId{std::nullopt};
    std::optional<std::string> model{std::nullopt};
    std::optional<std::string> userMessageId{std::nullopt};
    std::optional<std::string> systemPrompt{std::nullopt};
    std::optional<double> timeoutSeconds{std::nullopt};
    bool streaming{false};
    int maxHistoryMessages{20};
};

/**
 * Structured result of an end-to-end chat turn.
 */
struct ChatResult {
    bool success{false};
    std::string conversationId;
    std::string userMessageId;
    std::string assistantMessageId;
    std::string userPrompt;
    std::string responseText;
    std::string model;

    // C Core lifecycle metadata
    uint64_t operationId{0};
    FlexCommandStatus parserStatus{FLEX_STATUS_OK};
    FlexStateStatus stateStatus{FLEX_STATE_OK};

    // Performance and execution metrics
    int64_t totalDurationNs{0};
    int64_t evalCount{0};

    // Error details (populated when success == false)
    BackendError error;
};

class BackendCoordinatorImpl;

/**
 * Native C++ Backend Coordinator.
 *
 * Integrates:
 * - C11 Core command parser, state manager, and operation lifecycles (CoreSession)
 * - Local AI inference runtime (OllamaClient)
 * - Persistent SQLite conversation storage (ConversationStorage)
 *
 * Design Guarantees:
 * - QObject-derived; integrates seamlessly with Qt event-loop and future Qt GUI.
 * - Non-blocking asynchronous workflow with signals and callbacks.
 * - Provides synchronous helpers for CLI and deterministic testing.
 * - Respects C core 1024-byte input limit by decoupling command authorization
 *   from large AI prompt payloads.
 * - Enforces single completion of C core operations across all paths.
 * - Idempotent user message persistence using optional message IDs.
 * - Persists assistant responses only upon successful validation.
 */
class BackendCoordinator : public QObject {
    Q_OBJECT

public:
    static inline const std::string DEFAULT_SYSTEM_PROMPT =
        "You are FLEX 1.0, an advanced, concise AI desktop assistant for engineering and system control. "
        "Provide clear, accurate, and direct responses.";

    /**
     * Constructs a coordinator with default components or custom database/Ollama paths.
     */
    explicit BackendCoordinator(
        const std::optional<std::string>& dbPath = std::nullopt,
        const OllamaConfig& config = OllamaConfig::fromEnvironment(),
        QObject* parent = nullptr);

    /**
     * Constructs a coordinator with injected storage and Ollama client instances.
     * Ideal for testing with mock servers and in-memory/temp databases.
     */
    BackendCoordinator(
        std::shared_ptr<ConversationStorage> storage,
        std::shared_ptr<OllamaClient> ollamaClient,
        QObject* parent = nullptr);

    ~BackendCoordinator() override;

    // Non-copyable, non-movable QObject
    BackendCoordinator(const BackendCoordinator&) = delete;
    BackendCoordinator& operator=(const BackendCoordinator&) = delete;
    BackendCoordinator(BackendCoordinator&&) = delete;
    BackendCoordinator& operator=(BackendCoordinator&&) = delete;

    // Asynchronous Chat Operations
    uint64_t sendChatAsync(
        const ChatRequest& request,
        std::function<void(const ChatResult&)> onComplete = nullptr,
        std::function<void(const BackendError&)> onError = nullptr,
        std::function<void(const std::string& tokenChunk)> onToken = nullptr);

    bool cancelRequest(uint64_t requestId);
    void cancelAll();

    // Synchronous Chat Helpers (Event-Loop Driven; Safe for Testing & CLI)
    ChatResult sendChatSync(
        const ChatRequest& request,
        BackendError* outError = nullptr);

    // Conversation Management Convenience API
    std::string createConversation(const std::string& title = "New Chat");
    std::vector<ConversationRecord> listConversations(int limit = 30);
    std::optional<ConversationRecord> getConversation(const std::string& conversationId);
    void updateConversationTitle(const std::string& conversationId, const std::string& title);
    bool deleteConversation(const std::string& conversationId);
    std::vector<MessageRecord> getConversationMessages(
        const std::string& conversationId,
        const std::optional<int>& limit = std::nullopt);
    int64_t getMessageCount(const std::string& conversationId);

    // AI & System Diagnostics
    bool checkHealthSync(double timeoutSeconds = 2.0);
    std::vector<std::string> listModelsSync(double timeoutSeconds = 4.0);
    bool isModelAvailableSync(const std::string& modelName, double timeoutSeconds = 3.0);

    // Subsystem Accessors
    [[nodiscard]] CoreSession& coreSession();
    [[nodiscard]] const CoreSession& coreSession() const;
    [[nodiscard]] std::shared_ptr<ConversationStorage> storage() const;
    [[nodiscard]] std::shared_ptr<OllamaClient> ollamaClient() const;

signals:
    void chatFinished(uint64_t requestId, const flex::ChatResult& result);
    void chatError(uint64_t requestId, const flex::BackendError& error);
    void chatToken(uint64_t requestId, const QString& token);

private:
    std::unique_ptr<BackendCoordinatorImpl> m_impl;
};

// Static helper to convert BackendErrorCategory to readable string
const char* backendErrorCategoryToString(BackendErrorCategory category) noexcept;

} // namespace flex

#endif // FLEX_BACKEND_HPP

