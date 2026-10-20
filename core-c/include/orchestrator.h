#ifndef FLEX_ORCHESTRATOR_H
#define FLEX_ORCHESTRATOR_H

#include "command_parser.h"
#include "state_manager.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Result returned by the command-state orchestrator.
 */
typedef struct {
    FlexCommandStatus parser_status;    /**< Status returned by the command parser */
    FlexStateStatus state_status;       /**< Status returned by the state manager */
    FlexCommandType command_type;       /**< Command type parsed from input */
    FlexOperation operation;            /**< Operation corresponding to command (or NONE) */
    uint64_t operation_id;              /**< Monotonic ID assigned if operation started, 0 otherwise */
    bool operation_started;             /**< True if an operation was successfully started */
    bool shutdown_requested;            /**< True if shutdown was requested (e.g. exit command) */
    FlexCommandResult command_result;   /**< Full parsed command result, including arguments */
} FlexDispatchResult;

typedef FlexDispatchResult FlexOrchestratorResult;

/**
 * Dispatches a raw command-line string against a caller-owned state manager.
 *
 * 1. Parses the command-line string using flex_parse_command_result().
 * 2. If parsing fails, returns without altering the state manager.
 * 3. If parsing succeeds and command is CHAT, VOICE, or MEMORY, attempts to begin the operation.
 * 4. If command is EXIT, requests shutdown on the state manager.
 * 5. Does not automatically finish active operations.
 */
FlexDispatchResult flex_dispatch_command(FlexStateManager *state, const char *command_line);
FlexDispatchResult flex_orchestrator_dispatch(FlexStateManager *state, const char *command_line);

/**
 * Explicitly finishes the active operation on the state manager.
 */
FlexStateStatus flex_dispatch_finish_operation(FlexStateManager *state);
FlexStateStatus flex_orchestrator_finish(FlexStateManager *state);

#endif /* FLEX_ORCHESTRATOR_H */
