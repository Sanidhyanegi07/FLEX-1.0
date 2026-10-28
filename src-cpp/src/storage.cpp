#include "flex/storage.hpp"

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
    // trim left
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    // trim right
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
    return s;
}

std::string toLowerString(const std::string& input)
{
    std::string s = input;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
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
            query.bindValue(":applied_at", utcNowIso());
            if (!query.exec()) {
                db.rollback();
                throw StorageInitError("Failed to record schema version: " + query.lastError().text().toStdString());
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

} // namespace flex
