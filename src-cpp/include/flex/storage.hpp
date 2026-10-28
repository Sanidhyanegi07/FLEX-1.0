#ifndef FLEX_STORAGE_HPP
#define FLEX_STORAGE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace flex {

/**
 * Base exception class for persistent conversation storage errors.
 */
class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * Raised when database initialization, connection, or migration fails.
 */
class StorageInitError : public StorageError {
public:
    using StorageError::StorageError;
};

/**
 * Raised when a database write operation fails.
 */
class StorageWriteError : public StorageError {
public:
    using StorageError::StorageError;
};

/**
 * Raised when a database read or query operation fails.
 */
class StorageReadError : public StorageError {
public:
    using StorageError::StorageError;
};

/**
 * Raised when an operation targets a non-existent conversation ID.
 */
class ConversationNotFoundError : public StorageError {
public:
    using StorageError::StorageError;
};

/**
 * Representation of a stored conversation record.
 */
struct ConversationRecord {
    std::string id;
    std::string title;
    std::string createdAt;
    std::string updatedAt;

    bool operator==(const ConversationRecord& other) const noexcept {
        return id == other.id && title == other.title &&
               createdAt == other.createdAt && updatedAt == other.updatedAt;
    }
};

/**
 * Representation of a stored conversation message record.
 */
struct MessageRecord {
    std::string id;
    std::string conversationId;
    std::string role;
    std::string content;
    std::optional<std::string> model;
    int64_t sequenceNum{0};
    std::string createdAt;

    bool operator==(const MessageRecord& other) const noexcept {
        return id == other.id && conversationId == other.conversationId &&
               role == other.role && content == other.content &&
               model == other.model && sequenceNum == other.sequenceNum &&
               createdAt == other.createdAt;
    }
};

class ConversationStorageImpl;

/**
 * Thread-safe persistent SQLite storage for conversations and messages.
 * Matches schema, foreign-key constraints, sequence numbering, and lifecycle of backend/ai/storage.py.
 */
class ConversationStorage {
public:
    static constexpr int CURRENT_SCHEMA_VERSION = 1;
    static constexpr int DEFAULT_BUSY_TIMEOUT_MS = 5000;
    static inline const std::string DEFAULT_CONVERSATION_TITLE = "New Chat";

    explicit ConversationStorage(const std::optional<std::string>& dbPath = std::nullopt);
    ~ConversationStorage();

    // Non-copyable
    ConversationStorage(const ConversationStorage&) = delete;
    ConversationStorage& operator=(const ConversationStorage&) = delete;

    // Movable
    ConversationStorage(ConversationStorage&& other) noexcept;
    ConversationStorage& operator=(ConversationStorage&& other) noexcept;

    // Core Conversation Operations
    std::string createConversation(
        const std::string& title = DEFAULT_CONVERSATION_TITLE,
        const std::optional<std::string>& conversationId = std::nullopt);

    std::vector<ConversationRecord> listConversations(int limit = 30);
    std::optional<ConversationRecord> getConversation(const std::string& conversationId);
    void updateConversationTitle(const std::string& conversationId, const std::string& title);
    bool deleteConversation(const std::string& conversationId);

    // Message Operations
    std::string saveMessage(
        const std::string& conversationId,
        const std::string& role,
        const std::string& content,
        const std::optional<std::string>& model = std::nullopt,
        const std::optional<std::string>& messageId = std::nullopt,
        bool autoTitle = true);

    std::vector<MessageRecord> getMessages(
        const std::string& conversationId,
        const std::optional<int>& limit = std::nullopt);

    int64_t getMessageCount(const std::string& conversationId);
    int64_t getConversationCount(bool filterEmpty = false);
    int64_t getTotalMessageCount();

    std::vector<ConversationRecord> searchConversations(
        const std::string& query,
        int limit = 50,
        bool filterEmpty = false);

    void close();

    // Diagnostics & Mode Queries
    [[nodiscard]] std::string dbPath() const;
    [[nodiscard]] bool isDegraded() const noexcept;
    [[nodiscard]] std::optional<std::string> storageError() const;

    // Path Resolution Helper
    static std::string getDefaultDatabasePath();

private:
    std::unique_ptr<ConversationStorageImpl> m_impl;
};

} // namespace flex

#endif // FLEX_STORAGE_HPP

