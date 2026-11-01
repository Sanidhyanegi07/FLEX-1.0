#include "flex/storage.hpp"
#include "knowledge_chunker.h"
#include "string_util.h"
#include "memory_index.h"
#include "sqlite_codec.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <mutex>
#include <set>

namespace flex {

namespace {

const std::set<std::string> VALID_STORAGE_ROLES = {"system", "user", "assistant"};

QString utcNowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

std::string normalizeString(const std::string& input)
{
    std::string s = input;
    flex_str_trim(s.data());
    s.resize(std::strlen(s.c_str()));
    return s;
}

std::string toLowerString(const std::string& input)
{
    std::string s(input.size() + 1, '\0');
    size_t len = flex_str_tolower(input.c_str(), s.data(), s.size());
    s.resize(len);
    return s;
}

} // anonymous namespace

class ConversationStorageImpl {
public:
    explicit ConversationStorageImpl(const std::optional<std::string>& dbPath)
    {
        m_rawPath = dbPath.value_or(ConversationStorage::getDefaultDatabasePath());
        m_connectionName = QString("flex_storage_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        initStorage(m_rawPath);
    }

    ~ConversationStorageImpl()
    {
        close();
    }

    void close()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (QSqlDatabase::contains(m_connectionName)) {
            {
                QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
                if (db.isOpen()) {
                    db.close();
                }
            }
            QSqlDatabase::removeDatabase(m_connectionName);
        }
    }

    [[nodiscard]] std::string dbPath() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_currentPath.toStdString();
    }

    [[nodiscard]] bool isDegraded() const noexcept
    {
        return m_isDegraded;
    }

    [[nodiscard]] std::optional<std::string> storageError() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_storageError;
    }

    std::string createConversation(
        const std::string& title,
        const std::optional<std::string>& conversationId)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QString cid = conversationId.has_value() && !conversationId.value().empty()
            ? QString::fromStdString(conversationId.value())
            : QUuid::createUuid().toString(QUuid::WithoutBraces);

        std::string trimmedTitle = normalizeString(title);
        QString cleanTitle = trimmedTitle.empty()
            ? QString::fromStdString(ConversationStorage::DEFAULT_CONVERSATION_TITLE)
            : QString::fromStdString(trimmedTitle);

        QString now = utcNowIso();

        if (!db.transaction()) {
            throw StorageWriteError("Failed to begin transaction for createConversation");
        }

        QSqlQuery query(db);
        query.prepare("INSERT INTO conversations (id, title, created_at, updated_at) "
                      "VALUES (:id, :title, :created_at, :updated_at);");
        query.bindValue(":id", cid);
        query.bindValue(":title", cleanTitle);
        query.bindValue(":created_at", now);
        query.bindValue(":updated_at", now);

        if (!query.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to create conversation: " + query.lastError().text().toStdString());
        }

        if (!db.commit()) {
            db.rollback();
            throw StorageWriteError("Failed to commit conversation creation: " + db.lastError().text().toStdString());
        }

        return cid.toStdString();
    }

    std::vector<ConversationRecord> listConversations(int limit)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("SELECT id, title, created_at, updated_at FROM conversations "
                      "ORDER BY updated_at DESC LIMIT :limit;");
        query.bindValue(":limit", std::max(1, limit));

        if (!query.exec()) {
            throw StorageReadError("Failed to list conversations: " + query.lastError().text().toStdString());
        }

        std::vector<ConversationRecord> results;
        while (query.next()) {
            results.push_back(ConversationRecord{
                query.value(0).toString().toStdString(),
                query.value(1).toString().toStdString(),
                query.value(2).toString().toStdString(),
                query.value(3).toString().toStdString()
            });
        }
        return results;
    }

    std::optional<ConversationRecord> getConversation(const std::string& conversationId)
    {
        if (conversationId.empty()) {
            return std::nullopt;
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("SELECT id, title, created_at, updated_at FROM conversations WHERE id = :id;");
        query.bindValue(":id", QString::fromStdString(conversationId));

        if (!query.exec()) {
            throw StorageReadError("Failed to get conversation: " + query.lastError().text().toStdString());
        }

        if (!query.next()) {
            return std::nullopt;
        }

        return ConversationRecord{
            query.value(0).toString().toStdString(),
            query.value(1).toString().toStdString(),
            query.value(2).toString().toStdString(),
            query.value(3).toString().toStdString()
        };
    }

    void updateConversationTitle(const std::string& conversationId, const std::string& title)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        std::string trimmedTitle = normalizeString(title);
        QString cleanTitle = trimmedTitle.empty()
            ? QString::fromStdString(ConversationStorage::DEFAULT_CONVERSATION_TITLE)
            : QString::fromStdString(trimmedTitle);
        QString now = utcNowIso();

        if (!db.transaction()) {
            throw StorageWriteError("Failed to begin transaction for updateConversationTitle");
        }

        QSqlQuery query(db);
        query.prepare("UPDATE conversations SET title = :title, updated_at = :updated_at WHERE id = :id;");
        query.bindValue(":title", cleanTitle);
        query.bindValue(":updated_at", now);
        query.bindValue(":id", QString::fromStdString(conversationId));

        if (!query.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to update conversation title: " + query.lastError().text().toStdString());
        }

        if (query.numRowsAffected() == 0) {
            db.rollback();
            throw ConversationNotFoundError("Conversation " + conversationId + " not found");
        }

        if (!db.commit()) {
            db.rollback();
            throw StorageWriteError("Failed to commit conversation title update: " + db.lastError().text().toStdString());
        }
    }

    bool deleteConversation(const std::string& conversationId)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        if (!db.transaction()) {
            throw StorageWriteError("Failed to begin transaction for deleteConversation");
        }

        QSqlQuery query(db);
        query.prepare("DELETE FROM conversations WHERE id = :id;");
        query.bindValue(":id", QString::fromStdString(conversationId));

        if (!query.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to delete conversation: " + query.lastError().text().toStdString());
        }

        bool deleted = query.numRowsAffected() > 0;
        if (!db.commit()) {
            db.rollback();
            throw StorageWriteError("Failed to commit delete conversation: " + db.lastError().text().toStdString());
        }

        return deleted;
    }

    std::string saveMessage(
        const std::string& conversationId,
        const std::string& role,
        const std::string& content,
        const std::optional<std::string>& model,
        const std::optional<std::string>& messageId,
        bool autoTitle)
    {
        std::string normRole = toLowerString(normalizeString(role));
        if (VALID_STORAGE_ROLES.find(normRole) == VALID_STORAGE_ROLES.end()) {
            throw std::invalid_argument("Invalid role: " + role + ". Permitted roles: system, user, assistant");
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QString mid = messageId.has_value() && !messageId.value().empty()
            ? QString::fromStdString(messageId.value())
            : QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString cid = QString::fromStdString(conversationId);
        QString now = utcNowIso();

        if (!db.transaction()) {
            throw StorageWriteError("Failed to begin transaction for saveMessage");
        }

        // 1. Verify conversation exists and fetch title
        QSqlQuery titleQuery(db);
        titleQuery.prepare("SELECT title FROM conversations WHERE id = :id;");
        titleQuery.bindValue(":id", cid);
        if (!titleQuery.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to verify conversation: " + titleQuery.lastError().text().toStdString());
        }
        if (!titleQuery.next()) {
            db.rollback();
            throw ConversationNotFoundError("Cannot save message: conversation " + conversationId + " not found");
        }
        QString currentTitle = titleQuery.value(0).toString();

        // 2. Duplicate prevention check (idempotency)
        if (messageId.has_value() && !messageId.value().empty()) {
            QSqlQuery dupQuery(db);
            dupQuery.prepare("SELECT id FROM messages WHERE id = :id;");
            dupQuery.bindValue(":id", mid);
            if (dupQuery.exec() && dupQuery.next()) {
                db.commit();
                return mid.toStdString();
            }
        }

        // 3. Monotonic sequence calculation
        QSqlQuery seqQuery(db);
        seqQuery.prepare("SELECT COALESCE(MAX(sequence_num) + 1, 0) AS next_seq FROM messages WHERE conversation_id = :cid;");
        seqQuery.bindValue(":cid", cid);
        if (!seqQuery.exec() || !seqQuery.next()) {
            db.rollback();
            throw StorageWriteError("Failed to compute sequence number: " + seqQuery.lastError().text().toStdString());
        }
        int64_t nextSeq = seqQuery.value(0).toLongLong();

        // 4. Insert message
        QSqlQuery insQuery(db);
        insQuery.prepare("INSERT INTO messages (id, conversation_id, role, content, model, sequence_num, created_at) "
                         "VALUES (:id, :conversation_id, :role, :content, :model, :sequence_num, :created_at);");
        insQuery.bindValue(":id", mid);
        insQuery.bindValue(":conversation_id", cid);
        insQuery.bindValue(":role", QString::fromStdString(normRole));
        insQuery.bindValue(":content", QString::fromStdString(content));
        if (model.has_value() && !model.value().empty()) {
            insQuery.bindValue(":model", QString::fromStdString(model.value()));
        } else {
            insQuery.bindValue(":model", QVariant(QMetaType(QMetaType::QString)));
        }
        insQuery.bindValue(":sequence_num", static_cast<qint64>(nextSeq));
        insQuery.bindValue(":created_at", now);

        if (!insQuery.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to insert message: " + insQuery.lastError().text().toStdString());
        }

        // 5. Update conversation updated_at and optionally auto-title
        QString newTitle = currentTitle;
        if (autoTitle && normRole == "user" &&
            (currentTitle == QString::fromStdString(ConversationStorage::DEFAULT_CONVERSATION_TITLE) || currentTitle.isEmpty())) {
            QString cleanExcerpt = QString::fromStdString(content).trimmed().replace("\n", " ").replace("\r", " ");
            if (!cleanExcerpt.isEmpty()) {
                newTitle = cleanExcerpt.left(35) + (cleanExcerpt.length() > 35 ? "..." : "");
            }
        }

        QSqlQuery updQuery(db);
        updQuery.prepare("UPDATE conversations SET title = :title, updated_at = :updated_at WHERE id = :id;");
        updQuery.bindValue(":title", newTitle);
        updQuery.bindValue(":updated_at", now);
        updQuery.bindValue(":id", cid);

        if (!updQuery.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to update conversation timestamp: " + updQuery.lastError().text().toStdString());
        }

        if (!db.commit()) {
            db.rollback();
            throw StorageWriteError("Failed to commit message save: " + db.lastError().text().toStdString());
        }

        return mid.toStdString();
    }

    std::vector<MessageRecord> getMessages(
        const std::string& conversationId,
        const std::optional<int>& limit)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        if (limit.has_value() && limit.value() > 0) {
            query.prepare(
                "SELECT id, conversation_id, role, content, model, sequence_num, created_at "
                "FROM ("
                "    SELECT id, conversation_id, role, content, model, sequence_num, created_at "
                "    FROM messages "
                "    WHERE conversation_id = :cid "
                "    ORDER BY sequence_num DESC "
                "    LIMIT :limit"
                ") sub "
                "ORDER BY sequence_num ASC;"
            );
            query.bindValue(":cid", QString::fromStdString(conversationId));
            query.bindValue(":limit", limit.value());
        } else {
            query.prepare(
                "SELECT id, conversation_id, role, content, model, sequence_num, created_at "
                "FROM messages "
                "WHERE conversation_id = :cid "
                "ORDER BY sequence_num ASC;"
            );
            query.bindValue(":cid", QString::fromStdString(conversationId));
        }

        if (!query.exec()) {
            throw StorageReadError("Failed to get messages: " + query.lastError().text().toStdString());
        }

        std::vector<MessageRecord> results;
        while (query.next()) {
            std::optional<std::string> modelOpt = std::nullopt;
            if (!query.value(4).isNull()) {
                modelOpt = query.value(4).toString().toStdString();
            }
            results.push_back(MessageRecord{
                query.value(0).toString().toStdString(),
                query.value(1).toString().toStdString(),
                query.value(2).toString().toStdString(),
                query.value(3).toString().toStdString(),
                modelOpt,
                query.value(5).toLongLong(),
                query.value(6).toString().toStdString()
            });
        }
        return results;
    }

    int64_t getMessageCount(const std::string& conversationId)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("SELECT COUNT(*) FROM messages WHERE conversation_id = :cid;");
        query.bindValue(":cid", QString::fromStdString(conversationId));

        if (!query.exec() || !query.next()) {
            throw StorageReadError("Failed to count messages: " + query.lastError().text().toStdString());
        }
        return query.value(0).toLongLong();
    }

    int64_t getConversationCount(bool filterEmpty)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        if (filterEmpty) {
            query.prepare(
                "SELECT COUNT(*) FROM conversations c "
                "WHERE EXISTS (SELECT 1 FROM messages m WHERE m.conversation_id = c.id) "
                "   OR c.title != :default_title;"
            );
            query.bindValue(":default_title", QString::fromStdString(ConversationStorage::DEFAULT_CONVERSATION_TITLE));
        } else {
            query.prepare("SELECT COUNT(*) FROM conversations;");
        }

        if (!query.exec() || !query.next()) {
            throw StorageReadError("Failed to count conversations: " + query.lastError().text().toStdString());
        }
        return query.value(0).toLongLong();
    }

    std::string saveMemoryEntry(
        const std::string& key,
        const std::string& content,
        const std::string& category,
        const std::optional<std::string>& id)
    {
        std::string cleanKey = normalizeString(key);
        std::string cleanContent = normalizeString(content);
        std::string cleanCat = category.empty() ? "general" : normalizeString(category);

        if (!flex_mem_validate_fact(cleanKey.c_str(), cleanContent.c_str(), cleanCat.c_str())) {
            throw StorageWriteError("Invalid memory fact: key and content must be non-empty within length limits.");
        }

        std::string memId = id.has_value() ? normalizeString(id.value())
                                            : QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        std::string nowIso = utcNowIso().toStdString();

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare(
            "INSERT INTO memory_entries (id, key, content, category, created_at, updated_at) "
            "VALUES (:id, :key, :content, :category, :created_at, :updated_at) "
            "ON CONFLICT(id) DO UPDATE SET "
            "  key = excluded.key, "
            "  content = excluded.content, "
            "  category = excluded.category, "
            "  updated_at = excluded.updated_at;"
        );
        query.bindValue(":id", QString::fromStdString(memId));
        query.bindValue(":key", QString::fromStdString(cleanKey));
        query.bindValue(":content", QString::fromStdString(cleanContent));
        query.bindValue(":category", QString::fromStdString(cleanCat));
        query.bindValue(":created_at", QString::fromStdString(nowIso));
        query.bindValue(":updated_at", QString::fromStdString(nowIso));

        if (!query.exec()) {
            throw StorageWriteError("Failed to save memory entry: " + query.lastError().text().toStdString());
        }
        return memId;
    }

    std::vector<MemoryEntry> listMemoryEntries(int limit)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("SELECT id, key, content, category, created_at, updated_at FROM memory_entries ORDER BY updated_at DESC LIMIT :limit;");
        query.bindValue(":limit", std::max(1, limit));

        if (!query.exec()) {
            throw StorageReadError("Failed to list memory entries: " + query.lastError().text().toStdString());
        }

        std::vector<MemoryEntry> results;
        while (query.next()) {
            MemoryEntry entry;
            entry.id = query.value(0).toString().toStdString();
            entry.key = query.value(1).toString().toStdString();
            entry.content = query.value(2).toString().toStdString();
            entry.category = query.value(3).toString().toStdString();
            entry.createdAt = query.value(4).toString().toStdString();
            entry.updatedAt = query.value(5).toString().toStdString();
            results.push_back(std::move(entry));
        }
        return results;
    }

    std::optional<MemoryEntry> getMemoryEntry(const std::string& id)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("SELECT id, key, content, category, created_at, updated_at FROM memory_entries WHERE id = :id;");
        query.bindValue(":id", QString::fromStdString(id));

        if (!query.exec()) {
            throw StorageReadError("Failed to get memory entry: " + query.lastError().text().toStdString());
        }

        if (query.next()) {
            MemoryEntry entry;
            entry.id = query.value(0).toString().toStdString();
            entry.key = query.value(1).toString().toStdString();
            entry.content = query.value(2).toString().toStdString();
            entry.category = query.value(3).toString().toStdString();
            entry.createdAt = query.value(4).toString().toStdString();
            entry.updatedAt = query.value(5).toString().toStdString();
            return entry;
        }
        return std::nullopt;
    }

    bool updateMemoryEntry(
        const std::string& id,
        const std::string& key,
        const std::string& content,
        const std::string& category)
    {
        std::string cleanKey = normalizeString(key);
        std::string cleanContent = normalizeString(content);
        std::string cleanCat = category.empty() ? "general" : normalizeString(category);

        if (!flex_mem_validate_fact(cleanKey.c_str(), cleanContent.c_str(), cleanCat.c_str())) {
            throw StorageWriteError("Invalid memory fact: key and content must be non-empty within length limits.");
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("UPDATE memory_entries SET key = :key, content = :content, category = :category, updated_at = :updated_at WHERE id = :id;");
        query.bindValue(":key", QString::fromStdString(cleanKey));
        query.bindValue(":content", QString::fromStdString(cleanContent));
        query.bindValue(":category", QString::fromStdString(cleanCat));
        query.bindValue(":updated_at", utcNowIso());
        query.bindValue(":id", QString::fromStdString(id));

        if (!query.exec()) {
            throw StorageWriteError("Failed to update memory entry: " + query.lastError().text().toStdString());
        }
        return query.numRowsAffected() > 0;
    }

    bool deleteMemoryEntry(const std::string& id)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("DELETE FROM memory_entries WHERE id = :id;");
        query.bindValue(":id", QString::fromStdString(id));

        if (!query.exec()) {
            throw StorageWriteError("Failed to delete memory entry: " + query.lastError().text().toStdString());
        }
        return query.numRowsAffected() > 0;
    }

    std::vector<MemoryEntry> searchMemoryEntries(const std::string& queryText, int limit)
    {
        std::string cleanQuery = normalizeString(queryText);
        if (cleanQuery.empty()) {
            return listMemoryEntries(limit);
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QString pattern = QString("%%1%").arg(QString::fromStdString(cleanQuery));
        QSqlQuery query(db);
        query.prepare(
            "SELECT id, key, content, category, created_at, updated_at "
            "FROM memory_entries "
            "WHERE key LIKE :pat1 OR content LIKE :pat2 OR category LIKE :pat3 "
            "ORDER BY updated_at DESC "
            "LIMIT :limit;"
        );
        query.bindValue(":pat1", pattern);
        query.bindValue(":pat2", pattern);
        query.bindValue(":pat3", pattern);
        query.bindValue(":limit", std::max(1, limit));

        if (!query.exec()) {
            throw StorageReadError("Failed to search memory entries: " + query.lastError().text().toStdString());
        }

        std::vector<MemoryEntry> results;
        while (query.next()) {
            MemoryEntry entry;
            entry.id = query.value(0).toString().toStdString();
            entry.key = query.value(1).toString().toStdString();
            entry.content = query.value(2).toString().toStdString();
            entry.category = query.value(3).toString().toStdString();
            entry.createdAt = query.value(4).toString().toStdString();
            entry.updatedAt = query.value(5).toString().toStdString();
            results.push_back(std::move(entry));
        }

        if (!results.empty()) {
            std::sort(results.begin(), results.end(), [&](const MemoryEntry& a, const MemoryEntry& b) {
                float sa = flex_mem_fact_relevance(cleanQuery.c_str(), a.key.c_str(), a.content.c_str());
                float sb = flex_mem_fact_relevance(cleanQuery.c_str(), b.key.c_str(), b.content.c_str());
                return sa > sb;
            });
        }

        return results;
    }

std::string saveDocument(
        const std::string& filename,
        const std::string& originalPath,
        const std::string& content,
        std::string* outError = nullptr)
    {
        if (content.empty()) {
            if (outError) *outError = "Document content is empty.";
            return {};
        }

        if (!flex_kb_validate_document_text(content.c_str(), content.length(), FLEX_KB_MAX_FILE_BYTES)) {
            if (outError) *outError = "Document text is invalid, binary, or exceeds 10MB limit.";
            return {};
        }

        // Normalize text
        std::string normBuffer;
        normBuffer.resize(content.length() + 16);
        size_t normLen = flex_kb_normalize_text(content.c_str(), content.length(), &normBuffer[0], normBuffer.size());
        normBuffer.resize(normLen);

        if (normLen == 0) {
            if (outError) *outError = "Document contains only whitespace.";
            return {};
        }

        // Compute content hash
        char hashBuf[16];
        flex_kb_compute_content_hash(normBuffer.c_str(), normLen, hashBuf);
        std::string contentHash(hashBuf);

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        // Check duplicate
        QSqlQuery dupQuery(db);
        dupQuery.prepare("SELECT id FROM documents WHERE content_hash = :hash LIMIT 1;");
        dupQuery.bindValue(":hash", QString::fromStdString(contentHash));
        if (dupQuery.exec() && dupQuery.next()) {
            std::string existingId = dupQuery.value(0).toString().toStdString();
            if (outError) *outError = "Duplicate document: identical content already exists in Knowledge Base.";
            return existingId;
        }

        // Split chunks
        std::vector<flex_kb_chunk_t> chunks(FLEX_KB_MAX_CHUNKS_PER_DOC);
        flex_kb_chunk_config_t cfg = flex_kb_default_config();
        size_t chunkCount = flex_kb_split_chunks(
            normBuffer.c_str(), normLen, &cfg, chunks.data(), chunks.size());

        if (chunkCount == 0) {
            if (outError) *outError = "Failed to extract chunks from document.";
            return {};
        }

        QString docId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString now = utcNowIso();
        QString cleanFilename = QString::fromStdString(filename.empty() ? "Untitled Document" : filename);
        QString cleanPath = QString::fromStdString(originalPath);

        if (!db.transaction()) {
            throw StorageWriteError("Failed to begin transaction for document import");
        }

        QSqlQuery docQuery(db);
        docQuery.prepare(
            "INSERT INTO documents (id, filename, original_path, content_hash, file_size, chunk_count, created_at) "
            "VALUES (:id, :filename, :original_path, :content_hash, :file_size, :chunk_count, :created_at);"
        );
        docQuery.bindValue(":id", docId);
        docQuery.bindValue(":filename", cleanFilename);
        docQuery.bindValue(":original_path", cleanPath);
        docQuery.bindValue(":content_hash", QString::fromStdString(contentHash));
        docQuery.bindValue(":file_size", static_cast<qint64>(content.length()));
        docQuery.bindValue(":chunk_count", static_cast<qint64>(chunkCount));
        docQuery.bindValue(":created_at", now);

        if (!docQuery.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to insert document: " + docQuery.lastError().text().toStdString());
        }

        QSqlQuery chunkQuery(db);
        chunkQuery.prepare(
            "INSERT INTO document_chunks (id, document_id, chunk_index, content, token_count, created_at) "
            "VALUES (:id, :document_id, :chunk_index, :content, :token_count, :created_at);"
        );

        for (size_t i = 0; i < chunkCount; ++i) {
            QString chunkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            chunkQuery.bindValue(":id", chunkId);
            chunkQuery.bindValue(":document_id", docId);
            chunkQuery.bindValue(":chunk_index", static_cast<qint64>(chunks[i].chunk_index));
            chunkQuery.bindValue(":content", QString::fromUtf8(chunks[i].content));
            chunkQuery.bindValue(":token_count", static_cast<qint64>(chunks[i].estimated_tokens));
            chunkQuery.bindValue(":created_at", now);

            if (!chunkQuery.exec()) {
                db.rollback();
                throw StorageWriteError("Failed to insert document chunk: " + chunkQuery.lastError().text().toStdString());
            }
        }

        if (!db.commit()) {
            db.rollback();
            throw StorageWriteError("Failed to commit document import: " + db.lastError().text().toStdString());
        }

        return docId.toStdString();
    }

    std::vector<DocumentRecord> listDocuments(int limit)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare(
            "SELECT id, filename, original_path, content_hash, file_size, chunk_count, created_at "
            "FROM documents ORDER BY created_at DESC LIMIT :limit;"
        );
        query.bindValue(":limit", std::max(1, limit));

        if (!query.exec()) {
            throw StorageReadError("Failed to list documents: " + query.lastError().text().toStdString());
        }

        std::vector<DocumentRecord> results;
        while (query.next()) {
            DocumentRecord doc;
            doc.id = query.value(0).toString().toStdString();
            doc.filename = query.value(1).toString().toStdString();
            doc.originalPath = query.value(2).toString().toStdString();
            doc.contentHash = query.value(3).toString().toStdString();
            doc.fileSize = query.value(4).toLongLong();
            doc.chunkCount = query.value(5).toLongLong();
            doc.createdAt = query.value(6).toString().toStdString();
            results.push_back(std::move(doc));
        }
        return results;
    }

    std::optional<DocumentRecord> getDocument(const std::string& docId)
    {
        if (docId.empty()) return std::nullopt;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare(
            "SELECT id, filename, original_path, content_hash, file_size, chunk_count, created_at "
            "FROM documents WHERE id = :id LIMIT 1;"
        );
        query.bindValue(":id", QString::fromStdString(docId));

        if (!query.exec() || !query.next()) {
            return std::nullopt;
        }

        DocumentRecord doc;
        doc.id = query.value(0).toString().toStdString();
        doc.filename = query.value(1).toString().toStdString();
        doc.originalPath = query.value(2).toString().toStdString();
        doc.contentHash = query.value(3).toString().toStdString();
        doc.fileSize = query.value(4).toLongLong();
        doc.chunkCount = query.value(5).toLongLong();
        doc.createdAt = query.value(6).toString().toStdString();
        return doc;
    }

    bool deleteDocument(const std::string& docId)
    {
        if (docId.empty()) return false;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        if (!db.transaction()) {
            throw StorageWriteError("Failed to begin transaction for document deletion");
        }

        QSqlQuery chunkQuery(db);
        chunkQuery.prepare("DELETE FROM document_chunks WHERE document_id = :id;");
        chunkQuery.bindValue(":id", QString::fromStdString(docId));
        chunkQuery.exec();

        QSqlQuery docQuery(db);
        docQuery.prepare("DELETE FROM documents WHERE id = :id;");
        docQuery.bindValue(":id", QString::fromStdString(docId));
        if (!docQuery.exec()) {
            db.rollback();
            throw StorageWriteError("Failed to delete document: " + docQuery.lastError().text().toStdString());
        }

        bool deleted = docQuery.numRowsAffected() > 0;
        db.commit();
        return deleted;
    }

    std::vector<DocumentChunkRecord> listDocumentChunks(const std::string& docId, int limit)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare(
            "SELECT c.id, c.document_id, d.filename, c.chunk_index, c.content, c.token_count, c.created_at "
            "FROM document_chunks c "
            "JOIN documents d ON c.document_id = d.id "
            "WHERE c.document_id = :doc_id "
            "ORDER BY c.chunk_index ASC "
            "LIMIT :limit;"
        );
        query.bindValue(":doc_id", QString::fromStdString(docId));
        query.bindValue(":limit", std::max(1, limit));

        if (!query.exec()) {
            throw StorageReadError("Failed to list document chunks: " + query.lastError().text().toStdString());
        }

        std::vector<DocumentChunkRecord> results;
        while (query.next()) {
            DocumentChunkRecord chk;
            chk.id = query.value(0).toString().toStdString();
            chk.documentId = query.value(1).toString().toStdString();
            chk.documentName = query.value(2).toString().toStdString();
            chk.chunkIndex = query.value(3).toLongLong();
            chk.content = query.value(4).toString().toStdString();
            chk.tokenCount = query.value(5).toLongLong();
            chk.createdAt = query.value(6).toString().toStdString();
            results.push_back(std::move(chk));
        }
        return results;
    }

    std::vector<DocumentChunkRecord> searchKnowledgeChunks(const std::string& queryText, int limit)
    {
        std::string cleanQuery = normalizeString(queryText);
        if (cleanQuery.empty()) {
            return {};
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare(
            "SELECT c.id, c.document_id, d.filename, c.chunk_index, c.content, c.token_count, c.created_at "
            "FROM document_chunks c "
            "JOIN documents d ON c.document_id = d.id "
            "ORDER BY c.created_at DESC "
            "LIMIT 200;"
        );

        if (!query.exec()) {
            throw StorageReadError("Failed to search knowledge chunks: " + query.lastError().text().toStdString());
        }

        std::vector<DocumentChunkRecord> candidates;
        while (query.next()) {
            DocumentChunkRecord chk;
            chk.id = query.value(0).toString().toStdString();
            chk.documentId = query.value(1).toString().toStdString();
            chk.documentName = query.value(2).toString().toStdString();
            chk.chunkIndex = query.value(3).toLongLong();
            chk.content = query.value(4).toString().toStdString();
            chk.tokenCount = query.value(5).toLongLong();
            chk.createdAt = query.value(6).toString().toStdString();

            float score = flex_kb_score_chunk_relevance(cleanQuery.c_str(), chk.content.c_str());
            chk.relevanceScore = score;
            if (score > 0.4f) {
                candidates.push_back(std::move(chk));
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const DocumentChunkRecord& a, const DocumentChunkRecord& b) {
            return a.relevanceScore > b.relevanceScore;
        });

        if (static_cast<int>(candidates.size()) > limit) {
            candidates.resize(limit);
        }

        return candidates;
    }

    void getKnowledgeStats(int64_t* outDocCount, int64_t* outChunkCount)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        if (outDocCount) {
            QSqlQuery q(db);
            q.exec("SELECT COUNT(*) FROM documents;");
            *outDocCount = (q.next()) ? q.value(0).toLongLong() : 0;
        }

        if (outChunkCount) {
            QSqlQuery q(db);
            q.exec("SELECT COUNT(*) FROM document_chunks;");
            *outChunkCount = (q.next()) ? q.value(0).toLongLong() : 0;
        }
    }

    int64_t getTotalMessageCount()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QSqlQuery query(db);
        query.prepare("SELECT COUNT(*) FROM messages;");
        if (!query.exec() || !query.next()) {
            throw StorageReadError("Failed to count total messages: " + query.lastError().text().toStdString());
        }
        return query.value(0).toLongLong();
    }

    std::vector<ConversationRecord> searchConversations(
        const std::string& queryText,
        int limit,
        bool filterEmpty)
    {
        std::string cleanQuery = normalizeString(queryText);
        if (cleanQuery.empty()) {
            auto all = listConversations(limit);
            if (filterEmpty) {
                std::vector<ConversationRecord> filtered;
                for (const auto& rec : all) {
                    if (getMessageCount(rec.id) > 0 || rec.title != ConversationStorage::DEFAULT_CONVERSATION_TITLE) {
                        filtered.push_back(rec);
                    }
                }
                return filtered;
            }
            return all;
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        QSqlDatabase db = getDatabase();

        QString pattern = QString("%%1%").arg(QString::fromStdString(cleanQuery));
        QSqlQuery query(db);

        if (filterEmpty) {
            query.prepare(
                "SELECT DISTINCT c.id, c.title, c.created_at, c.updated_at "
                "FROM conversations c "
                "LEFT JOIN messages m ON m.conversation_id = c.id "
                "WHERE (c.title LIKE :pat1 OR m.content LIKE :pat2) "
                "  AND (m.id IS NOT NULL OR c.title != :default_title) "
                "ORDER BY c.updated_at DESC "
                "LIMIT :limit;"
            );
            query.bindValue(":pat1", pattern);
            query.bindValue(":pat2", pattern);
            query.bindValue(":default_title", QString::fromStdString(ConversationStorage::DEFAULT_CONVERSATION_TITLE));
            query.bindValue(":limit", std::max(1, limit));
        } else {
            query.prepare(
                "SELECT DISTINCT c.id, c.title, c.created_at, c.updated_at "
                "FROM conversations c "
                "LEFT JOIN messages m ON m.conversation_id = c.id "
                "WHERE c.title LIKE :pat1 OR m.content LIKE :pat2 "
                "ORDER BY c.updated_at DESC "
                "LIMIT :limit;"
            );
            query.bindValue(":pat1", pattern);
            query.bindValue(":pat2", pattern);
            query.bindValue(":limit", std::max(1, limit));
        }

        if (!query.exec()) {
            throw StorageReadError("Failed to search conversations: " + query.lastError().text().toStdString());
        }

        std::vector<ConversationRecord> results;
        while (query.next()) {
            results.push_back(ConversationRecord{
                query.value(0).toString().toStdString(),
                query.value(1).toString().toStdString(),
                query.value(2).toString().toStdString(),
                query.value(3).toString().toStdString()
            });
        }
        return results;
    }

private:
    QSqlDatabase getDatabase()
    {
        if (!QSqlDatabase::contains(m_connectionName)) {
            throw StorageReadError("Storage connection is not open.");
        }
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (!db.isOpen()) {
            throw StorageReadError("Storage connection is closed.");
        }
        return db;
    }

    void initStorage(const std::string& targetPath)
    {
        m_currentPath = QString::fromStdString(targetPath);

        if (m_currentPath != ":memory:") {
            QFileInfo fi(m_currentPath);
            QDir dir = fi.dir();
            if (!dir.exists()) {
                if (!dir.mkpath(".")) {
                    fallbackToMemory("Directory creation failed for " + targetPath);
                    return;
                }
            }
        }

        try {
            createConnection(m_currentPath);
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            applyMigrations(db);
        } catch (const StorageInitError& initErr) {
            fallbackToMemory(initErr.what());
        } catch (const std::exception& exc) {
            quarantineAndFallback(m_currentPath, exc.what());
        }
    }

    void createConnection(const QString& path)
    {
        if (QSqlDatabase::contains(m_connectionName)) {
            {
                QSqlDatabase oldDb = QSqlDatabase::database(m_connectionName, false);
                if (oldDb.isOpen()) {
                    oldDb.close();
                }
            }
            QSqlDatabase::removeDatabase(m_connectionName);
        }

        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        db.setDatabaseName(path);
        if (!db.open()) {
            throw StorageInitError("Failed to open SQLite database at " + path.toStdString() + ": " + db.lastError().text().toStdString());
        }

        QSqlQuery query(db);
        if (!query.exec("PRAGMA foreign_keys = ON;")) {
            throw StorageInitError("Failed to enable foreign_keys pragma");
        }

        if (path != ":memory:") {
            query.exec("PRAGMA journal_mode = WAL;");
            query.exec(QString("PRAGMA busy_timeout = %1;").arg(ConversationStorage::DEFAULT_BUSY_TIMEOUT_MS));
            query.exec("PRAGMA synchronous = NORMAL;");
        }
    }

    void applyMigrations(QSqlDatabase& db)
    {
        if (!db.transaction()) {
            throw StorageInitError("Failed to start migration transaction: " + db.lastError().text().toStdString());
        }

        QSqlQuery query(db);
        if (!query.exec("CREATE TABLE IF NOT EXISTS schema_version ("
                        "version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL);")) {
            db.rollback();
            throw StorageInitError("Failed to create schema_version table: " + query.lastError().text().toStdString());
        }

        if (!query.exec("SELECT MAX(version) FROM schema_version;")) {
            db.rollback();
            throw StorageInitError("Failed to query schema_version: " + query.lastError().text().toStdString());
        }

        int currentVer = 0;
        if (query.next() && !query.value(0).isNull()) {
            currentVer = query.value(0).toInt();
        }

        if (currentVer > ConversationStorage::CURRENT_SCHEMA_VERSION) {
            db.rollback();
            throw StorageInitError(
                "Database schema version " + std::to_string(currentVer) +
                " is newer than supported version " + std::to_string(ConversationStorage::CURRENT_SCHEMA_VERSION) + "."
            );
        }

        if (currentVer < 1) {
            if (!query.exec("CREATE TABLE IF NOT EXISTS conversations ("
                            "id TEXT PRIMARY KEY, title TEXT NOT NULL, "
                            "created_at TEXT NOT NULL, updated_at TEXT NOT NULL);")) {
                db.rollback();
                throw StorageInitError("Failed to create conversations table: " + query.lastError().text().toStdString());
            }

            if (!query.exec("CREATE TABLE IF NOT EXISTS messages ("
                            "id TEXT PRIMARY KEY, conversation_id TEXT NOT NULL, "
                            "role TEXT NOT NULL CHECK(role IN ('system', 'user', 'assistant')), "
                            "content TEXT NOT NULL, model TEXT, sequence_num INTEGER NOT NULL, "
                            "created_at TEXT NOT NULL, "
                            "FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE);")) {
                db.rollback();
                throw StorageInitError("Failed to create messages table: " + query.lastError().text().toStdString());
            }

            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_messages_conversation_seq "
                            "ON messages(conversation_id, sequence_num ASC);")) {
                db.rollback();
                throw StorageInitError("Failed to create message sequence index: " + query.lastError().text().toStdString());
            }

            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_conversations_updated "
                            "ON conversations(updated_at DESC);")) {
                db.rollback();
                throw StorageInitError("Failed to create conversation updated index: " + query.lastError().text().toStdString());
            }

            query.prepare("INSERT INTO schema_version (version, applied_at) VALUES (1, :applied_at);");
            query.exec("CREATE TABLE IF NOT EXISTS memory_entries ("
                       "id TEXT PRIMARY KEY, key TEXT NOT NULL, "
                       "content TEXT NOT NULL, category TEXT DEFAULT 'general', "
                       "created_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
            query.exec("CREATE INDEX IF NOT EXISTS idx_memory_entries_key ON memory_entries(key);");
            query.bindValue(":applied_at", utcNowIso());
            if (!query.exec()) {
                db.rollback();
                throw StorageInitError("Failed to record schema version: " + query.lastError().text().toStdString());
            }
        }

        if (currentVer < 2) {
            if (!query.exec("CREATE TABLE IF NOT EXISTS documents ("
                            "id TEXT PRIMARY KEY, filename TEXT NOT NULL, "
                            "original_path TEXT NOT NULL, content_hash TEXT NOT NULL, "
                            "file_size INTEGER NOT NULL, chunk_count INTEGER NOT NULL, "
                            "created_at TEXT NOT NULL);")) {
                db.rollback();
                throw StorageInitError("Failed to create documents table: " + query.lastError().text().toStdString());
            }

            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_documents_hash ON documents(content_hash);")) {
                db.rollback();
                throw StorageInitError("Failed to create documents hash index: " + query.lastError().text().toStdString());
            }

            if (!query.exec("CREATE TABLE IF NOT EXISTS document_chunks ("
                            "id TEXT PRIMARY KEY, document_id TEXT NOT NULL, "
                            "chunk_index INTEGER NOT NULL, content TEXT NOT NULL, "
                            "token_count INTEGER NOT NULL, created_at TEXT NOT NULL, "
                            "FOREIGN KEY (document_id) REFERENCES documents(id) ON DELETE CASCADE);")) {
                db.rollback();
                throw StorageInitError("Failed to create document_chunks table: " + query.lastError().text().toStdString());
            }

            if (!query.exec("CREATE INDEX IF NOT EXISTS idx_chunks_doc_idx ON document_chunks(document_id, chunk_index ASC);")) {
                db.rollback();
                throw StorageInitError("Failed to create document_chunks index: " + query.lastError().text().toStdString());
            }

            query.prepare("INSERT INTO schema_version (version, applied_at) VALUES (2, :applied_at);");
            query.bindValue(":applied_at", utcNowIso());
            if (!query.exec()) {
                db.rollback();
                throw StorageInitError("Failed to record schema version 2: " + query.lastError().text().toStdString());
            }
        }

        if (!db.commit()) {
            db.rollback();
            throw StorageInitError("Failed to commit migration transaction: " + db.lastError().text().toStdString());
        }
    }

    void quarantineAndFallback(const QString& corruptPath, const std::string& error)
    {
        if (corruptPath != ":memory:" && QFile::exists(corruptPath)) {
            QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmss");
            QString quarantinePath = QString("%1.corrupt.%2").arg(corruptPath, timestamp);
            QFile::copy(corruptPath, quarantinePath);

            // Also quarantine WAL and SHM sidecars
            for (const char* ext : {"-wal", "-shm"}) {
                QString sidecar = corruptPath + ext;
                if (QFile::exists(sidecar)) {
                    QFile::copy(sidecar, quarantinePath + ext);
                }
            }
        }

        fallbackToMemory("Database corrupted (" + error + "). Preserved original. Operating in temporary in-memory mode.");
    }

    void fallbackToMemory(const std::string& reason)
    {
        m_isDegraded = true;
        m_storageError = reason;
        m_currentPath = ":memory:";

        try {
            createConnection(":memory:");
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            applyMigrations(db);
        } catch (const std::exception& exc) {
            throw StorageInitError("Fatal: In-memory fallback initialization failed: " + std::string(exc.what()));
        }
    }

    std::string m_rawPath;
    QString m_connectionName;
    QString m_currentPath;
    bool m_isDegraded = false;
    std::optional<std::string> m_storageError;
    mutable std::recursive_mutex m_mutex;
};

// -----------------------------------------------------------------------------
// ConversationStorage Public Forwarding Interface
// -----------------------------------------------------------------------------

ConversationStorage::ConversationStorage(const std::optional<std::string>& dbPath)
    : m_impl(std::make_unique<ConversationStorageImpl>(dbPath))
{
}

ConversationStorage::~ConversationStorage() = default;

ConversationStorage::ConversationStorage(ConversationStorage&& other) noexcept = default;
ConversationStorage& ConversationStorage::operator=(ConversationStorage&& other) noexcept = default;

std::string ConversationStorage::createConversation(
    const std::string& title,
    const std::optional<std::string>& conversationId)
{
    return m_impl->createConversation(title, conversationId);
}

std::vector<ConversationRecord> ConversationStorage::listConversations(int limit)
{
    return m_impl->listConversations(limit);
}

std::optional<ConversationRecord> ConversationStorage::getConversation(const std::string& conversationId)
{
    return m_impl->getConversation(conversationId);
}

void ConversationStorage::updateConversationTitle(const std::string& conversationId, const std::string& title)
{
    m_impl->updateConversationTitle(conversationId, title);
}

bool ConversationStorage::deleteConversation(const std::string& conversationId)
{
    return m_impl->deleteConversation(conversationId);
}

std::string ConversationStorage::saveMessage(
    const std::string& conversationId,
    const std::string& role,
    const std::string& content,
    const std::optional<std::string>& model,
    const std::optional<std::string>& messageId,
    bool autoTitle)
{
    return m_impl->saveMessage(conversationId, role, content, model, messageId, autoTitle);
}

std::vector<MessageRecord> ConversationStorage::getMessages(
    const std::string& conversationId,
    const std::optional<int>& limit)
{
    return m_impl->getMessages(conversationId, limit);
}

int64_t ConversationStorage::getMessageCount(const std::string& conversationId)
{
    return m_impl->getMessageCount(conversationId);
}

int64_t ConversationStorage::getConversationCount(bool filterEmpty)
{
    return m_impl->getConversationCount(filterEmpty);
}

int64_t ConversationStorage::getTotalMessageCount()
{
    return m_impl->getTotalMessageCount();
}

std::vector<ConversationRecord> ConversationStorage::searchConversations(
    const std::string& query,
    int limit,
    bool filterEmpty)
{
    return m_impl->searchConversations(query, limit, filterEmpty);
}

std::string ConversationStorage::saveMemoryEntry(
    const std::string& key,
    const std::string& content,
    const std::string& category,
    const std::optional<std::string>& id)
{
    return m_impl->saveMemoryEntry(key, content, category, id);
}

std::vector<MemoryEntry> ConversationStorage::listMemoryEntries(int limit)
{
    return m_impl->listMemoryEntries(limit);
}

std::optional<MemoryEntry> ConversationStorage::getMemoryEntry(const std::string& id)
{
    return m_impl->getMemoryEntry(id);
}

bool ConversationStorage::updateMemoryEntry(
    const std::string& id,
    const std::string& key,
    const std::string& content,
    const std::string& category)
{
    return m_impl->updateMemoryEntry(id, key, content, category);
}

bool ConversationStorage::deleteMemoryEntry(const std::string& id)
{
    return m_impl->deleteMemoryEntry(id);
}

std::vector<MemoryEntry> ConversationStorage::searchMemoryEntries(const std::string& query, int limit)
{
    return m_impl->searchMemoryEntries(query, limit);
}

void ConversationStorage::close()
{
    if (m_impl) {
        m_impl->close();
    }
}

std::string ConversationStorage::dbPath() const
{
    return m_impl ? m_impl->dbPath() : "";
}

bool ConversationStorage::isDegraded() const noexcept
{
    return m_impl && m_impl->isDegraded();
}

std::optional<std::string> ConversationStorage::storageError() const
{
    return m_impl ? m_impl->storageError() : std::nullopt;
}

std::string ConversationStorage::getDefaultDatabasePath()
{
    QByteArray envOverride = qgetenv("FLEX_DB_PATH");
    if (!envOverride.isEmpty() && !envOverride.trimmed().isEmpty()) {
        return QDir::cleanPath(QString::fromLocal8Bit(envOverride.trimmed())).toStdString();
    }

    QByteArray localAppData = qgetenv("LOCALAPPDATA");
    QString baseDir;
    if (!localAppData.isEmpty() && !localAppData.trimmed().isEmpty()) {
        baseDir = QDir(QString::fromLocal8Bit(localAppData.trimmed())).filePath("FLEX");
    } else {
        baseDir = QDir(QDir::homePath()).filePath(".flex");
    }

    return QDir::cleanPath(QDir(baseDir).filePath("conversations.db")).toStdString();
}

std::string ConversationStorage::saveDocument(
    const std::string& filename,
    const std::string& originalPath,
    const std::string& content,
    std::string* outError)
{
    return m_impl->saveDocument(filename, originalPath, content, outError);
}

std::vector<DocumentRecord> ConversationStorage::listDocuments(int limit)
{
    return m_impl->listDocuments(limit);
}

std::optional<DocumentRecord> ConversationStorage::getDocument(const std::string& docId)
{
    return m_impl->getDocument(docId);
}

bool ConversationStorage::deleteDocument(const std::string& docId)
{
    return m_impl->deleteDocument(docId);
}

std::vector<DocumentChunkRecord> ConversationStorage::listDocumentChunks(const std::string& docId, int limit)
{
    return m_impl->listDocumentChunks(docId, limit);
}

std::vector<DocumentChunkRecord> ConversationStorage::searchKnowledgeChunks(const std::string& query, int limit)
{
    return m_impl->searchKnowledgeChunks(query, limit);
}

void ConversationStorage::getKnowledgeStats(int64_t* outDocCount, int64_t* outChunkCount)
{
    m_impl->getKnowledgeStats(outDocCount, outChunkCount);
}

} // namespace flex
