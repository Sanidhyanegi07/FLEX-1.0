#include "command_parser.h"
#include "test_state_manager.h"
#include "test_orchestrator.h"
#include "test_c_core_modules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_INT_EQ(expected, actual, msg) do { \
    g_tests_run++; \
    int _exp = (int)(expected); \
    int _act = (int)(actual); \
    if (_exp == _act) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "FAIL: line %d: %s (expected %d, got %d)\n", \
                __LINE__, (msg), _exp, _act); \
    } \
} while (0)

#define ASSERT_STR_EQ(expected, actual, msg) do { \
    g_tests_run++; \
    const char *_exp = (expected); \
    const char *_act = (actual); \
    if (_exp != NULL && _act != NULL && strcmp(_exp, _act) == 0) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "FAIL: line %d: %s (expected '%s', got '%s')\n", \
                __LINE__, (msg), _exp ? _exp : "(null)", _act ? _act : "(null)"); \
    } \
} while (0)

#define ASSERT_COMMAND_TYPE(expected, input_expr) do { \
    g_tests_run++; \
    FlexCommandType actual = flex_parse_command(input_expr); \
    if (actual == (expected)) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        fprintf(stderr, "FAIL: line %d: input '%s' expected %s (%d), got %s (%d)\n", \
                __LINE__, \
                (input_expr) ? (input_expr) : "(null)", \
                flex_command_type_to_string(expected), (int)(expected), \
                flex_command_type_to_string(actual), (int)(actual)); \
    } \
} while (0)

#define ASSERT_COMMAND_RESULT(input_expr, exp_type, exp_status, exp_name, exp_args, exp_has_args) do { \
    FlexCommandResult res = flex_parse_command_result(input_expr); \
    ASSERT_INT_EQ((exp_type), res.type, "res.type"); \
    ASSERT_INT_EQ((exp_status), res.status, "res.status"); \
    ASSERT_STR_EQ((exp_name), res.name, "res.name"); \
    ASSERT_STR_EQ((exp_args), res.args, "res.args"); \
    ASSERT_INT_EQ((exp_has_args) ? 1 : 0, res.has_args ? 1 : 0, "res.has_args"); \
} while (0)

/* ========================================================================= */
/* Stage 1A Regression Tests (Must remain 100% working and backward-compatible) */
/* ========================================================================= */

static void test_exact_commands(void)
{
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "chat");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_VOICE, "voice");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_MEMORY, "memory");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_EXIT, "exit");
}

static void test_case_insensitivity(void)
{
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "CHAT");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "Chat");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "cHaT");

    ASSERT_COMMAND_TYPE(FLEX_COMMAND_VOICE, "VOICE");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_VOICE, "Voice");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_VOICE, "vOiCe");

    ASSERT_COMMAND_TYPE(FLEX_COMMAND_MEMORY, "MEMORY");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_MEMORY, "Memory");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_MEMORY, "mEmOrY");

    ASSERT_COMMAND_TYPE(FLEX_COMMAND_EXIT, "EXIT");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_EXIT, "Exit");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_EXIT, "eXiT");
}

static void test_whitespace_handling(void)
{
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "  chat");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "chat  ");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "   chat   ");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "\tchat\t");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_CHAT, "\n  chat  \r\n");

    ASSERT_COMMAND_TYPE(FLEX_COMMAND_VOICE, "  voice  ");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_MEMORY, "  memory  ");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_EXIT, "  exit  ");
}

static void test_unknown_commands(void)
{
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "   ");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "\t\n\r");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "unknown");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "chatt");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "chatting");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "chat help");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "help");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "12345");
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, "!@#$");
}

static void test_null_input(void)
{
    ASSERT_COMMAND_TYPE(FLEX_COMMAND_UNKNOWN, NULL);
}

/* ========================================================================= */
/* Stage 1B Tests: Structured Command Results & Safe Argument Parsing        */
/* ========================================================================= */

static void test_stage1b_valid_commands_with_args(void)
{
    /* chat with prompt argument */
    ASSERT_COMMAND_RESULT("chat Explain pointers in C",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "Explain pointers in C", true);

    /* case insensitivity on command, preserving casing in argument */
    ASSERT_COMMAND_RESULT("CHAT Explain pointers in C",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "Explain pointers in C", true);
    ASSERT_COMMAND_RESULT("cHaT  Mixed Casing Argument Here ",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "Mixed Casing Argument Here", true);

    /* voice with device or action argument */
    ASSERT_COMMAND_RESULT("voice default_mic",
                          FLEX_COMMAND_VOICE, FLEX_STATUS_OK,
                          "voice", "default_mic", true);

    /* memory with key-value or query argument */
    ASSERT_COMMAND_RESULT("memory store user_name Alice",
                          FLEX_COMMAND_MEMORY, FLEX_STATUS_OK,
                          "memory", "store user_name Alice", true);

    /* exit with argument */
    ASSERT_COMMAND_RESULT("exit now",
                          FLEX_COMMAND_EXIT, FLEX_STATUS_OK,
                          "exit", "now", true);

    /* internal spaces preserved, leading/trailing trimmed */
    ASSERT_COMMAND_RESULT("chat    word1   word2   word3   ",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "word1   word2   word3", true);
}

static void test_stage1b_valid_commands_no_args(void)
{
    ASSERT_COMMAND_RESULT("chat",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "", false);
    ASSERT_COMMAND_RESULT("  voice  ",
                          FLEX_COMMAND_VOICE, FLEX_STATUS_OK,
                          "voice", "", false);
    ASSERT_COMMAND_RESULT("MEMORY",
                          FLEX_COMMAND_MEMORY, FLEX_STATUS_OK,
                          "memory", "", false);
    ASSERT_COMMAND_RESULT("exit",
                          FLEX_COMMAND_EXIT, FLEX_STATUS_OK,
                          "exit", "", false);
}

static void test_stage1b_whitespace_and_argument_formatting(void)
{
    /* tabs and newlines */
    ASSERT_COMMAND_RESULT("\tchat\ttab argument\t",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "tab argument", true);
    ASSERT_COMMAND_RESULT("\r\n  voice  \t  start recording  \r\n",
                          FLEX_COMMAND_VOICE, FLEX_STATUS_OK,
                          "voice", "start recording", true);

    /* command with only trailing whitespace, no arguments */
    ASSERT_COMMAND_RESULT("chat   \t  \n",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "", false);

    /* single character argument */
    ASSERT_COMMAND_RESULT("chat ?",
                          FLEX_COMMAND_CHAT, FLEX_STATUS_OK,
                          "chat", "?", true);
}

static void test_stage1b_unknown_commands(void)
{
    ASSERT_COMMAND_RESULT("unknown",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_UNKNOWN_COMMAND,
                          "unknown", "", false);
    ASSERT_COMMAND_RESULT("unknown some arg",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_UNKNOWN_COMMAND,
                          "unknown", "some arg", true);
    ASSERT_COMMAND_RESULT("chatt typo",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_UNKNOWN_COMMAND,
                          "chatt", "typo", true);
    ASSERT_COMMAND_RESULT("help me",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_UNKNOWN_COMMAND,
                          "help", "me", true);
    ASSERT_COMMAND_RESULT("12345 number arg",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_UNKNOWN_COMMAND,
                          "12345", "number arg", true);
    ASSERT_COMMAND_RESULT("!@#$ symbols",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_UNKNOWN_COMMAND,
                          "!@#$", "symbols", true);
}

static void test_stage1b_empty_and_null_inputs(void)
{
    ASSERT_COMMAND_RESULT("",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_EMPTY_INPUT,
                          "", "", false);
    ASSERT_COMMAND_RESULT("   ",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_EMPTY_INPUT,
                          "", "", false);
    ASSERT_COMMAND_RESULT("\t\r\n",
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_EMPTY_INPUT,
                          "", "", false);
    ASSERT_COMMAND_RESULT(NULL,
                          FLEX_COMMAND_UNKNOWN, FLEX_STATUS_INVALID_INPUT,
                          "", "", false);
}

static void test_stage1b_oversized_and_boundary_inputs(void)
{
    char boundary_input[FLEX_MAX_INPUT_LEN + 1];
    char oversized_input[FLEX_MAX_INPUT_LEN + 2];

    /* Construct exactly 1024 characters: "chat " + 1019 'A's */
    memcpy(boundary_input, "chat ", 5);
    memset(boundary_input + 5, 'A', FLEX_MAX_INPUT_LEN - 5);
    boundary_input[FLEX_MAX_INPUT_LEN] = '\0';

    FlexCommandResult b_res = flex_parse_command_result(boundary_input);
    ASSERT_INT_EQ(FLEX_COMMAND_CHAT, b_res.type, "boundary type");
    ASSERT_INT_EQ(FLEX_STATUS_OK, b_res.status, "boundary status");
    ASSERT_STR_EQ("chat", b_res.name, "boundary name");
    ASSERT_INT_EQ(1019, b_res.args_len, "boundary args_len");
    ASSERT_INT_EQ(1, b_res.has_args ? 1 : 0, "boundary has_args");
    ASSERT_INT_EQ('A', b_res.args[0], "boundary first arg char");
    ASSERT_INT_EQ('A', b_res.args[1018], "boundary last arg char");
    ASSERT_INT_EQ('\0', b_res.args[1019], "boundary null terminator");

    /* Construct 1025 characters: "chat " + 1020 'B's (exceeds FLEX_MAX_INPUT_LEN) */
    memcpy(oversized_input, "chat ", 5);
    memset(oversized_input + 5, 'B', FLEX_MAX_INPUT_LEN - 4);
    oversized_input[FLEX_MAX_INPUT_LEN + 1] = '\0';

    FlexCommandResult o_res = flex_parse_command_result(oversized_input);
    ASSERT_INT_EQ(FLEX_COMMAND_UNKNOWN, o_res.type, "oversized type");
    ASSERT_INT_EQ(FLEX_STATUS_INPUT_TOO_LONG, o_res.status, "oversized status");
    ASSERT_STR_EQ("", o_res.name, "oversized name empty");
    ASSERT_STR_EQ("", o_res.args, "oversized args empty");
    ASSERT_INT_EQ(0, o_res.has_args ? 1 : 0, "oversized has_args false");
}

static void test_stage1b_oversized_command_name(void)
{
    /* Single command word of 80 characters (exceeds FLEX_MAX_COMMAND_LEN 64) */
    char long_verb_input[100];
    memset(long_verb_input, 'z', 80);
    memcpy(long_verb_input + 80, " args", 6);

    FlexCommandResult v_res = flex_parse_command_result(long_verb_input);
    ASSERT_INT_EQ(FLEX_COMMAND_UNKNOWN, v_res.type, "long verb type");
    ASSERT_INT_EQ(FLEX_STATUS_UNKNOWN_COMMAND, v_res.status, "long verb status");
    ASSERT_INT_EQ(FLEX_MAX_COMMAND_LEN - 1, (int)strlen(v_res.name), "long verb truncated safely");
    ASSERT_STR_EQ("args", v_res.args, "long verb args parsed");
    ASSERT_INT_EQ(1, v_res.has_args ? 1 : 0, "long verb has_args true");
}

static void test_stage1b_extended_api_and_helpers(void)
{
    /* flex_parse_command_line alias */
    FlexCommandResult line_res = flex_parse_command_line("voice mic_1");
    ASSERT_INT_EQ(FLEX_COMMAND_VOICE, line_res.type, "line_res type");
    ASSERT_INT_EQ(FLEX_STATUS_OK, line_res.status, "line_res status");
    ASSERT_STR_EQ("mic_1", line_res.args, "line_res args");

    /* flex_parse_command_ex with pointer */
    FlexCommandResult ex_res;
    FlexCommandStatus ex_status = flex_parse_command_ex("memory lookup key1", &ex_res);
    ASSERT_INT_EQ(FLEX_STATUS_OK, ex_status, "ex_status OK");
    ASSERT_INT_EQ(FLEX_COMMAND_MEMORY, ex_res.type, "ex_res type");
    ASSERT_STR_EQ("lookup key1", ex_res.args, "ex_res args");

    /* flex_parse_command_ex with NULL pointer */
    FlexCommandStatus null_status = flex_parse_command_ex("memory lookup key1", NULL);
    ASSERT_INT_EQ(FLEX_STATUS_INVALID_INPUT, null_status, "ex null pointer status");

    /* flex_command_type_to_string */
    ASSERT_STR_EQ("FLEX_COMMAND_UNKNOWN", flex_command_type_to_string(FLEX_COMMAND_UNKNOWN), "type str UNKNOWN");
    ASSERT_STR_EQ("FLEX_COMMAND_CHAT", flex_command_type_to_string(FLEX_COMMAND_CHAT), "type str CHAT");
    ASSERT_STR_EQ("FLEX_COMMAND_VOICE", flex_command_type_to_string(FLEX_COMMAND_VOICE), "type str VOICE");
    ASSERT_STR_EQ("FLEX_COMMAND_MEMORY", flex_command_type_to_string(FLEX_COMMAND_MEMORY), "type str MEMORY");
    ASSERT_STR_EQ("FLEX_COMMAND_EXIT", flex_command_type_to_string(FLEX_COMMAND_EXIT), "type str EXIT");
    ASSERT_STR_EQ("INVALID_TYPE", flex_command_type_to_string((FlexCommandType)999), "type str INVALID");

    /* flex_command_status_to_string */
    ASSERT_STR_EQ("FLEX_STATUS_OK", flex_command_status_to_string(FLEX_STATUS_OK), "status str OK");
    ASSERT_STR_EQ("FLEX_STATUS_UNKNOWN_COMMAND", flex_command_status_to_string(FLEX_STATUS_UNKNOWN_COMMAND), "status str UNKNOWN_COMMAND");
    ASSERT_STR_EQ("FLEX_STATUS_EMPTY_INPUT", flex_command_status_to_string(FLEX_STATUS_EMPTY_INPUT), "status str EMPTY_INPUT");
    ASSERT_STR_EQ("FLEX_STATUS_INVALID_INPUT", flex_command_status_to_string(FLEX_STATUS_INVALID_INPUT), "status str INVALID_INPUT");
    ASSERT_STR_EQ("FLEX_STATUS_INPUT_TOO_LONG", flex_command_status_to_string(FLEX_STATUS_INPUT_TOO_LONG), "status str INPUT_TOO_LONG");
    ASSERT_STR_EQ("INVALID_STATUS", flex_command_status_to_string((FlexCommandStatus)999), "status str INVALID");
}

int main(void)
{
    printf("=== Running FLEX 1.0 Test Suite (Stage 1A, 1B, 1C, 1D) ===\n\n");

    printf("[Running Stage 1A Regression Tests...]\n");
    test_exact_commands();
    test_case_insensitivity();
    test_whitespace_handling();
    test_unknown_commands();
    test_null_input();
    printf("  Stage 1A tests completed (35 assertions).\n\n");

    printf("[Running Stage 1B Structured Parser Tests...]\n");
    test_stage1b_valid_commands_with_args();
    test_stage1b_valid_commands_no_args();
    test_stage1b_whitespace_and_argument_formatting();
    test_stage1b_unknown_commands();
    test_stage1b_empty_and_null_inputs();
    test_stage1b_oversized_and_boundary_inputs();
    test_stage1b_oversized_command_name();
    test_stage1b_extended_api_and_helpers();
    printf("  Stage 1B tests completed.\n\n");

    run_state_manager_tests(&g_tests_run, &g_tests_passed, &g_tests_failed);

    run_orchestrator_tests(&g_tests_run, &g_tests_passed, &g_tests_failed);

    run_all_extended_c_core_tests(&g_tests_run, &g_tests_passed, &g_tests_failed);

    printf("========================================\n");
    printf("Test Summary:\n");
    printf("  Total Run : %d\n", g_tests_run);
    printf("  Passed    : %d\n", g_tests_passed);
    printf("  Failed    : %d\n", g_tests_failed);
    printf("========================================\n");

    if (g_tests_failed == 0) {
        printf("Result: SUCCESS (All %d tests passed)\n", g_tests_passed);
        return 0;
    }

    printf("Result: FAILURE (%d test(s) failed)\n", g_tests_failed);
    return 1;
}


