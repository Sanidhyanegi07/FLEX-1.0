#include "test_orchestrator.h"
#include "orchestrator.h"

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

static void test_dispatch_each_supported_command(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* 1. chat */
    FlexDispatchResult r1 = flex_dispatch_command(&state, "chat");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r1.parser_status, "chat parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r1.state_status, "chat state status");
    ASSERT_INT_EQ(FLEX_COMMAND_CHAT, r1.command_type, "chat command type");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, r1.operation, "chat operation");
    ASSERT_U64_EQ(1, r1.operation_id, "chat op id 1");
    ASSERT_INT_EQ(1, r1.operation_started ? 1 : 0, "chat operation started");
    ASSERT_INT_EQ(0, r1.shutdown_requested ? 1 : 0, "chat shutdown not requested");
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "state is processing");

    FlexStateStatus f1 = flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f1, "finish chat OK");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state is idle");

    /* 2. voice */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "voice");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r2.parser_status, "voice parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r2.state_status, "voice state status");
    ASSERT_INT_EQ(FLEX_COMMAND_VOICE, r2.command_type, "voice command type");
    ASSERT_INT_EQ(FLEX_OPERATION_VOICE, r2.operation, "voice operation");
    ASSERT_U64_EQ(2, r2.operation_id, "voice op id 2");
    ASSERT_INT_EQ(1, r2.operation_started ? 1 : 0, "voice operation started");
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "state is processing");

    FlexStateStatus f2 = flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f2, "finish voice OK");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state is idle");

    /* 3. memory */
    FlexDispatchResult r3 = flex_dispatch_command(&state, "memory");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r3.parser_status, "memory parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r3.state_status, "memory state status");
    ASSERT_INT_EQ(FLEX_COMMAND_MEMORY, r3.command_type, "memory command type");
    ASSERT_INT_EQ(FLEX_OPERATION_MEMORY, r3.operation, "memory operation");
    ASSERT_U64_EQ(3, r3.operation_id, "memory op id 3");
    ASSERT_INT_EQ(1, r3.operation_started ? 1 : 0, "memory operation started");
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "state is processing");

    FlexStateStatus f3 = flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f3, "finish memory OK");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state is idle");

    /* 4. exit */
    FlexDispatchResult r4 = flex_dispatch_command(&state, "exit");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r4.parser_status, "exit parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r4.state_status, "exit state status");
    ASSERT_INT_EQ(FLEX_COMMAND_EXIT, r4.command_type, "exit command type");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, r4.operation, "exit operation NONE");
    ASSERT_U64_EQ(0, r4.operation_id, "exit op id 0");
    ASSERT_INT_EQ(0, r4.operation_started ? 1 : 0, "exit op not started");
    ASSERT_INT_EQ(1, r4.shutdown_requested ? 1 : 0, "exit shutdown requested");
    ASSERT_INT_EQ(1, flex_state_is_stopping(&state) ? 1 : 0, "state is stopping");
}

static void test_dispatch_commands_with_arguments(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* chat with prompt argument */
    FlexDispatchResult r1 = flex_dispatch_command(&state, "chat Explain pointers in C");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r1.parser_status, "chat args parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r1.state_status, "chat args state status");
    ASSERT_INT_EQ(1, r1.operation_started ? 1 : 0, "chat args started");
    ASSERT_STR_EQ("Explain pointers in C", r1.command_result.args, "chat args content");
    ASSERT_INT_EQ(1, r1.command_result.has_args ? 1 : 0, "chat args has_args");
    flex_dispatch_finish_operation(&state);

    /* voice with audio device argument */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "voice default_mic");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r2.parser_status, "voice args parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r2.state_status, "voice args state status");
    ASSERT_INT_EQ(1, r2.operation_started ? 1 : 0, "voice args started");
    ASSERT_STR_EQ("default_mic", r2.command_result.args, "voice args content");
    flex_dispatch_finish_operation(&state);

    /* memory with store key-value argument */
    FlexDispatchResult r3 = flex_dispatch_command(&state, "memory store user_name Alice");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r3.parser_status, "memory args parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r3.state_status, "memory args state status");
    ASSERT_INT_EQ(1, r3.operation_started ? 1 : 0, "memory args started");
    ASSERT_STR_EQ("store user_name Alice", r3.command_result.args, "memory args content");
    flex_dispatch_finish_operation(&state);

    /* exit with argument */
    FlexDispatchResult r4 = flex_dispatch_command(&state, "exit now");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r4.parser_status, "exit args parser status");
    ASSERT_INT_EQ(1, r4.shutdown_requested ? 1 : 0, "exit args shutdown requested");
    ASSERT_STR_EQ("now", r4.command_result.args, "exit args content");
    ASSERT_INT_EQ(1, flex_state_is_stopping(&state) ? 1 : 0, "state is stopping");
}

static void test_dispatch_unknown_and_malformed_input(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* unknown verb with arguments */
    FlexDispatchResult r1 = flex_dispatch_command(&state, "unknown_verb arg1 arg2");
    ASSERT_INT_EQ(FLEX_STATUS_UNKNOWN_COMMAND, r1.parser_status, "unknown parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r1.state_status, "unknown state status OK");
    ASSERT_INT_EQ(0, r1.operation_started ? 1 : 0, "unknown op not started");
    ASSERT_U64_EQ(0, r1.operation_id, "unknown op id 0");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");

    /* number input */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "12345");
    ASSERT_INT_EQ(FLEX_STATUS_UNKNOWN_COMMAND, r2.parser_status, "number parser status");
    ASSERT_INT_EQ(0, r2.operation_started ? 1 : 0, "number op not started");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");

    /* symbol input */
    FlexDispatchResult r3 = flex_dispatch_command(&state, "!@#$");
    ASSERT_INT_EQ(FLEX_STATUS_UNKNOWN_COMMAND, r3.parser_status, "symbols parser status");
    ASSERT_INT_EQ(0, r3.operation_started ? 1 : 0, "symbols op not started");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");
}

static void test_dispatch_empty_and_oversized_input(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* empty string */
    FlexDispatchResult r1 = flex_dispatch_command(&state, "");
    ASSERT_INT_EQ(FLEX_STATUS_EMPTY_INPUT, r1.parser_status, "empty parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r1.state_status, "empty state status OK");
    ASSERT_INT_EQ(0, r1.operation_started ? 1 : 0, "empty op not started");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");

    /* whitespace only */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "   \t\r\n");
    ASSERT_INT_EQ(FLEX_STATUS_EMPTY_INPUT, r2.parser_status, "whitespace parser status");
    ASSERT_INT_EQ(0, r2.operation_started ? 1 : 0, "whitespace op not started");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");

    /* oversized input */
    char oversized[FLEX_MAX_INPUT_LEN + 2];
    memcpy(oversized, "chat ", 5);
    memset(oversized + 5, 'X', FLEX_MAX_INPUT_LEN - 4);
    oversized[FLEX_MAX_INPUT_LEN + 1] = '\0';

    FlexDispatchResult r3 = flex_dispatch_command(&state, oversized);
    ASSERT_INT_EQ(FLEX_STATUS_INPUT_TOO_LONG, r3.parser_status, "oversized parser status");
    ASSERT_INT_EQ(FLEX_STATE_OK, r3.state_status, "oversized state status OK");
    ASSERT_INT_EQ(0, r3.operation_started ? 1 : 0, "oversized op not started");
    ASSERT_U64_EQ(0, r3.operation_id, "oversized op id 0");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");
}

static void test_dispatch_concurrent_operation_rejection(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Start chat */
    FlexDispatchResult r1 = flex_dispatch_command(&state, "chat First Task");
    ASSERT_INT_EQ(1, r1.operation_started ? 1 : 0, "chat started");
    ASSERT_U64_EQ(1, r1.operation_id, "chat id 1");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "current op CHAT");

    /* Attempt to start voice while chat is active */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "voice");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r2.parser_status, "voice syntax valid");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_ALREADY_PROCESSING, r2.state_status, "state rejected ALREADY_PROCESSING");
    ASSERT_INT_EQ(0, r2.operation_started ? 1 : 0, "voice not started");
    ASSERT_U64_EQ(0, r2.operation_id, "voice op id 0");

    /* Verify state integrity */
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "still processing");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "still active CHAT");
    ASSERT_U64_EQ(1, state.current_operation_id, "still op id 1");

    /* Attempt to start memory while chat is active */
    FlexDispatchResult r3 = flex_dispatch_command(&state, "memory test");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r3.parser_status, "memory syntax valid");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_ALREADY_PROCESSING, r3.state_status, "state rejected ALREADY_PROCESSING");
    ASSERT_INT_EQ(0, r3.operation_started ? 1 : 0, "memory not started");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "still active CHAT");

    /* Attempt invalid command while chat is active */
    FlexDispatchResult r4 = flex_dispatch_command(&state, "unknown_command");
    ASSERT_INT_EQ(FLEX_STATUS_UNKNOWN_COMMAND, r4.parser_status, "unknown parser status");
    ASSERT_INT_EQ(0, r4.operation_started ? 1 : 0, "unknown not started");
    ASSERT_INT_EQ(FLEX_OPERATION_CHAT, state.current_operation, "still active CHAT");
    ASSERT_U64_EQ(1, state.current_operation_id, "still op id 1");

    /* Finish chat */
    flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "now idle");
}

static void test_dispatch_finish_and_next_operation(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* 1. Start chat */
    flex_dispatch_command(&state, "chat First");
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "processing chat");
    ASSERT_U64_EQ(1, state.current_operation_id, "id 1");

    /* 2. Explicit finish */
    FlexStateStatus f1 = flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f1, "finish OK");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "idle");
    ASSERT_U64_EQ(1, state.current_operation_id, "id remains 1");

    /* 3. Start voice */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "voice Second");
    ASSERT_INT_EQ(1, r2.operation_started ? 1 : 0, "voice started");
    ASSERT_U64_EQ(2, r2.operation_id, "id 2");
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "processing voice");

    /* 4. Finish voice */
    FlexStateStatus f2 = flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f2, "finish voice OK");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "idle");

    /* 5. Finish when already idle fails */
    FlexStateStatus f3 = flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_NOT_PROCESSING, f3, "finish while idle fails");
}

static void test_dispatch_shutdown_behavior(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Case A: Shutdown from IDLE */
    FlexDispatchResult r1 = flex_dispatch_command(&state, "exit");
    ASSERT_INT_EQ(1, r1.shutdown_requested ? 1 : 0, "shutdown requested");
    ASSERT_INT_EQ(1, flex_state_is_stopping(&state) ? 1 : 0, "stopping");

    /* Attempting to dispatch when STOPPING is rejected */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "chat hello");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r2.parser_status, "parser syntax OK");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_STOPPING, r2.state_status, "state rejected STOPPING");
    ASSERT_INT_EQ(0, r2.operation_started ? 1 : 0, "op not started");

    /* Case B: Shutdown during active processing (graceful drain) */
    flex_state_init(&state);
    flex_dispatch_command(&state, "memory store session 42");
    ASSERT_INT_EQ(1, flex_state_is_processing(&state) ? 1 : 0, "processing");

    FlexDispatchResult r3 = flex_dispatch_command(&state, "exit");
    ASSERT_INT_EQ(1, r3.shutdown_requested ? 1 : 0, "shutdown requested during processing");
    ASSERT_INT_EQ(1, flex_state_is_stopping(&state) ? 1 : 0, "lifecycle is stopping");
    ASSERT_INT_EQ(FLEX_OPERATION_MEMORY, state.current_operation, "memory op still active");

    /* Cannot start new op while draining */
    FlexDispatchResult r4 = flex_dispatch_command(&state, "voice");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_STOPPING, r4.state_status, "cannot start new op");
    ASSERT_INT_EQ(0, r4.operation_started ? 1 : 0, "not started");

    /* Finish the active operation */
    FlexStateStatus f1 = flex_dispatch_finish_operation(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f1, "finish active op OK");
    ASSERT_INT_EQ(FLEX_OPERATION_NONE, state.current_operation, "op cleared");
    ASSERT_INT_EQ(1, flex_state_is_stopping(&state) ? 1 : 0, "lifecycle remains stopping");
}

static void test_dispatch_state_preservation_and_operation_ids(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    ASSERT_U64_EQ(0, state.current_operation_id, "initial id 0");

    /* Dispatch malformed commands -> ID must NOT advance */
    flex_dispatch_command(&state, "");
    ASSERT_U64_EQ(0, state.current_operation_id, "id still 0 after empty");

    flex_dispatch_command(&state, "unknown_cmd");
    ASSERT_U64_EQ(0, state.current_operation_id, "id still 0 after unknown");

    /* Start valid command -> ID advances to 1 */
    FlexDispatchResult r1 = flex_dispatch_command(&state, "chat Op 1");
    ASSERT_U64_EQ(1, r1.operation_id, "id 1");
    ASSERT_U64_EQ(1, state.current_operation_id, "state id 1");

    /* Rejected command while active -> ID must NOT advance */
    flex_dispatch_command(&state, "voice Op 2");
    ASSERT_U64_EQ(1, state.current_operation_id, "id still 1 after rejected");

    /* Finish command -> ID remains 1 */
    flex_dispatch_finish_operation(&state);
    ASSERT_U64_EQ(1, state.current_operation_id, "id still 1 after finish");

    /* Next command -> ID advances to 2 */
    FlexDispatchResult r2 = flex_dispatch_command(&state, "voice Op 2");
    ASSERT_U64_EQ(2, r2.operation_id, "id 2");
    ASSERT_U64_EQ(2, state.current_operation_id, "state id 2");
    flex_dispatch_finish_operation(&state);
}

static void test_dispatch_null_pointer_handling(void)
{
    /* NULL state pointer */
    FlexDispatchResult r1 = flex_dispatch_command(NULL, "chat hello");
    ASSERT_INT_EQ(FLEX_STATUS_INVALID_INPUT, r1.parser_status, "null state parser status");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, r1.state_status, "null state status");
    ASSERT_INT_EQ(0, r1.operation_started ? 1 : 0, "null state not started");

    /* NULL command string */
    FlexStateManager state;
    flex_state_init(&state);

    FlexDispatchResult r2 = flex_dispatch_command(&state, NULL);
    ASSERT_INT_EQ(FLEX_STATUS_INVALID_INPUT, r2.parser_status, "null command parser status");
    ASSERT_INT_EQ(0, r2.operation_started ? 1 : 0, "null command not started");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");

    /* NULL state to finish */
    FlexStateStatus f1 = flex_dispatch_finish_operation(NULL);
    ASSERT_INT_EQ(FLEX_STATE_ERROR_INVALID_ARGUMENT, f1, "null state finish fails");
}

static void test_dispatch_operation_id_overflow(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Simulate edge case: UINT64_MAX */
    state.current_operation_id = UINT64_MAX;

    FlexDispatchResult r1 = flex_dispatch_command(&state, "chat Overflow Test");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r1.parser_status, "parser syntax OK");
    ASSERT_INT_EQ(FLEX_STATE_ERROR_ID_OVERFLOW, r1.state_status, "state ID_OVERFLOW");
    ASSERT_INT_EQ(0, r1.operation_started ? 1 : 0, "operation not started");
    ASSERT_U64_EQ(0, r1.operation_id, "result op id 0");
    ASSERT_U64_EQ(UINT64_MAX, state.current_operation_id, "state id uncorrupted at UINT64_MAX");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "state remains idle");
}

static void test_dispatch_aliases(void)
{
    FlexStateManager state;
    flex_state_init(&state);

    /* Test flex_orchestrator_dispatch alias */
    FlexOrchestratorResult r1 = flex_orchestrator_dispatch(&state, "chat alias test");
    ASSERT_INT_EQ(FLEX_STATUS_OK, r1.parser_status, "alias parser status");
    ASSERT_INT_EQ(1, r1.operation_started ? 1 : 0, "alias op started");
    ASSERT_U64_EQ(1, r1.operation_id, "alias op id 1");

    /* Test flex_orchestrator_finish alias */
    FlexStateStatus f1 = flex_orchestrator_finish(&state);
    ASSERT_INT_EQ(FLEX_STATE_OK, f1, "alias finish status");
    ASSERT_INT_EQ(1, flex_state_is_idle(&state) ? 1 : 0, "alias state is idle");
}

void run_orchestrator_tests(int *tests_run, int *tests_passed, int *tests_failed)
{
    s_run = tests_run;
    s_passed = tests_passed;
    s_failed = tests_failed;

    printf("[Running Stage 1D Orchestrator Integration Tests...]\n");
    test_dispatch_each_supported_command();
    test_dispatch_commands_with_arguments();
    test_dispatch_unknown_and_malformed_input();
    test_dispatch_empty_and_oversized_input();
    test_dispatch_concurrent_operation_rejection();
    test_dispatch_finish_and_next_operation();
    test_dispatch_shutdown_behavior();
    test_dispatch_state_preservation_and_operation_ids();
    test_dispatch_null_pointer_handling();
    test_dispatch_operation_id_overflow();
    test_dispatch_aliases();
    printf("  Stage 1D tests completed.\n\n");
}
