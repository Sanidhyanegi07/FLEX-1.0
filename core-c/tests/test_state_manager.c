#include "test_state_manager.h"
#include "state_manager.h"

#include <stdio.h>
#include <string.h>

static int *s_run = NULL;
static int *s_passed = NULL;
static int *s_failed = NULL;

#define ASSERT_INT_EQ(expected, actual, msg) do { \
    (*s_run)++; \
    int _exp = (int)(expected); \
    int _act = (int)(actual); \
    if (_exp == _act) { \
        (*s_passed)++; \
    } else { \
        (*s_failed)++; \
        fprintf(stderr, "FAIL: line %d: %s (expected %d, got %d)\n", \
                __LINE__, (msg), _exp, _act); \
    } \
} while (0)

#define ASSERT_U64_EQ(expected, actual, msg) do { \
    (*s_run)++; \
    uint64_t _exp = (uint64_t)(expected); \
    uint64_t _act = (uint64_t)(actual); \
    if (_exp == _act) { \
        (*s_passed)++; \
    } else { \
        (*s_failed)++; \
        fprintf(stderr, "FAIL: line %d: %s (expected %llu, got %llu)\n", \
                __LINE__, (msg), (unsigned long long)_exp, (unsigned long long)_act); \
    } \
} while (0)

#define ASSERT_STR_EQ(expected, actual, msg) do { \
    (*s_run)++; \
    const char *_exp = (expected); \
    const char *_act = (actual); \
    if (_exp != NULL && _act != NULL && strcmp(_exp, _act) == 0) { \
        (*s_passed)++; \
    } else { \
        (*s_failed)++; \
        fprintf(stderr, "FAIL: line %d: %s (expected '%s', got '%s')\n", \
                __LINE__, (msg), _exp ? _exp : "(null)", _act ? _act : "(null)"); \
    } \
} while (0)

static void test_state_initialization(void)
{
    FlexStateManager state;
    memset(&state, 0xFF, sizeof(state));

    FlexStateStatus status = flex_state_init(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, status, "init status");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "init lifecycle IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "init op NONE");
    ASSERT_U64_EQ(0, state.current_operation_id, "init op id 0");
    ASSERT_INT_EQ(FLEX_STATE_OK, state.last_error, "init last_error OK");

    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "is_idle true");
    ASSERT_INT_EQ(0, flex_state_is_processing(&state) ? 1 : 0, "is_processing false");
    ASSERT_INT_EQ(0, flex_state_is_stopping(&state) ? 1 : 0, "is_stopping false");
}

static void test_state_begin_and_finish_each_operation(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* 1. CHAT */
    FlexStateStatus s1 = flex_state_begin_operation(&state, FLEX_OPERATION_CHAT);
    ASSERT_INT_EQ(FLEX_STATE_OK, s1, "begin CHAT status");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "lifecycle PROCESSING");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "op CHAT");
    ASSERT_U64_EQ(1, state.current_operation_id, "op id 1");
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "is_processing true");

    FlexStateStatus f1 = flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f1, "finish CHAT status");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "lifecycle IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op NONE");
    ASSERT_U64_EQ(1, state.current_operation_id, "op id preserved after finish");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "is_idle true");

    /* 2. VOICE */
    FlexStateStatus s2 = flex_state_begin_operation(&state, FLEX_OPERATION_VOICE);
    ASSERT_INT_EQ(FLEX_STATE_OK, s2, "begin VOICE status");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "lifecycle PROCESSING");
    ASSERT_INT_EQ(FLEX_OPERATION_VOICE, state.current_operation, "op VOICE");
    ASSERT_U64_EQ(2, state.current_operation_id, "op id 2");

    FlexStateStatus f2 = flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f2, "finish VOICE status");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "lifecycle IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op NONE");
    ASSERT_U64_EQ(2, state.current_operation_id, "op id preserved after finish");

    /* 3. MEMORY */
    FlexStateStatus s3 = flex_state_begin_operation(&state, FLEX_OPERATION_MEMORY);
    ASSERT_INT_EQ(FLEX_STATE_OK, s3, "begin MEMORY status");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "lifecycle PROCESSING");
    ASSERT_INT_EQ(FLEX_OPERATION_MEMORY, state.current_operation, "op MEMORY");
    ASSERT_U64_EQ(3, state.current_operation_id, "op id 3");

    FlexStateStatus f3 = flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f3, "finish MEMORY status");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "lifecycle IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op NONE");
    ASSERT_U64_EQ(3, state.current_operation_id, "op id preserved after finish");
}

static void test_state_invalid_operations(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Begin with FLEX_OPERATION_NONE */
    FlexStateStatus s1 = flex_state_begin_operation(&state, FLEX_OPERATION_NONE);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, s1, "reject NONE op");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, state.last_error, "last_error recorded");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "state remains IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op remains NONE");
    ASSERT_U64_EQ(0, state.current_operation_id, "id remains 0");

    /* Begin with invalid enum value */
    FlexStateStatus s2 = flex_state_begin_operation(&state, (FlexOperation)999);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, s2, "reject invalid enum op");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "state remains IDLE");
    ASSERT_U64_EQ(0, state.current_operation_id, "id remains 0");

    /* Begin while already processing */
    flex_state_begin_operation(&state, FLEX_OPERATION_CHAT);
    FlexStateStatus s3 = flex_state_begin_operation(&state, FLEX_OPERATION_VOICE);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_ALREADY_PROCESSING, s3, "reject op while processing");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_ALREADY_PROCESSING, state.last_error, "last_error recorded");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "state remains PROCESSING");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "op remains CHAT");
    ASSERT_U64_EQ(1, state.current_operation_id, "id remains 1");

    flex_state_finish_operation(&state);
}

static void test_state_finish_when_not_active(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Finishing when IDLE */
    FlexStateStatus f1 = flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, f1, "finish when idle fails");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, state.last_error, "last_error recorded");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "state remains IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op remains NONE");
    ASSERT_U64_EQ(0, state.current_operation_id, "id remains 0");
}

static void test_state_shutdown_behavior(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Shutdown from IDLE */
    FlexStateStatus sd1 = flex_state_request_shutdown(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, sd1, "shutdown from IDLE OK");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_STOPPING, state.lifecycle, "lifecycle is STOPPING");
    ASSERT_INT_EQ(1, flex_state_is_stopping(&state) ? 1 : 0, "is_stopping true");
    ASSERT_INT_EQ(0, flex_state_is_idle(&state) ? 1 : 0, "is_idle false");

    /* Begin operation rejected when STOPPING */
    FlexStateStatus b1 = flex_state_begin_operation(&state, FLEX_OPERATION_CHAT);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_STOPPING, b1, "begin op rejected when STOPPING");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_STOPPING, state.last_error, "last_error STOPPING");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_STOPPING, state.lifecycle, "lifecycle remains STOPPING");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op remains NONE");

    /* Redundant shutdown request is idempotent */
    FlexStateStatus sd2 = flex_state_request_shutdown(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, sd2, "redundant shutdown request OK");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_STOPPING, state.lifecycle, "lifecycle remains STOPPING");

    /* Shutdown requested while an operation is actively PROCESSING */
    flex_state_init(&state);
    flex_state_begin_operation(&state, FLEX_OPERATION_MEMORY);
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "is processing");

    FlexStateStatus sd3 = flex_state_request_shutdown(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, sd3, "shutdown during processing OK");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_STOPPING, state.lifecycle, "lifecycle transitions to STOPPING");
    ASSERT_INT_EQ(FLEX_OPERATION_MEMORY, state.current_operation, "operation still active until finished");

    /* Cannot begin another operation while in graceful stop */
    FlexStateStatus b2 = flex_state_begin_operation(&state, FLEX_OPERATION_VOICE);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_STOPPING, b2, "cannot begin new op while STOPPING");

    /* Finish the active operation -> operation becomes NONE, lifecycle remains STOPPING */
    FlexStateStatus f1 = flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f1, "finish active op during shutdown OK");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_STOPPING, state.lifecycle, "lifecycle stays STOPPING");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "operation becomes NONE");
}

static void test_state_repeated_operations_and_id_increments(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    FlexOperation ops[] = {
        FLEX_OPERATION_CHAT,
        FLEX_OPERATION_VOICE,
        FLEX_OPERATION_MEMORY
    };

    for (uint64_t i = 1; i <= 12; i++) {
        FlexOperation expected_op = ops[(i - 1) % 3];

        FlexStateStatus b_status = flex_state_begin_operation(&state, expected_op);
        ASSERT_INT_EQ(FLEX_STATE_OK, b_status, "repeated begin status OK");
        ASSERT_U64_EQ(i, state.current_operation_id, "op id matches iteration count");
        ASSERT_INT_EQ(expected_op, state.current_operation, "active op matches");

        FlexStateStatus f_status = flex_state_finish_operation(&state);
        ASSERT_INT_EQ(FLEX_STATE_OK, f_status, "repeated finish status OK");
        ASSERT_U64_EQ(i, state.current_operation_id, "op id unchanged on finish");
        ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op cleared");
    }
}

static void test_state_integrity_after_rejected_transitions(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Begin operation 1 */
    flex_state_begin_operation(&state, FLEX_OPERATION_CHAT);
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "state PROCESSING");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "op CHAT");
    ASSERT_U64_EQ(1, state.current_operation_id, "id 1");

    /* Attempt invalid begin -> rejected, state untouched */
    flex_state_begin_operation(&state, FLEX_OPERATION_VOICE);
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "state still PROCESSING");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "op still CHAT");
    ASSERT_U64_EQ(1, state.current_operation_id, "id still 1");

    /* Attempt invalid begin with invalid enum -> rejected, state untouched */
    flex_state_begin_operation(&state, (FlexOperation)88);
    ASSERT_INT_EQ(FLEX_LIFECYCLE_PROCESSING, state.lifecycle, "state still PROCESSING");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "op still CHAT");
    ASSERT_U64_EQ(1, state.current_operation_id, "id still 1");

    /* Finish validly */
    flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "state IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op NONE");
    ASSERT_U64_EQ(1, state.current_operation_id, "id 1");

    /* Attempt invalid finish -> rejected, state untouched */
    flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "state still IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op still NONE");
    ASSERT_U64_EQ(1, state.current_operation_id, "id still 1");
}

static void test_state_null_pointer_handling(void)
{
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, flex_state_init(NULL), "init(NULL)");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, flex_state_begin_operation(NULL, FLEX_OPERATION_CHAT), "begin(NULL)");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, flex_state_finish_operation(NULL), "finish(NULL)");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, flex_state_request_shutdown(NULL), "shutdown(NULL)");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, flex_state_reset_error(NULL), "reset_error(NULL)");

    ASSERT_INT_EQ(0, flex_state_is_idle(NULL) ? 1 : 0, "is_idle(NULL) false");
    ASSERT_INT_EQ(0, flex_state_is_processing(NULL) ? 1 : 0, "is_processing(NULL) false");
    ASSERT_INT_EQ(0, flex_state_is_stopping(NULL) ? 1 : 0, "is_stopping(NULL) false");
}

static void test_state_integer_overflow_protection(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Simulate edge case: operation id reached UINT64_MAX */
    state.current_operation_id = UINT64_MAX;

    FlexStateStatus status = flex_state_begin_operation(&state, FLEX_OPERATION_CHAT);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_ID_OVERFLOW, status, "reject op on id overflow");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_ID_OVERFLOW, state.last_error, "last_error ID_OVERFLOW");
    ASSERT_U64_EQ(UINT64_MAX, state.current_operation_id, "id not wrapped, still UINT64_MAX");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "lifecycle remains IDLE");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op remains NONE");
}

static void test_state_error_reset(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Cause error */
    flex_state_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, state.last_error, "has error");

    /* Reset error */
    FlexStateStatus r = flex_state_reset_error(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, r, "reset_error OK");
    ASSERT_INT_EQ(FLEX_STATE_OK, state.last_error, "last_error cleared to OK");
    ASSERT_INT_EQ(FLEX_LIFECYCLE_IDLE, state.lifecycle, "lifecycle unaffected");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op unaffected");
}

static void test_state_string_and_mapping_helpers(void)
{
    /* Lifecycle to string */
    ASSERT_STR_EQ("FLEX_LIFECYCLE_IDLE", flex_lifecycle_to_string(FLEX_LIFECYCLE_IDLE), "lifecycle IDLE");
    ASSERT_STR_EQ("FLEX_LIFECYCLE_PROCESSING", flex_lifecycle_to_string(FLEX_LIFECYCLE_PROCESSING), "lifecycle PROCESSING");
    ASSERT_STR_EQ("FLEX_LIFECYCLE_STOPPING", flex_lifecycle_to_string(FLEX_LIFECYCLE_STOPPING), "lifecycle STOPPING");
    ASSERT_STR_EQ("INVALID_LIFECYCLE", flex_lifecycle_to_string((FlexLifecycleState)999), "lifecycle invalid");

    /* Operation to string */
    ASSERT_STR_EQ("FLEX_OPERATION_NONE", flex_operation_to_string(FLEX_OPERATION_NONE), "op NONE");
    ASSERT_STR_EQ("FLEX_OPERATION_CHAT", flex_operation_to_string(FLEX_OPERATION_CHAT), "op CHAT");
    ASSERT_STR_EQ("FLEX_OPERATION_VOICE", flex_operation_to_string(FLEX_OPERATION_VOICE), "op VOICE");
    ASSERT_STR_EQ("FLEX_OPERATION_MEMORY", flex_operation_to_string(FLEX_OPERATION_MEMORY), "op MEMORY");
    ASSERT_STR_EQ("INVALID_OPERATION", flex_operation_to_string((FlexOperation)999), "op invalid");

    /* Status to string */
    ASSERT_STR_EQ("FLEX_STATE_OK", flex_state_status_to_string(FLEX_STATE_OK), "status OK");
    ASSERT_STR_EQ("FLEX_STATE_ERROR_INVALID_ARGUMENT", flex_state_status_to_string(FLEX_STATE_ERROR_INVALID_ARGUMENT), "status INVALID_ARG");
    ASSERT_STR_EQ("FLEX_STATE_ERROR_INVALID_TRANSITION", flex_state_status_to_string(FLEX_STATE_ERROR_INVALID_TRANSITION), "status INVALID_TRANS");
    ASSERT_STR_EQ("FLEX_STATE_ERROR_ALREADY_PROCESSING", flex_state_status_to_string(FLEX_STATE_ERROR_ALREADY_PROCESSING), "status ALREADY_PROC");
    ASSERT_STR_EQ("FLEX_STATE_ERROR_NOT_PROCESSING", flex_state_status_to_string(FLEX_STATE_ERROR_NOT_PROCESSING), "status NOT_PROC");
    ASSERT_STR_EQ("FLEX_STATE_ERROR_STOPPING", flex_state_status_to_string(FLEX_STATE_ERROR_STOPPING), "status STOPPING");
    ASSERT_STR_EQ("FLEX_STATE_ERROR_ID_OVERFLOW", flex_state_status_to_string(FLEX_STATE_ERROR_ID_OVERFLOW), "status ID_OVERFLOW");
    ASSERT_STR_EQ("INVALID_STATUS", flex_state_status_to_string((FlexStateStatus)999), "status invalid");

    /* Mapping from command to operation */
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, flex_operation_from_command(FLEX_COMMAND_CHAT), "map CHAT");
    ASSERT_INT_EQ(FLEX_OPERATION_VOICE, flex_operation_from_command(FLEX_COMMAND_VOICE), "map VOICE");
    ASSERT_INT_EQ(FLEX_OPERATION_MEMORY, flex_operation_from_command(FLEX_COMMAND_MEMORY), "map MEMORY");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, flex_operation_from_command(FLEX_COMMAND_EXIT), "map EXIT");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, flex_operation_from_command(FLEX_COMMAND_UNKNOWN), "map UNKNOWN");
}

void run_state_manager_tests(int *tests_run, int *tests_passed, int *tests_failed)
{
    s_run = tests_run;
    s_passed = tests_passed;
    s_failed = tests_failed;

    printf("[Running Stage 1C State Manager Tests...]\n");
    test_state_initialization();
    test_state_begin_and_finish_each_operation();
    test_state_invalid_operations();
    test_state_finish_when_not_active();
    test_state_shutdown_behavior();
    test_state_repeated_operations_and_id_increments();
    test_state_integrity_after_rejected_transitions();
    test_state_null_pointer_handling();
    test_state_integer_overflow_protection();
    test_state_error_reset();
    test_state_string_and_mapping_helpers();
    printf("  Stage 1C tests completed.\n\n");
}
