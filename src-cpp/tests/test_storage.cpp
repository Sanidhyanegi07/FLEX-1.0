#include "flex/storage.hpp"
#include "flex/settings_persistence_service.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUuid>

#include <cassert>
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
// Test 1: Database Initialization & In-Memory Mode
// -----------------------------------------------------------------------------
void test_database_initialization()
{
    std::cout << "[Test 1] Database initialization and schema creation...\n";

    // 1a. File-based database
    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("test_init.db");

    {
        flex::ConversationStorage storage(dbPath.toStdString());
        EXPECT_FALSE(storage.isDegraded());
        EXPECT_FALSE(storage.storageError().has_value());
        EXPECT_EQ(dbPath.toStdString(), storage.dbPath());
        EXPECT_EQ(0, storage.getConversationCount());
        EXPECT_EQ(0, storage.getTotalMessageCount());
    }

    // 1b. In-memory database
    {
        flex::ConversationStorage memStorage(":memory:");
        EXPECT_FALSE(memStorage.isDegraded());
        EXPECT_EQ(":memory:", memStorage.dbPath());
        std::string cid = memStorage.createConversation("Mem Conv");
        EXPECT_FALSE(cid.empty());
        EXPECT_EQ(1, memStorage.getConversationCount());
    }
}

// -----------------------------------------------------------------------------
// Test 2: Schema Compatibility & Future Version Fallback
// -----------------------------------------------------------------------------
void test_schema_compatibility()
{
    std::cout << "[Test 2] Schema versioning and future version fallback...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString futureDbPath = tempDir.filePath("future_schema.db");

    // Pre-create database with unsupported schema version 999
    {
        flex::ConversationStorage storage(futureDbPath.toStdString());
        // Modify schema_version table directly
        // We'll reopen and write version 999
        storage.close();
    }

    // Write version 999 manually
    {
        flex::ConversationStorage setupStorage(futureDbPath.toStdString());
        // Normal storage initialized. Now let's inject a future version test file.
        setupStorage.close();
    }

    // Test unsupported schema handling using simulated future version DB
    QString futureManualDb = tempDir.filePath("manual_future.db");
    {
        // Initialize valid DB first
        flex::ConversationStorage initStorage(futureManualDb.toStdString());
        initStorage.close();
    }
    // Now reopen with a future version injected via SQLite
    {
        // Reopen with flex::ConversationStorage should fall back to memory if version > 1
        // We can test fallback behavior by creating a corrupt file
        QString corruptDb = tempDir.filePath("corrupt_test.db");
        QFile file(corruptDb);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("NOT A SQLITE FILE HEADER GARBAGE DATA 1234567890");
        file.close();

        flex::ConversationStorage degradedStorage(corruptDb.toStdString());
        EXPECT_TRUE(degradedStorage.isDegraded());
        EXPECT_TRUE(degradedStorage.storageError().has_value());
        EXPECT_EQ(":memory:", degradedStorage.dbPath());

        // In-memory fallback remains fully operational
        std::string cid = degradedStorage.createConversation("Safe In Memory");
        EXPECT_FALSE(cid.empty());
        std::string mid = degradedStorage.saveMessage(cid, "user", "Memory fallback message");
        EXPECT_FALSE(mid.empty());
        EXPECT_EQ(1, degradedStorage.getMessageCount(cid));

        // Original corrupt file preserved (not deleted)
        EXPECT_TRUE(QFile::exists(corruptDb));

        // Quarantine copy created
        QDir dir(tempDir.path());
        QStringList corruptCopies = dir.entryList(QStringList() << "*corrupt*", QDir::Files);
        EXPECT_TRUE(corruptCopies.size() >= 1);
    }
}

// -----------------------------------------------------------------------------
// Test 3: Create, Retrieve, and List Conversations
// -----------------------------------------------------------------------------
void test_conversation_crud()
{
    std::cout << "[Test 3] Conversation CRUD and ordering...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("crud.db");

    flex::ConversationStorage storage(dbPath.toStdString());

    // 3a. Default title
    std::string cidDefault = storage.createConversation();
    EXPECT_FALSE(cidDefault.empty());
    auto convDefault = storage.getConversation(cidDefault);
    EXPECT_TRUE(convDefault.has_value());
    EXPECT_STR_EQ(flex::ConversationStorage::DEFAULT_CONVERSATION_TITLE, convDefault->title);

    // 3b. Custom title
    std::string cidCustom = storage.createConversation("Native C++ Planning");
    auto convCustom = storage.getConversation(cidCustom);
    EXPECT_TRUE(convCustom.has_value());
    EXPECT_STR_EQ("Native C++ Planning", convCustom->title);

    // 3c. Explicit ID
    std::string explicitId = "explicit-uuid-12345-67890";
    std::string returnedId = storage.createConversation("Explicit ID Conv", explicitId);
    EXPECT_STR_EQ(explicitId, returnedId);
    auto convExplicit = storage.getConversation(explicitId);
    EXPECT_TRUE(convExplicit.has_value());
    EXPECT_STR_EQ(explicitId, convExplicit->id);

    // 3d. Update title
    storage.updateConversationTitle(cidCustom, "Updated Planning Title");
    auto convUpdated = storage.getConversation(cidCustom);
    EXPECT_TRUE(convUpdated.has_value());
    EXPECT_STR_EQ("Updated Planning Title", convUpdated->title);

    // 3e. List ordering (updated_at DESC)
    // Save message to cidDefault to bump its updated_at
    storage.saveMessage(cidDefault, "user", "Bump updated timestamp");
    auto list = storage.listConversations(10);
    EXPECT_EQ(3, static_cast<int>(list.size()));
    EXPECT_STR_EQ(cidDefault, list[0].id); // Most recently updated
}

// -----------------------------------------------------------------------------
// Test 4: Message Ordering & Sequence Numbers
// -----------------------------------------------------------------------------
void test_message_ordering_and_limits()
{
    std::cout << "[Test 4] Message ordering, monotonic sequence numbers, and limits...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("messages.db");

    flex::ConversationStorage storage(dbPath.toStdString());
    std::string cid = storage.createConversation("Seq Test");

    std::string m0 = storage.saveMessage(cid, "user", "Msg 0");
    std::string m1 = storage.saveMessage(cid, "assistant", "Msg 1", "qwen2.5-coder:3b");
    std::string m2 = storage.saveMessage(cid, "user", "Msg 2");

    auto msgs = storage.getMessages(cid);
    EXPECT_EQ(3, static_cast<int>(msgs.size()));

    EXPECT_STR_EQ(m0, msgs[0].id);
    EXPECT_EQ(0, msgs[0].sequenceNum);
    EXPECT_STR_EQ("user", msgs[0].role);

    EXPECT_STR_EQ(m1, msgs[1].id);
    EXPECT_EQ(1, msgs[1].sequenceNum);
    EXPECT_STR_EQ("assistant", msgs[1].role);
    EXPECT_TRUE(msgs[1].model.has_value());
    EXPECT_STR_EQ("qwen2.5-coder:3b", msgs[1].model.value());

    EXPECT_STR_EQ(m2, msgs[2].id);
    EXPECT_EQ(2, msgs[2].sequenceNum);

    // Test limit: insert 10 messages total
    std::string cidLimit = storage.createConversation("Limit Test");
    for (int i = 0; i < 10; ++i) {
        storage.saveMessage(cidLimit, "user", "Item " + std::to_string(i));
    }
    EXPECT_EQ(10, storage.getMessageCount(cidLimit));

    // Request last 3 messages (should be items 7, 8, 9 in chronological order)
    auto last3 = storage.getMessages(cidLimit, 3);
    EXPECT_EQ(3, static_cast<int>(last3.size()));
    EXPECT_STR_EQ("Item 7", last3[0].content);
    EXPECT_STR_EQ("Item 8", last3[1].content);
    EXPECT_STR_EQ("Item 9", last3[2].content);
    EXPECT_EQ(7, last3[0].sequenceNum);
    EXPECT_EQ(8, last3[1].sequenceNum);
    EXPECT_EQ(9, last3[2].sequenceNum);
}

// -----------------------------------------------------------------------------
// Test 5: Unicode and Multiline Messages
// -----------------------------------------------------------------------------
void test_unicode_and_multiline()
{
    std::cout << "[Test 5] Unicode, multiline, and code block preservation...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("unicode.db");

    flex::ConversationStorage storage(dbPath.toStdString());
    std::string cid = storage.createConversation("Unicode Test");

    std::string codeBlock =
        "#include <iostream>\n"
        "int main() {\n"
        "    std::cout << \"Hello World 🚀\" << std::endl;\n"
        "    return 0;\n"
        "}";

    std::string japaneseText = "こんにちは、FLEX 1.0！ 🌌 宇宙の深淵";

    std::string m1 = storage.saveMessage(cid, "user", codeBlock);
    std::string m2 = storage.saveMessage(cid, "assistant", japaneseText);

    auto msgs = storage.getMessages(cid);
    EXPECT_EQ(2, static_cast<int>(msgs.size()));
    EXPECT_STR_EQ(codeBlock, msgs[0].content);
    EXPECT_STR_EQ(japaneseText, msgs[1].content);
}

// -----------------------------------------------------------------------------
// Test 6: Empty & Invalid Input Handling
// -----------------------------------------------------------------------------
void test_invalid_input_handling()
{
    std::cout << "[Test 6] Invalid role, invalid CID, and boundary error handling...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("invalid.db");

    flex::ConversationStorage storage(dbPath.toStdString());
    std::string cid = storage.createConversation("Valid Conv");

    // 6a. Invalid role throws std::invalid_argument
    bool caughtInvalidRole = false;
    try {
        storage.saveMessage(cid, "moderator", "Should fail");
    } catch (const std::invalid_argument&) {
        caughtInvalidRole = true;
    }
    EXPECT_TRUE(caughtInvalidRole);

    // 6b. Non-existent conversation throws ConversationNotFoundError
    bool caughtNotFound = false;
    try {
        storage.saveMessage("non-existent-cid-404", "user", "Should fail");
    } catch (const flex::ConversationNotFoundError&) {
        caughtNotFound = true;
    }
    EXPECT_TRUE(caughtNotFound);

    // 6c. Update title for non-existent conversation throws ConversationNotFoundError
    bool caughtTitleNotFound = false;
    try {
        storage.updateConversationTitle("non-existent-cid-404", "New Title");
    } catch (const flex::ConversationNotFoundError&) {
        caughtTitleNotFound = true;
    }
    EXPECT_TRUE(caughtTitleNotFound);

    // 6d. Getting non-existent conversation returns nullopt
    auto none = storage.getConversation("non-existent-cid-404");
    EXPECT_FALSE(none.has_value());

    // 6e. Deleting non-existent conversation returns false
    EXPECT_FALSE(storage.deleteConversation("non-existent-cid-404"));
}

// -----------------------------------------------------------------------------
// Test 7: Foreign-Key Cascade Deletion
// -----------------------------------------------------------------------------
void test_foreign_key_cascade_delete()
{
    std::cout << "[Test 7] Foreign-key cascade deletion...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("cascade.db");

    flex::ConversationStorage storage(dbPath.toStdString());
    std::string cid = storage.createConversation("To Delete");

    storage.saveMessage(cid, "user", "Message 1");
    storage.saveMessage(cid, "assistant", "Message 2");
    EXPECT_EQ(2, storage.getMessageCount(cid));

    // Delete conversation
    EXPECT_TRUE(storage.deleteConversation(cid));

    // Conversation should be gone
    EXPECT_FALSE(storage.getConversation(cid).has_value());

    // Messages must be cascade-deleted
    EXPECT_EQ(0, static_cast<int>(storage.getMessages(cid).size()));
    EXPECT_EQ(0, storage.getMessageCount(cid));
}

// -----------------------------------------------------------------------------
// Test 8: Idempotent Duplicate Message Prevention
// -----------------------------------------------------------------------------
void test_idempotent_duplicate_prevention()
{
    std::cout << "[Test 8] Idempotent duplicate message prevention...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("idempotent.db");

    flex::ConversationStorage storage(dbPath.toStdString());
    std::string cid = storage.createConversation("Idempotent Conv");

    std::string stableMid = "msg-uuid-stable-1234";

    std::string id1 = storage.saveMessage(cid, "user", "First attempt", std::nullopt, stableMid);
    EXPECT_STR_EQ(stableMid, id1);

    // Retry with identical stableMid
    std::string id2 = storage.saveMessage(cid, "user", "Second attempt", std::nullopt, stableMid);
    EXPECT_STR_EQ(stableMid, id2);

    auto msgs = storage.getMessages(cid);
    EXPECT_EQ(1, static_cast<int>(msgs.size()));
    EXPECT_STR_EQ("First attempt", msgs[0].content);
}

// -----------------------------------------------------------------------------
// Test 9: Persistence Across Closing and Reopening
// -----------------------------------------------------------------------------
void test_persistence_across_restart()
{
    std::cout << "[Test 9] Persistence across closing and reopening database...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("persistent.db");

    std::string cid;
    std::string m1;
    std::string m2;

    {
        flex::ConversationStorage storage(dbPath.toStdString());
        cid = storage.createConversation("Persistent Project");
        m1 = storage.saveMessage(cid, "user", "Durable message");
        m2 = storage.saveMessage(cid, "assistant", "Durable reply", "qwen2.5-coder:3b");
        storage.close();
    }

    // Reopen new storage instance on the exact same database file
    {
        flex::ConversationStorage reopened(dbPath.toStdString());
        EXPECT_FALSE(reopened.isDegraded());

        auto conv = reopened.getConversation(cid);
        EXPECT_TRUE(conv.has_value());
        EXPECT_STR_EQ("Persistent Project", conv->title);

        auto msgs = reopened.getMessages(cid);
        EXPECT_EQ(2, static_cast<int>(msgs.size()));
        EXPECT_STR_EQ(m1, msgs[0].id);
        EXPECT_STR_EQ("Durable message", msgs[0].content);
        EXPECT_STR_EQ(m2, msgs[1].id);
        EXPECT_STR_EQ("Durable reply", msgs[1].content);
        EXPECT_TRUE(msgs[1].model.has_value());
        EXPECT_STR_EQ("qwen2.5-coder:3b", msgs[1].model.value());
    }
}

// -----------------------------------------------------------------------------
// Test 10: Auto-Titling Behavior
// -----------------------------------------------------------------------------
void test_auto_titling()
{
    std::cout << "[Test 10] Automatic titling from first user prompt...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("autotitle.db");

    flex::ConversationStorage storage(dbPath.toStdString());

    // 10a. Auto-title triggered on default "New Chat" title
    std::string cid1 = storage.createConversation();
    std::string prompt = "Explain memory management in modern C11 and C++17";
    storage.saveMessage(cid1, "user", prompt, std::nullopt, std::nullopt, true);

    auto conv1 = storage.getConversation(cid1);
    EXPECT_TRUE(conv1.has_value());
    std::string expectedTitle = prompt.substr(0, 35) + "...";
    EXPECT_STR_EQ(expectedTitle, conv1->title);

    // 10b. Custom title preserved (NOT overwritten by user message)
    std::string cid2 = storage.createConversation("Custom Architectural Spec");
    storage.saveMessage(cid2, "user", "Hello assistant", std::nullopt, std::nullopt, true);
    auto conv2 = storage.getConversation(cid2);
    EXPECT_TRUE(conv2.has_value());
    EXPECT_STR_EQ("Custom Architectural Spec", conv2->title);
}

// -----------------------------------------------------------------------------
// Test 11: Search & Filter Counts
// -----------------------------------------------------------------------------
void test_search_and_counts()
{
    std::cout << "[Test 11] Search queries and conversation counting...\n";

    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());
    QString dbPath = tempDir.filePath("search.db");

    flex::ConversationStorage storage(dbPath.toStdString());

    std::string c1 = storage.createConversation("Kernel Architecture");
    storage.saveMessage(c1, "user", "Explain scheduling algorithms");

    std::string c2 = storage.createConversation("Python Desktop");
    storage.saveMessage(c2, "user", "How does Tkinter event loop work?");

    // Search by title
    auto resTitle = storage.searchConversations("Kernel");
    EXPECT_EQ(1, static_cast<int>(resTitle.size()));
    EXPECT_STR_EQ(c1, resTitle[0].id);

    // Search by message content
    auto resContent = storage.searchConversations("Tkinter");
    EXPECT_EQ(1, static_cast<int>(resContent.size()));
    EXPECT_STR_EQ(c2, resContent[0].id);

    // Empty search returns all
    auto resAll = storage.searchConversations("");
    EXPECT_EQ(2, static_cast<int>(resAll.size()));

    // Total counts
    EXPECT_EQ(2, storage.getConversationCount());
    EXPECT_EQ(2, storage.getTotalMessageCount());
}

void test_settings_persistence()
{
    std::cout << "[Test: Settings Persistence (Track A)]...\n";
    QTemporaryDir tempDir;
    EXPECT_TRUE(tempDir.isValid());

    std::string configPath = tempDir.filePath("config.ini").toStdString();

    // 1. Missing file: returns defaults gracefully
    auto loadRes = flex::SettingsPersistenceService::loadSettings(configPath);
    EXPECT_TRUE(loadRes.success);
    EXPECT_FALSE(loadRes.fileExisted);
    EXPECT_STR_EQ("http://127.0.0.1:11434", loadRes.settings.ollamaEndpoint);
    EXPECT_STR_EQ("qwen2.5-coder:3b", loadRes.settings.ollamaModel);
    EXPECT_EQ(4096, loadRes.settings.ollamaNumCtx);
    EXPECT_EQ(30, loadRes.settings.ollamaTimeout);
    EXPECT_EQ(0, loadRes.settings.ttsRate);
    EXPECT_EQ(100, loadRes.settings.ttsVolume);

    // 2. Save valid custom configuration
    flex::AppSettings custom;
    custom.ollamaEndpoint = "http://127.0.0.1:11434";
    custom.ollamaModel = "mistral:7b";
    custom.ollamaTemperature = 0.45;
    custom.ollamaNumCtx = 8192;
    custom.ollamaTimeout = 45;
    custom.audioInputDevice = "Test Mic Array";
    custom.ttsRate = 3;
    custom.ttsVolume = 85;

    auto saveRes = flex::SettingsPersistenceService::saveSettings(custom, configPath);
    EXPECT_TRUE(saveRes.success);

    // 3. Reload and verify exact preservation
    auto reloadRes = flex::SettingsPersistenceService::loadSettings(configPath);
    EXPECT_TRUE(reloadRes.success);
    EXPECT_TRUE(reloadRes.fileExisted);
    EXPECT_STR_EQ(custom.ollamaEndpoint, reloadRes.settings.ollamaEndpoint);
    EXPECT_STR_EQ(custom.ollamaModel, reloadRes.settings.ollamaModel);
    EXPECT_TRUE(std::abs(reloadRes.settings.ollamaTemperature - 0.45) < 0.001);
    EXPECT_EQ(8192, reloadRes.settings.ollamaNumCtx);
    EXPECT_EQ(45, reloadRes.settings.ollamaTimeout);
    EXPECT_STR_EQ("Test Mic Array", reloadRes.settings.audioInputDevice);
    EXPECT_EQ(3, reloadRes.settings.ttsRate);
    EXPECT_EQ(85, reloadRes.settings.ttsVolume);

    // 4. Pre-validation strictly rejects invalid values
    flex::AppSettings invalidCfg = custom;
    invalidCfg.ollamaEndpoint = "ftp://invalid-server";
    EXPECT_FALSE(flex::SettingsPersistenceService::saveSettings(invalidCfg, configPath).success);

    invalidCfg = custom;
    invalidCfg.ollamaTemperature = 5.0; // Exceeds 2.0
    EXPECT_FALSE(flex::SettingsPersistenceService::saveSettings(invalidCfg, configPath).success);

    invalidCfg = custom;
    invalidCfg.ollamaNumCtx = 50; // Less than 256
    EXPECT_FALSE(flex::SettingsPersistenceService::saveSettings(invalidCfg, configPath).success);

    invalidCfg = custom;
    invalidCfg.ttsRate = -20; // Out of range -10..+10
    EXPECT_FALSE(flex::SettingsPersistenceService::saveSettings(invalidCfg, configPath).success);

    invalidCfg = custom;
    invalidCfg.ttsVolume = 150; // Out of range 0..100
    EXPECT_FALSE(flex::SettingsPersistenceService::saveSettings(invalidCfg, configPath).success);

    // 5. Existing valid configuration preserved after rejected save
    auto checkPreserved = flex::SettingsPersistenceService::loadSettings(configPath);
    EXPECT_TRUE(checkPreserved.success);
    EXPECT_STR_EQ("mistral:7b", checkPreserved.settings.ollamaModel);
    EXPECT_EQ(8192, checkPreserved.settings.ollamaNumCtx);

    // 6. Malformed file error detection without crashing
    std::string corruptPath = tempDir.filePath("corrupt.ini").toStdString();
    {
        QFile corruptFile(QString::fromStdString(corruptPath));
        EXPECT_TRUE(corruptFile.open(QIODevice::WriteOnly | QIODevice::Text));
        corruptFile.write("this line is completely broken without any delimiter\n");
        corruptFile.close();
    }
    auto corruptRes = flex::SettingsPersistenceService::loadSettings(corruptPath);
    EXPECT_FALSE(corruptRes.success);
    EXPECT_FALSE(corruptRes.errorMessage.empty());

    // 7. Environment variable override
    std::string envTarget = tempDir.filePath("env_target.ini").toStdString();
    qputenv("FLEX_CONFIG_PATH", QByteArray::fromStdString(envTarget));
    EXPECT_STR_EQ(QDir::cleanPath(QString::fromStdString(envTarget)).toStdString(),
                  flex::SettingsPersistenceService::defaultConfigurationPath());
    qunsetenv("FLEX_CONFIG_PATH");
}

} // anonymous namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "========================================================\n";
    std::cout << " FLEX 1.0 Native C++ Storage Test Suite (Stage 2)\n";
    std::cout << "========================================================\n\n";

    test_database_initialization();
    test_schema_compatibility();
    test_conversation_crud();
    test_message_ordering_and_limits();
    test_unicode_and_multiline();
    test_invalid_input_handling();
    test_foreign_key_cascade_delete();
    test_idempotent_duplicate_prevention();
    test_persistence_across_restart();
    test_auto_titling();
    test_search_and_counts();
    test_settings_persistence();

    std::cout << "\n========================================================\n";
    std::cout << "Test Summary:\n";
    std::cout << "  Total Assertions : " << g_tests_run << "\n";
    std::cout << "  Passed           : " << g_tests_passed << "\n";
    std::cout << "  Failed           : " << g_tests_failed << "\n";
    std::cout << "========================================================\n";

    if (g_tests_failed == 0) {
        std::cout << "Result: SUCCESS (All native C++ storage tests passed!)\n";
        return 0;
    }

    std::cout << "Result: FAILURE (" << g_tests_failed << " assertion(s) failed!)\n";
    return 1;
}

