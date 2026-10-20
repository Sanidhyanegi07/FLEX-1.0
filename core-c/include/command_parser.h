#ifndef FLEX_COMMAND_PARSER_H
#define FLEX_COMMAND_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#define FLEX_MAX_INPUT_LEN   1024
#define FLEX_MAX_COMMAND_LEN 64
#define FLEX_MAX_ARGS_LEN    1024

typedef enum {
    FLEX_COMMAND_UNKNOWN = 0,
    FLEX_COMMAND_CHAT,
    FLEX_COMMAND_VOICE,
    FLEX_COMMAND_MEMORY,
    FLEX_COMMAND_EXIT
} FlexCommandType;

typedef enum {
    FLEX_STATUS_OK = 0,             /**< Recognized command successfully parsed */
    FLEX_STATUS_UNKNOWN_COMMAND,    /**< Command verb not recognized */
    FLEX_STATUS_EMPTY_INPUT,        /**< Input is empty or whitespace-only */
    FLEX_STATUS_INVALID_INPUT,      /**< NULL pointer input */
    FLEX_STATUS_INPUT_TOO_LONG      /**< Input exceeds FLEX_MAX_INPUT_LEN */
} FlexCommandStatus;

typedef struct {
    FlexCommandType type;               /**< Recognized command type or UNKNOWN */
    FlexCommandStatus status;           /**< Status code of the parse operation */
    char name[FLEX_MAX_COMMAND_LEN];    /**< Normalized (lowercase) command name */
    char args[FLEX_MAX_ARGS_LEN];       /**< Parsed command argument string */
    size_t args_len;                    /**< Length of arguments string */
    bool has_args;                      /**< True if arguments are present */
} FlexCommandResult;

typedef FlexCommandResult FlexCommand;

/* Backward-compatible Stage 1A parser */
FlexCommandType flex_parse_command(const char *input);

/* Stage 1B structured parser */
FlexCommandResult flex_parse_command_result(const char *input);
FlexCommandResult flex_parse_command_line(const char *input);
FlexCommandStatus flex_parse_command_ex(const char *input, FlexCommandResult *out_result);

/* String helper functions */
const char *flex_command_type_to_string(FlexCommandType type);
const char *flex_command_status_to_string(FlexCommandStatus status);

#endif /* FLEX_COMMAND_PARSER_H */

