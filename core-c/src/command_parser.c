#include "command_parser.h"

#include <ctype.h>
#include <string.h>

const char *flex_command_type_to_string(FlexCommandType type)
{
    switch (type) {
    case FLEX_COMMAND_UNKNOWN:
        return "FLEX_COMMAND_UNKNOWN";
    case FLEX_COMMAND_CHAT:
        return "FLEX_COMMAND_CHAT";
    case FLEX_COMMAND_VOICE:
        return "FLEX_COMMAND_VOICE";
    case FLEX_COMMAND_MEMORY:
        return "FLEX_COMMAND_MEMORY";
    case FLEX_COMMAND_EXIT:
        return "FLEX_COMMAND_EXIT";
    default:
        return "INVALID_TYPE";
    }
}

const char *flex_command_status_to_string(FlexCommandStatus status)
{
    switch (status) {
    case FLEX_STATUS_OK:
        return "FLEX_STATUS_OK";
    case FLEX_STATUS_UNKNOWN_COMMAND:
        return "FLEX_STATUS_UNKNOWN_COMMAND";
    case FLEX_STATUS_EMPTY_INPUT:
        return "FLEX_STATUS_EMPTY_INPUT";
    case FLEX_STATUS_INVALID_INPUT:
        return "FLEX_STATUS_INVALID_INPUT";
    case FLEX_STATUS_INPUT_TOO_LONG:
        return "FLEX_STATUS_INPUT_TOO_LONG";
    default:
        return "INVALID_STATUS";
    }
}

FlexCommandResult flex_parse_command_result(const char *input)
{
    FlexCommandResult result;
    memset(&result, 0, sizeof(result));
    result.type = FLEX_COMMAND_UNKNOWN;

    if (input == NULL) {
        result.status = FLEX_STATUS_INVALID_INPUT;
        return result;
    }

    /* Bounded input scan to prevent reading unbounded memory */
    size_t input_len = 0;
    while (input[input_len] != '\0') {
        input_len++;
        if (input_len > FLEX_MAX_INPUT_LEN) {
            result.status = FLEX_STATUS_INPUT_TOO_LONG;
            return result;
        }
    }

    /* Skip leading whitespace */
    const char *p = input;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0') {
        result.status = FLEX_STATUS_EMPTY_INPUT;
        return result;
    }

    /* Extract command verb */
    const char *cmd_start = p;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    size_t cmd_len = (size_t)(p - cmd_start);

    if (cmd_len >= FLEX_MAX_COMMAND_LEN) {
        /* Command verb exceeds supported length; copy truncated and mark unknown */
        size_t copy_len = FLEX_MAX_COMMAND_LEN - 1;
        for (size_t i = 0; i < copy_len; i++) {
            result.name[i] = (char)tolower((unsigned char)cmd_start[i]);
        }
        result.name[copy_len] = '\0';
        result.status = FLEX_STATUS_UNKNOWN_COMMAND;
        result.type = FLEX_COMMAND_UNKNOWN;
    } else {
        for (size_t i = 0; i < cmd_len; i++) {
            result.name[i] = (char)tolower((unsigned char)cmd_start[i]);
        }
        result.name[cmd_len] = '\0';

        if (strcmp(result.name, "chat") == 0) {
            result.type = FLEX_COMMAND_CHAT;
            result.status = FLEX_STATUS_OK;
        } else if (strcmp(result.name, "voice") == 0) {
            result.type = FLEX_COMMAND_VOICE;
            result.status = FLEX_STATUS_OK;
        } else if (strcmp(result.name, "memory") == 0) {
            result.type = FLEX_COMMAND_MEMORY;
            result.status = FLEX_STATUS_OK;
        } else if (strcmp(result.name, "exit") == 0) {
            result.type = FLEX_COMMAND_EXIT;
            result.status = FLEX_STATUS_OK;
        } else {
            result.type = FLEX_COMMAND_UNKNOWN;
            result.status = FLEX_STATUS_UNKNOWN_COMMAND;
        }
    }

    /* Skip whitespace between command verb and arguments */
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p != '\0') {
        /* Arguments present; trim trailing whitespace */
        const char *arg_start = p;
        const char *arg_end = p + strlen(p);
        while (arg_end > arg_start && isspace((unsigned char)*(arg_end - 1))) {
            arg_end--;
        }

        size_t raw_args_len = (size_t)(arg_end - arg_start);
        if (raw_args_len >= FLEX_MAX_ARGS_LEN) {
            raw_args_len = FLEX_MAX_ARGS_LEN - 1;
        }

        if (raw_args_len > 0) {
            memcpy(result.args, arg_start, raw_args_len);
            result.args[raw_args_len] = '\0';
            result.args_len = raw_args_len;
            result.has_args = true;
        }
    }

    return result;
}

FlexCommandResult flex_parse_command_line(const char *input)
{
    return flex_parse_command_result(input);
}

FlexCommandStatus flex_parse_command_ex(const char *input, FlexCommandResult *out_result)
{
    if (out_result == NULL) {
        return FLEX_STATUS_INVALID_INPUT;
    }

    *out_result = flex_parse_command_result(input);
    return out_result->status;
}

FlexCommandType flex_parse_command(const char *input)
{
    FlexCommandResult result = flex_parse_command_result(input);
    if (result.status == FLEX_STATUS_OK && !result.has_args) {
        return result.type;
    }
    return FLEX_COMMAND_UNKNOWN;
}
