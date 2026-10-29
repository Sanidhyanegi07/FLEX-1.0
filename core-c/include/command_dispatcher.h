#ifndef FLEX_COMMAND_DISPATCHER_H
#define FLEX_COMMAND_DISPATCHER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_DISPATCH_RESP_MAX 1024

typedef enum {
    FLEX_DISPATCH_HANDLED_INTERNAL = 0,
    FLEX_DISPATCH_FORWARD_TO_AI = 1,
    FLEX_DISPATCH_ERR_SYNTAX = 2,
    FLEX_DISPATCH_ERR_POLICY = 3
} flex_dispatch_action_t;

typedef struct {
    flex_dispatch_action_t action;
    char command_verb[32];
    char response_text[FLEX_DISPATCH_RESP_MAX];
    size_t response_len;
    bool modifies_session;
    bool requires_clear_ui;
} flex_dispatch_result_t;

/**
 * Checks whether an input line is a built-in slash command (e.g. /help, /status).
 */
bool flex_is_slash_command(const char* input);

/**
 * Dispatches a slash command or user input through the C core command dispatcher.
 * Executes internal deterministic actions (/help, /status, /calc, /reset, /voice)
 * or indicates that the prompt should proceed to the AI model.
 */
flex_dispatch_action_t flex_dispatch_input(
    const char* raw_input,
    flex_dispatch_result_t* out_result
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_COMMAND_DISPATCHER_H */

