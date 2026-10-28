#include "flex/core_session.hpp"

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
// Test 1: Initial State & Query Functions
// -----------------------------------------------------------------------------
void test_initial_state()
{
    std::cout << "[Test 1] Initial state and query methods...\n";
    flex::CoreSession session;

    EXPECT_TRUE(session.isIdle());
    EXPECT_FALSE(session.isProcessing());
    EXPECT_FALSE(session.isStopping());
    EXPECT_EQ(FLEX_LIFECYCLE_IDLE, session.lifecycle());
    EXPECT_EQ(FLEX_OPERATION_NONE, session.currentOperation());
    EXPECT_EQ(0ULL, session.currentOperationId());
    EXPECT_EQ(FLEX_STATE_OK, session.lastError());

    const auto& raw = session.rawState();
    EXPECT_EQ(FLEX_LIFECYCLE_IDLE, raw.lifecycle);
    EXPECT_EQ(FLEX_OPERATION_NONE, raw.current_operation);
    EXPECT_EQ(0ULL, raw.current_operation_id);
    EXPECT_EQ(FLEX_STATE_OK, raw.last_error);
}

// -----------------------------------------------------------------------------
// Test 2: Valid Dispatch (Chat, Voice, Memory, Exit)
// -----------------------------------------------------------------------------
void test_valid_dispatch()
{
    std::cout << "[Test 2] Valid dispatch commands...\n";

    // 2a. Chat with arguments
    {
        flex::CoreSession session;
        auto res = session.dispatch("chat Hello, Antigravity!");
        EXPECT_TRUE(res.isOk());
        EXPECT_EQ(FLEX_STATUS_OK, res.parserStatus());
        EXPECT_EQ(FLEX_STATE_OK, res.stateStatus());
        EXPECT_EQ(FLEX_COMMAND_CHAT, res.commandType());
        EXPECT_EQ(FLEX_OPERATION_CHAT, res.operation());
        EXPECT_TRUE(res.operationStarted());
        EXPECT_FALSE(res.shutdownRequested());
        EXPECT_EQ(1ULL, res.operationId());
        EXPECT_STR_EQ("chat", std::string(res.commandName()));
        EXPECT_STR_EQ("Hello, Antigravity!", std::string(res.commandArgs()));
        EXPECT_TRUE(res.hasArgs());

        EXPECT_TRUE(session.isProcessing());
        EXPECT_FALSE(session.isIdle());
        EXPECT_EQ(FLEX_OPERATION_CHAT, session.currentOperation());
        EXPECT_EQ(1ULL, session.currentOperationId());

        EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
        EXPECT_TRUE(session.isIdle());
        EXPECT_EQ(FLEX_OPERATION_NONE, session.currentOperation());
    }

    // 2b. Voice with arguments
    {
        flex::CoreSession session;
        auto res = session.dispatch("voice start listening");
        EXPECT_TRUE(res.isOk());
        EXPECT_EQ(FLEX_STATUS_OK, res.parserStatus());
        EXPECT_EQ(FLEX_COMMAND_VOICE, res.commandType());
        EXPECT_EQ(FLEX_OPERATION_VOICE, res.operation());
        EXPECT_TRUE(res.operationStarted());
        EXPECT_EQ(1ULL, res.operationId());
        EXPECT_STR_EQ("voice", std::string(res.commandName()));
        EXPECT_STR_EQ("start listening", std::string(res.commandArgs()));

        EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
        EXPECT_TRUE(session.isIdle());
    }

    // 2c. Memory with arguments
    {
        flex::CoreSession session;
        auto res = session.dispatch("memory recall session_history");
        EXPECT_TRUE(res.isOk());
        EXPECT_EQ(FLEX_STATUS_OK, res.parserStatus());
        EXPECT_EQ(FLEX_COMMAND_MEMORY, res.commandType());
        EXPECT_EQ(FLEX_OPERATION_MEMORY, res.operation());
        EXPECT_TRUE(res.operationStarted());
        EXPECT_EQ(1ULL, res.operationId());
        EXPECT_STR_EQ("memory", std::string(res.commandName()));
        EXPECT_STR_EQ("recall session_history", std::string(res.commandArgs()));

        EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
        EXPECT_TRUE(session.isIdle());
    }

    // 2d. Commands without arguments
    {
        flex::CoreSession session;
        auto res = session.dispatch("chat");
        EXPECT_TRUE(res.isOk());
        EXPECT_TRUE(res.operationStarted());
        EXPECT_FALSE(res.hasArgs());
        EXPECT_STR_EQ("", std::string(res.commandArgs()));
        EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
    }

    // 2e. Exit command
    {
        flex::CoreSession session;
        auto res = session.dispatch("exit");
        EXPECT_TRUE(res.isOk());
        EXPECT_EQ(FLEX_COMMAND_EXIT, res.commandType());
        EXPECT_EQ(FLEX_OPERATION_NONE, res.operation());
        EXPECT_FALSE(res.operationStarted());
        EXPECT_TRUE(res.shutdownRequested());
        EXPECT_TRUE(session.isStopping());
        EXPECT_FALSE(session.isIdle());
        EXPECT_FALSE(session.isProcessing());
    }
}

// -----------------------------------------------------------------------------
// Test 3: Rejected Dispatch (Parser level)
// -----------------------------------------------------------------------------
void test_rejected_dispatch_parser()
{
    std::cout << "[Test 3] Rejected dispatch (parser errors)...\n";
    flex::CoreSession session;

    // 3a. Unknown command
    auto res_unk = session.dispatch("teleport to moon");
    EXPECT_FALSE(res_unk.isOk());
    EXPECT_EQ(FLEX_STATUS_UNKNOWN_COMMAND, res_unk.parserStatus());
    EXPECT_EQ(FLEX_STATE_OK, res_unk.stateStatus());
    EXPECT_FALSE(res_unk.operationStarted());
    EXPECT_TRUE(session.isIdle());

    // 3b. Empty string
    auto res_empty = session.dispatch("");
    EXPECT_FALSE(res_empty.isOk());
    EXPECT_EQ(FLEX_STATUS_EMPTY_INPUT, res_empty.parserStatus());
    EXPECT_FALSE(res_empty.operationStarted());
    EXPECT_TRUE(session.isIdle());

    // 3c. Whitespace only
    auto res_ws = session.dispatch("   \t  \n  ");
    EXPECT_FALSE(res_ws.isOk());
    EXPECT_EQ(FLEX_STATUS_EMPTY_INPUT, res_ws.parserStatus());
    EXPECT_FALSE(res_ws.operationStarted());
    EXPECT_TRUE(session.isIdle());

    // 3d. Null pointer
    auto res_null = session.dispatch(static_cast<const char*>(nullptr));
    EXPECT_FALSE(res_null.isOk());
    EXPECT_EQ(FLEX_STATUS_INVALID_INPUT, res_null.parserStatus());
    EXPECT_FALSE(res_null.operationStarted());
    EXPECT_TRUE(session.isIdle());

    // 3e. Oversized input (> 1024 chars)
    std::string huge(1025, 'x');
    auto res_huge = session.dispatch(huge);
    EXPECT_FALSE(res_huge.isOk());
    EXPECT_EQ(FLEX_STATUS_INPUT_TOO_LONG, res_huge.parserStatus());
    EXPECT_FALSE(res_huge.operationStarted());
    EXPECT_TRUE(session.isIdle());
}

// -----------------------------------------------------------------------------
// Test 4: Rejected Dispatch (State level: Already Processing & Stopping)
// -----------------------------------------------------------------------------
void test_rejected_dispatch_state()
{
    std::cout << "[Test 4] Rejected dispatch (state machine constraints)...\n";

    // 4a. Already processing
    {
        flex::CoreSession session;
        auto res1 = session.dispatch("chat active task");
        EXPECT_TRUE(res1.isOk());
        EXPECT_TRUE(res1.operationStarted());
        EXPECT_TRUE(session.isProcessing());

        // Second operational command while busy must be rejected
        auto res2 = session.dispatch("voice new task");
        EXPECT_FALSE(res2.isOk());
        EXPECT_EQ(FLEX_STATUS_OK, res2.parserStatus());
        EXPECT_EQ(FLEX_STATE_ERROR_ALREADY_PROCESSING, res2.stateStatus());
        EXPECT_FALSE(res2.operationStarted());
        EXPECT_EQ(0ULL, res2.operationId());

        // Session must remain in original operation
        EXPECT_TRUE(session.isProcessing());
        EXPECT_EQ(FLEX_OPERATION_CHAT, session.currentOperation());
        EXPECT_EQ(1ULL, session.currentOperationId());

        // Finish original operation
        EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
        EXPECT_TRUE(session.isIdle());
    }

    // 4b. Dispatch while stopping
    {
        flex::CoreSession session;
        EXPECT_EQ(FLEX_STATE_OK, session.requestShutdown());
        EXPECT_TRUE(session.isStopping());

        auto res = session.dispatch("chat message after shutdown");
        EXPECT_FALSE(res.isOk());
        EXPECT_EQ(FLEX_STATUS_OK, res.parserStatus());
        EXPECT_EQ(FLEX_STATE_ERROR_STOPPING, res.stateStatus());
        EXPECT_FALSE(res.operationStarted());
        EXPECT_TRUE(session.isStopping());
    }
}

// -----------------------------------------------------------------------------
// Test 5: Operation Completion and Repeated Completion
// -----------------------------------------------------------------------------
void test_operation_completion_and_repeated_completion()
{
    std::cout << "[Test 5] Operation completion and repeated completion...\n";
    flex::CoreSession session;

    // 5a. Finish when IDLE -> FLEX_STATE_ERROR_NOT_PROCESSING
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, session.finishOperation());
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, session.lastError());

    // Reset error
    EXPECT_EQ(FLEX_STATE_OK, session.resetError());
    EXPECT_EQ(FLEX_STATE_OK, session.lastError());

    // 5b. Start operation, finish once (succeeds), finish second & third time (fails)
    auto res = session.dispatch("chat test completion");
    EXPECT_TRUE(res.operationStarted());
    EXPECT_TRUE(session.isProcessing());

    // First completion -> success
    EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
    EXPECT_TRUE(session.isIdle());
    EXPECT_EQ(FLEX_OPERATION_NONE, session.currentOperation());
    EXPECT_EQ(FLEX_STATE_OK, session.lastError());

    // Repeated completion 1 -> failure
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, session.finishOperation());
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, session.lastError());

    // Repeated completion 2 -> failure
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, session.finishOperation());
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, session.lastError());
}

// -----------------------------------------------------------------------------
// Test 6: Direct Core API & Error Paths
// -----------------------------------------------------------------------------
void test_direct_api_and_error_paths()
{
    std::cout << "[Test 6] Direct API and error paths...\n";
    flex::CoreSession session;

    // 6a. Invalid operation enums
    EXPECT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, session.beginOperation(FLEX_OPERATION_NONE));
    EXPECT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, session.beginOperation(static_cast<FlexOperation>(999)));

    // 6b. Normal manual begin/finish cycle
    EXPECT_EQ(FLEX_STATE_OK, session.beginOperation(FLEX_OPERATION_CHAT));
    EXPECT_TRUE(session.isProcessing());
    EXPECT_EQ(1ULL, session.currentOperationId());

    // Attempt to begin second operation manually
    EXPECT_EQ(FLEX_STATE_ERROR_ALREADY_PROCESSING, session.beginOperation(FLEX_OPERATION_VOICE));
    EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
    EXPECT_TRUE(session.isIdle());

    // 6c. Monotonic ID incrementation across operations
    EXPECT_EQ(FLEX_STATE_OK, session.beginOperation(FLEX_OPERATION_VOICE));
    EXPECT_EQ(2ULL, session.currentOperationId());
    EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());

    EXPECT_EQ(FLEX_STATE_OK, session.beginOperation(FLEX_OPERATION_MEMORY));
    EXPECT_EQ(3ULL, session.currentOperationId());
    EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());

    // 6d. Shutdown requested during active operation
    EXPECT_EQ(FLEX_STATE_OK, session.beginOperation(FLEX_OPERATION_CHAT));
    EXPECT_TRUE(session.isProcessing());

    // Request shutdown while processing
    EXPECT_EQ(FLEX_STATE_OK, session.requestShutdown());
    EXPECT_TRUE(session.isStopping());
    EXPECT_EQ(FLEX_OPERATION_CHAT, session.currentOperation());

    // Finish active operation while stopping
    EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
    // Should remain in STOPPING state
    EXPECT_TRUE(session.isStopping());
    EXPECT_EQ(FLEX_OPERATION_NONE, session.currentOperation());

    // Repeated finish while STOPPING
    EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, session.finishOperation());

    // Begin operation while STOPPING
    EXPECT_EQ(FLEX_STATE_ERROR_STOPPING, session.beginOperation(FLEX_OPERATION_CHAT));
}

// -----------------------------------------------------------------------------
// Test 7: RAII ScopedOperation Lifecycles
// -----------------------------------------------------------------------------
void test_raii_scoped_operation()
{
    std::cout << "[Test 7] RAII ScopedOperation lifecycles...\n";

    // 7a. Normal RAII automatic completion upon scope exit
    {
        flex::CoreSession session;
        {
            auto op = session.scopedOperation(FLEX_OPERATION_CHAT);
            EXPECT_TRUE(op.isActive());
            EXPECT_EQ(1ULL, op.operationId());
            EXPECT_EQ(FLEX_STATE_OK, op.status());
            EXPECT_TRUE(session.isProcessing());
        }
        // Exiting scope automatically calls finishOperation()
        EXPECT_TRUE(session.isIdle());
        EXPECT_EQ(FLEX_OPERATION_NONE, session.currentOperation());
    }

    // 7b. Explicit finish and repeated finish prevention
    {
        flex::CoreSession session;
        auto op = session.scopedOperation(FLEX_OPERATION_VOICE);
        EXPECT_TRUE(op.isActive());
        EXPECT_TRUE(session.isProcessing());

        // Explicit finish
        EXPECT_EQ(FLEX_STATE_OK, op.finish());
        EXPECT_FALSE(op.isActive());
        EXPECT_TRUE(session.isIdle());

        // Repeated finish on same guard returns error
        EXPECT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, op.finish());
    }

    // 7c. Dismiss guard (manual completion)
    {
        flex::CoreSession session;
        {
            auto op = session.scopedOperation(FLEX_OPERATION_MEMORY);
            EXPECT_TRUE(op.isActive());
            op.dismiss();
            EXPECT_FALSE(op.isActive());
        }
        // Guard dismissed; session remains in processing until manually finished
        EXPECT_TRUE(session.isProcessing());
        EXPECT_EQ(FLEX_STATE_OK, session.finishOperation());
        EXPECT_TRUE(session.isIdle());
    }

    // 7d. Move semantics for ScopedOperation
    {
        flex::CoreSession session;
        flex::ScopedOperation op2;
        {
            auto op1 = session.scopedOperation(FLEX_OPERATION_CHAT);
            EXPECT_TRUE(op1.isActive());
            op2 = std::move(op1);
            EXPECT_FALSE(op1.isActive());
            EXPECT_TRUE(op2.isActive());
        }
        // op1 destroyed, op2 still holds the operation
        EXPECT_TRUE(session.isProcessing());
        EXPECT_EQ(FLEX_STATE_OK, op2.finish());
        EXPECT_TRUE(session.isIdle());
    }

    // 7e. dispatchScoped helper (valid command)
    {
        flex::CoreSession session;
        {
            auto [res, op] = session.dispatchScoped("chat scoped auto finish");
            EXPECT_TRUE(res.isOk());
            EXPECT_TRUE(res.operationStarted());
            EXPECT_TRUE(op.isActive());
            EXPECT_TRUE(session.isProcessing());
        }
        // Exiting scope finishes the operation
        EXPECT_TRUE(session.isIdle());
    }

    // 7f. dispatchScoped helper (rejected command)
    {
        flex::CoreSession session;
        auto [res, op] = session.dispatchScoped("invalid command");
        EXPECT_FALSE(res.isOk());
        EXPECT_FALSE(res.operationStarted());
        EXPECT_FALSE(op.isActive());
        EXPECT_TRUE(session.isIdle());
    }
}

// -----------------------------------------------------------------------------
// Test 8: CoreSession Move Semantics
// -----------------------------------------------------------------------------
void test_session_move_semantics()
{
    std::cout << "[Test 8] CoreSession move semantics...\n";

    flex::CoreSession session1;
    auto res = session1.dispatch("chat moving session");
    EXPECT_TRUE(res.operationStarted());
    EXPECT_TRUE(session1.isProcessing());
    EXPECT_EQ(1ULL, session1.currentOperationId());

    // Move construct session2 from session1
    flex::CoreSession session2(std::move(session1));
    EXPECT_TRUE(session2.isProcessing());
    EXPECT_EQ(1ULL, session2.currentOperationId());
    EXPECT_EQ(FLEX_OPERATION_CHAT, session2.currentOperation());

    // session1 should be re-initialized to IDLE
    EXPECT_TRUE(session1.isIdle());
    EXPECT_EQ(0ULL, session1.currentOperationId());

    // Complete on session2
    EXPECT_EQ(FLEX_STATE_OK, session2.finishOperation());
    EXPECT_TRUE(session2.isIdle());

    // Move assignment
    session1 = std::move(session2);
    EXPECT_TRUE(session1.isIdle());
}

// -----------------------------------------------------------------------------
// Test 9: Helper String Conversions and Mappings
// -----------------------------------------------------------------------------
void test_helper_conversions()
{
    std::cout << "[Test 9] Enum string conversions and mappings...\n";

    // Lifecycle
    EXPECT_STR_EQ("FLEX_LIFECYCLE_IDLE", flex::CoreSession::lifecycleToString(FLEX_LIFECYCLE_IDLE));
    EXPECT_STR_EQ("FLEX_LIFECYCLE_PROCESSING", flex::CoreSession::lifecycleToString(FLEX_LIFECYCLE_PROCESSING));
    EXPECT_STR_EQ("FLEX_LIFECYCLE_STOPPING", flex::CoreSession::lifecycleToString(FLEX_LIFECYCLE_STOPPING));
    EXPECT_STR_EQ("INVALID_LIFECYCLE", flex::CoreSession::lifecycleToString(static_cast<FlexLifecycleState>(999)));

    // Operation
    EXPECT_STR_EQ("FLEX_OPERATION_NONE", flex::CoreSession::operationToString(FLEX_OPERATION_NONE));
    EXPECT_STR_EQ("FLEX_OPERATION_CHAT", flex::CoreSession::operationToString(FLEX_OPERATION_CHAT));
    EXPECT_STR_EQ("FLEX_OPERATION_VOICE", flex::CoreSession::operationToString(FLEX_OPERATION_VOICE));
    EXPECT_STR_EQ("FLEX_OPERATION_MEMORY", flex::CoreSession::operationToString(FLEX_OPERATION_MEMORY));
    EXPECT_STR_EQ("INVALID_OPERATION", flex::CoreSession::operationToString(static_cast<FlexOperation>(999)));

    // State Status
    EXPECT_STR_EQ("FLEX_STATE_OK", flex::CoreSession::stateStatusToString(FLEX_STATE_OK));
    EXPECT_STR_EQ("FLEX_STATE_ERROR_INVALID_ARGUMENT", flex::CoreSession::stateStatusToString(FLEX_STATE_ERROR_INVALID_ARGUMENT));
    EXPECT_STR_EQ("FLEX_STATE_ERROR_ALREADY_PROCESSING", flex::CoreSession::stateStatusToString(FLEX_STATE_ERROR_ALREADY_PROCESSING));
    EXPECT_STR_EQ("FLEX_STATE_ERROR_NOT_PROCESSING", flex::CoreSession::stateStatusToString(FLEX_STATE_ERROR_NOT_PROCESSING));
    EXPECT_STR_EQ("FLEX_STATE_ERROR_STOPPING", flex::CoreSession::stateStatusToString(FLEX_STATE_ERROR_STOPPING));
    EXPECT_STR_EQ("INVALID_STATUS", flex::CoreSession::stateStatusToString(static_cast<FlexStateStatus>(999)));

    // Command Type
    EXPECT_STR_EQ("FLEX_COMMAND_CHAT", flex::CoreSession::commandTypeToString(FLEX_COMMAND_CHAT));
    EXPECT_STR_EQ("FLEX_COMMAND_VOICE", flex::CoreSession::commandTypeToString(FLEX_COMMAND_VOICE));
    EXPECT_STR_EQ("FLEX_COMMAND_MEMORY", flex::CoreSession::commandTypeToString(FLEX_COMMAND_MEMORY));
    EXPECT_STR_EQ("FLEX_COMMAND_EXIT", flex::CoreSession::commandTypeToString(FLEX_COMMAND_EXIT));
    EXPECT_STR_EQ("FLEX_COMMAND_UNKNOWN", flex::CoreSession::commandTypeToString(FLEX_COMMAND_UNKNOWN));

    // Command Status
    EXPECT_STR_EQ("FLEX_STATUS_OK", flex::CoreSession::commandStatusToString(FLEX_STATUS_OK));
    EXPECT_STR_EQ("FLEX_STATUS_UNKNOWN_COMMAND", flex::CoreSession::commandStatusToString(FLEX_STATUS_UNKNOWN_COMMAND));
    EXPECT_STR_EQ("FLEX_STATUS_EMPTY_INPUT", flex::CoreSession::commandStatusToString(FLEX_STATUS_EMPTY_INPUT));
    EXPECT_STR_EQ("FLEX_STATUS_INVALID_INPUT", flex::CoreSession::commandStatusToString(FLEX_STATUS_INVALID_INPUT));
    EXPECT_STR_EQ("FLEX_STATUS_INPUT_TOO_LONG", flex::CoreSession::commandStatusToString(FLEX_STATUS_INPUT_TOO_LONG));

    // Mapping
    EXPECT_EQ(FLEX_OPERATION_CHAT, flex::CoreSession::operationFromCommand(FLEX_COMMAND_CHAT));
    EXPECT_EQ(FLEX_OPERATION_VOICE, flex::CoreSession::operationFromCommand(FLEX_COMMAND_VOICE));
    EXPECT_EQ(FLEX_OPERATION_MEMORY, flex::CoreSession::operationFromCommand(FLEX_COMMAND_MEMORY));
    EXPECT_EQ(FLEX_OPERATION_NONE, flex::CoreSession::operationFromCommand(FLEX_COMMAND_EXIT));
    EXPECT_EQ(FLEX_OPERATION_NONE, flex::CoreSession::operationFromCommand(FLEX_COMMAND_UNKNOWN));
}

} // anonymous namespace

int main()
{
    std::cout << "========================================================\n";
    std::cout << " FLEX 1.0 Native C++ Wrapper Test Suite (Stage 1)\n";
    std::cout << "========================================================\n\n";

    test_initial_state();
    test_valid_dispatch();
    test_rejected_dispatch_parser();
    test_rejected_dispatch_state();
    test_operation_completion_and_repeated_completion();
    test_direct_api_and_error_paths();
    test_raii_scoped_operation();
    test_session_move_semantics();
    test_helper_conversions();

    std::cout << "\n========================================================\n";
    std::cout << "Test Summary:\n";
    std::cout << "  Total Assertions : " << g_tests_run << "\n";
    std::cout << "  Passed           : " << g_tests_passed << "\n";
    std::cout << "  Failed           : " << g_tests_failed << "\n";
    std::cout << "========================================================\n";

    if (g_tests_failed == 0) {
        std::cout << "Result: SUCCESS (All native C++ wrapper tests passed!)\n";
        return 0;
    }

    std::cout << "Result: FAILURE (" << g_tests_failed << " test assertion(s) failed!)\n";
    return 1;
}

