#include "orchestrator.h"

#include <string.h>

FlexDispatchResult flex_dispatch_command(FlexStateManager *state, const char *command_line)
{
    FlexDispatchResult result;
    memset(&result, 0, sizeof(result));

    /* Validate state pointer */
    if (state == NULL) {
        result.parser_status = FLEX_STATUS_INVALID_INPUT;
        result.state_status = FLEX_STATE_ERROR_INVALID_ARGUMENT;
        result.command_type = FLEX_COMMAND_UNKNOWN;
        result.operation = FLEX_OPERATION_NONE;
        return result;
    }

    /* 1. Parse command line */
    result.command_result = flex_parse_command_result(command_line);
    result.parser_status = result.command_result.status;
    result.command_type = result.command_result.type;
    result.operation = flex_operation_from_command(result.command_type);

    /* 2. If parser did not succeed with recognized command, do not transition state */
    if (result.parser_status != FLEX_STATUS_OK) {
        result.state_status = FLEX_STATE_OK;
        result.operation_started = false;
        result.shutdown_requested = false;
        result.operation_id = 0;
        return result;
    }

    /* 3. Handle EXIT command -> shutdown */
    if (result.command_type == FLEX_COMMAND_EXIT) {
        result.state_status = flex_state_request_shutdown(state);
        result.shutdown_requested = (result.state_status == FLEX_STATE_OK);
        result.operation_started = false;
        result.operation_id = 0;
        return result;
    }

    /* 4. Handle operational commands: CHAT, VOICE, MEMORY */
    if (result.operation != FLEX_OPERATION_NONE) {
        result.state_status = flex_state_begin_operation(state, result.operation);
        if (result.state_status == FLEX_STATE_OK) {
            result.operation_started = true;
            result.operation_id = state->current_operation_id;
        } else {
            result.operation_started = false;
            result.operation_id = 0;
        }
        return result;
    }

    /* Fallback for any other unrecognized command */
    result.state_status = FLEX_STATE_OK;
    result.operation_started = false;
    result.operation_id = 0;
    return result;
}

FlexDispatchResult flex_orchestrator_dispatch(FlexStateManager *state, const char *command_line)
{
    return flex_dispatch_command(state, command_line);
}

FlexStateStatus flex_dispatch_finish_operation(FlexStateManager *state)
{
    return flex_state_finish_operation(state);
}

FlexStateStatus flex_orchestrator_finish(FlexStateManager *state)
{
    return flex_dispatch_finish_operation(state);
}
