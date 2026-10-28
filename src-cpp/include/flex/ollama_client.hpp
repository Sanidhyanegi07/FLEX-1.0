#ifndef FLEX_OLLAMA_CLIENT_HPP
#define FLEX_OLLAMA_CLIENT_HPP

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
 * Validates whether a participant role string is one of the supported Ollama roles.
 */
inline bool isValidRole(std::string_view role) noexcept
{
    return role == "system" || role == "user" || role == "assistant";
}

/**
 * Validated chat message representation.
 */
struct ChatMessage {
    std::string role; // "system", "user", "assistant"
    std::string content;

    ChatMessage() = default;
    ChatMessage(std::string r, std::string c)
        : role(std::move(r)), content(std::move(c))
    {
    }

    bool operator==(const ChatMessage& other) const noexcept
    {
        return role == other.role && content == other.content;
    }
};

/**
 * Generation options passed to Ollama (temperature, context window, etc.).
 */
struct OllamaOptions {
    double temperature{0.2};
    std::optional<int> numCtx{4096};
};

/**
 * Configuration governing Ollama connection and runtime parameters.
 */
struct OllamaConfig {
    std::string baseUrl{"http://127.0.0.1:11434"};
    std::string model{"qwen2.5-coder:3b"};
    double timeoutSeconds{30.0};
    OllamaOptions options;

    /**
     * Resolves configuration with standard precedence:
     * 1. Environment variables (FLEX_OLLAMA_BASE_URL, FLEX_OLLAMA_MODEL, etc.)
     * 2. Built-in defaults (http://127.0.0.1:11434, qwen2.5-coder:3b, 30.0s).
     */
    static OllamaConfig fromEnvironment();
};

/**
 * Structured completion response from Ollama.
 */
struct ChatResponse {
    bool success{false};
    std::string text;
    std::string model;
    int statusCode{0};
    std::string errorMessage;

    // Performance and execution metrics
    int64_t totalDurationNs{0};
    int64_t loadDurationNs{0};
    int64_t promptEvalCount{0};
    int64_t evalCount{0};
    int64_t evalDurationNs{0};
    std::string doneReason;
    std::string rawJson;
};

/**
 * Strongly-typed error classification for Ollama operations.
 */
enum class OllamaErrorCode {
    None = 0,
    InvalidInput,
    NetworkError,
    Timeout,
    HttpError,
    ModelNotFound,
    MalformedJson,
    UnexpectedPayload,
    Cancelled
};

/**
 * Detailed error structure reported to callers.
 */
struct OllamaError {
    OllamaErrorCode code{OllamaErrorCode::None};
    int httpStatus{0};
    std::string message;

    [[nodiscard]] bool hasError() const noexcept
    {
        return code != OllamaErrorCode::None;
    }
};

class OllamaClientImpl;

/**
 * Asynchronous, event-loop friendly native C++ client for Ollama inference.
 *
 * Design Guarantees:
 * - Powered by Qt 6 Network (QNetworkAccessManager); non-blocking and GUI-thread safe.
 * - Supports non-streaming and streaming (newline-delimited JSON) requests.
 * - Provides timeout handling, cancellation tokens, and strict error classification.
 * - Does not log private conversation text or prompt bodies.
 * - Exposes both asynchronous callback/signal workflows and synchronous testing helpers.
 */
class OllamaClient : public QObject {
    Q_OBJECT

public:
    explicit OllamaClient(const OllamaConfig& config = OllamaConfig::fromEnvironment(),
                          QObject* parent = nullptr);
    ~OllamaClient() override;

    // Non-copyable
    OllamaClient(const OllamaClient&) = delete;
    OllamaClient& operator=(const OllamaClient&) = delete;

    // QObjects have identity and cannot be copied or moved
    OllamaClient(OllamaClient&&) = delete;
    OllamaClient& operator=(OllamaClient&&) = delete;

    // Configuration Accessors
    [[nodiscard]] const OllamaConfig& config() const;
    void setConfig(const OllamaConfig& config);
    void setBaseUrl(const std::string& url);
    void setModel(const std::string& model);
    void setTimeoutSeconds(double seconds);

    // Asynchronous Chat Operations
    uint64_t chatAsync(
        const std::vector<ChatMessage>& messages,
        std::function<void(const ChatResponse&)> onComplete,
        std::function<void(const OllamaError&)> onError = nullptr,
        const std::optional<double>& timeoutOverride = std::nullopt);

    uint64_t chatStreamAsync(
        const std::vector<ChatMessage>& messages,
        std::function<void(const std::string& tokenChunk)> onToken,
        std::function<void(const ChatResponse& finalResponse)> onComplete,
        std::function<void(const OllamaError&)> onError = nullptr,
        const std::optional<double>& timeoutOverride = std::nullopt);

    // Request Cancellation
    bool cancelRequest(uint64_t requestId);
    void cancelAll();

    // Synchronous Execution Helpers (Event-Loop Driven; Safe for Testing & CLI)
    ChatResponse chatSync(
        const std::vector<ChatMessage>& messages,
        const std::optional<double>& timeoutOverride = std::nullopt,
        OllamaError* outError = nullptr);

    ChatResponse chatStreamSync(
        const std::vector<ChatMessage>& messages,
        std::function<void(const std::string& tokenChunk)> onToken,
        const std::optional<double>& timeoutOverride = std::nullopt,
        OllamaError* outError = nullptr);

    // Health and Model Inspection
    bool checkHealthSync(double timeoutSeconds = 2.0);
    std::vector<std::string> listModelsSync(double timeoutSeconds = 4.0);
    bool isModelAvailableSync(const std::string& modelName, double timeoutSeconds = 3.0);

signals:
    void requestFinished(uint64_t requestId, const flex::ChatResponse& response);
    void requestError(uint64_t requestId, const flex::OllamaError& error);
    void streamToken(uint64_t requestId, const QString& token);

private:
    std::unique_ptr<OllamaClientImpl> m_impl;
};

} // namespace flex

#endif // FLEX_OLLAMA_CLIENT_HPP
