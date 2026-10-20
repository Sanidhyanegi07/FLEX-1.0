#include "command_parser.h"
#include "state_manager.h"
#include "orchestrator.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BRIDGE_BUFFER_SIZE 2048

static void print_json_escaped_string(const char *str)
{
    if (str == NULL) {
        fputs("\"\"", stdout);
        return;
    }

    fputc('"', stdout);
    for (const unsigned char *p = (const unsigned char *)str; *p != '\0'; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", stdout);
            break;
        case '\\':
            fputs("\\\\", stdout);
            break;
        case '\b':
            fputs("\\b", stdout);
            break;
        case '\f':
            fputs("\\f", stdout);
            break;
        case '\n':
            fputs("\\n", stdout);
            break;
        case '\r':
            fputs("\\r", stdout);
            break;
        case '\t':
            fputs("\\t", stdout);
            break;
        default:
            if (*p < 0x20) {
                fprintf(stdout, "\\u%04x", (unsigned int)*p);
            } else {
                fputc(*p, stdout);
            }
            break;
        }
    }
    fputc('"', stdout);
}

static void print_json_error(const char *error_code, const char *message)
{
    fputs("{\"action\":\"ERROR\",\"success\":false,\"error\":", stdout);
    print_json_escaped_string(error_code);
    fputs(",\"message\":", stdout);
    print_json_escaped_string(message);
    fputs("}\n", stdout);
    fflush(stdout);
}

static void handle_dispatch(FlexStateManager *state, const char *command)
{
    FlexDispatchResult res = flex_dispatch_command(state, command);
    bool overall_success = (res.parser_status == FLEX_STATUS_OK && res.state_status == FLEX_STATE_OK);

    fputs("{\"action\":\"DISPATCH\",\"success\":", stdout);
    fputs(overall_success ? "true" : "false", stdout);

    fputs(",\"parser_status\":", stdout);
    print_json_escaped_string(flex_command_status_to_string(res.parser_status));

    fputs(",\"state_status\":", stdout);
    print_json_escaped_string(flex_state_status_to_string(res.state_status));

    fputs(",\"command_type\":", stdout);
    print_json_escaped_string(flex_command_type_to_string(res.command_type));

    fputs(",\"operation\":", stdout);
    print_json_escaped_string(flex_operation_to_string(res.operation));

    fprintf(stdout, ",\"operation_id\":%llu", (unsigned long long)res.operation_id);

    fputs(",\"operation_started\":", stdout);
    fputs(res.operation_started ? "true" : "false", stdout);

    fputs(",\"shutdown_requested\":", stdout);
    fputs(res.shutdown_requested ? "true" : "false", stdout);

    fputs(",\"command_name\":", stdout);
    print_json_escaped_string(res.command_result.name);

    fputs(",\"args\":", stdout);
    print_json_escaped_string(res.command_result.args);

    fputs(",\"lifecycle\":", stdout);
    print_json_escaped_string(flex_lifecycle_to_string(state->lifecycle));

    fputs(",\"active_operation\":", stdout);
    print_json_escaped_string(flex_operation_to_string(state->current_operation));

    fputs("}\n", stdout);
    fflush(stdout);
}

static void handle_finish(FlexStateManager *state)
{
    FlexStateStatus st = flex_dispatch_finish_operation(state);
    bool overall_success = (st == FLEX_STATE_OK);

    fputs("{\"action\":\"FINISH\",\"success\":", stdout);
    fputs(overall_success ? "true" : "false", stdout);

    fputs(",\"state_status\":", stdout);
    print_json_escaped_string(flex_state_status_to_string(st));

    fputs(",\"lifecycle\":", stdout);
    print_json_escaped_string(flex_lifecycle_to_string(state->lifecycle));

    fputs(",\"active_operation\":", stdout);
    print_json_escaped_string(flex_operation_to_string(state->current_operation));

    fprintf(stdout, ",\"operation_id\":%llu", (unsigned long long)state->current_operation_id);

    fputs("}\n", stdout);
    fflush(stdout);
}

static void handle_status(const FlexStateManager *state)
{
    fputs("{\"action\":\"STATUS\",\"success\":true", stdout);

    fputs(",\"lifecycle\":", stdout);
    print_json_escaped_string(flex_lifecycle_to_string(state->lifecycle));

    fputs(",\"active_operation\":", stdout);
    print_json_escaped_string(flex_operation_to_string(state->current_operation));

    fprintf(stdout, ",\"operation_id\":%llu", (unsigned long long)state->current_operation_id);

    fputs(",\"last_error\":", stdout);
    print_json_escaped_string(flex_state_status_to_string(state->last_error));

    fputs("}\n", stdout);
    fflush(stdout);
}

static void handle_reset_error(FlexStateManager *state)
{
    FlexStateStatus st = flex_state_reset_error(state);

    fputs("{\"action\":\"RESET_ERROR\",\"success\":true", stdout);

    fputs(",\"state_status\":", stdout);
    print_json_escaped_string(flex_state_status_to_string(st));

    fputs(",\"last_error\":", stdout);
    print_json_escaped_string(flex_state_status_to_string(state->last_error));

    fputs("}\n", stdout);
    fflush(stdout);
}

int main(void)
{
    FlexStateManager state;
    if (flex_state_init(&state) != FLEX_STATE_OK) {
        fprintf(stderr, "bridge: failed to initialize state manager\n");
        return 1;
    }

    char line_buf[BRIDGE_BUFFER_SIZE];

    while (fgets(line_buf, sizeof(line_buf), stdin) != NULL) {
        size_t len = strlen(line_buf);

        /* Detect line length exceeding buffer and drain remaining line */
        if (len > 0 && line_buf[len - 1] != '\n' && !feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {
                /* Discard excess characters */
            }
            print_json_error("REQUEST_TOO_LONG", "Request line exceeds buffer limit");
            continue;
        }

        /* Strip trailing \r and \n */
        while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
            line_buf[--len] = '\0';
        }

        /* Skip empty lines */
        if (len == 0) {
            print_json_error("EMPTY_REQUEST", "Request line is empty");
            continue;
        }

        /* Parse action */
        if (strncmp(line_buf, "DISPATCH", 8) == 0) {
            if (line_buf[8] != '\t') {
                print_json_error("MALFORMED_REQUEST", "DISPATCH requires <TAB> separator before command");
                continue;
            }
            const char *command = line_buf + 9;
            handle_dispatch(&state, command);
        } else if (strcmp(line_buf, "FINISH") == 0) {
            handle_finish(&state);
        } else if (strcmp(line_buf, "STATUS") == 0) {
            handle_status(&state);
        } else if (strcmp(line_buf, "RESET_ERROR") == 0) {
            handle_reset_error(&state);
        } else {
            print_json_error("UNKNOWN_ACTION", "Supported actions: DISPATCH, FINISH, STATUS, RESET_ERROR");
        }
    }

    if (ferror(stdin)) {
        fprintf(stderr, "bridge: error reading from stdin\n");
        return 1;
    }

    if (state.current_operation != FLEX_OPERATION_NONE) {
        fprintf(stderr, "bridge: warning: EOF reached with active operation %s (id %llu)\n",
                flex_operation_to_string(state.current_operation),
                (unsigned long long)state.current_operation_id);
    }

    return 0;
}
