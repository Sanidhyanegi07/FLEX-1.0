#include "state_manager.h"

#include <string.h>

FlexStateStatus flex_state_init(FlexStateManager *state)
{
    if (state == NULL) {
        return FLEX_STATE_ERROR_INVALID_ARGUMENT;
    }

    state->lifecycle = FLEX_LIFECYCLE_IDLE;
    state->current_operation = FLEX_OPERATION_NONE;
    state->current_operation_id = 0;
    state->last_error = FLEX_STATE_OK;

    return FLEX_STATE_OK;
}

FlexStateStatus flex_state_begin_operation(FlexStateManager *state, FlexOperation operation)
{
    if (state == NULL) {
        return FLEX_STATE_ERROR_INVALID_ARGUMENT;
    }

    /* Validate operation enum */
    if (operation != FLEX_OPERATION_CHAT &&
        operation != FLEX_OPERATION_VOICE &&
        operation != FLEX_OPERATION_MEMORY) {
        state->last_error = FLEX_STATE_ERROR_INVALID_ARGUMENT;
        return FLEX_STATE_ERROR_INVALID_ARGUMENT;
    }

    /* Cannot begin operation if shutting down */
    if (state->lifecycle == FLEX_LIFECYCLE_STOPPING) {
        state->last_error = FLEX_STATE_ERROR_STOPPING;
        return FLEX_STATE_ERROR_STOPPING;
    }

    /* Cannot begin operation if already processing */
    if (state->lifecycle == FLEX_LIFECYCLE_PROCESSING ||
        state->current_operation != FLEX_OPERATION_NONE) {
        state->last_error = FLEX_STATE_ERROR_ALREADY_PROCESSING;
        return FLEX_STATE_ERROR_ALREADY_PROCESSING;
    }

    /* Overflow protection on operation ID */
    if (state->current_operation_id == UINT64_MAX) {
        state->last_error = FLEX_STATE_ERROR_ID_OVERFLOW;
        return FLEX_STATE_ERROR_ID_OVERFLOW;
    }

    state->lifecycle = FLEX_LIFECYCLE_PROCESSING;
    state->current_operation = operation;
    state->current_operation_id++;
    state->last_error = FLEX_STATE_OK;

    return FLEX_STATE_OK;
}

FlexStateStatus flex_state_finish_operation(FlexStateManager *state)
{
    if (state == NULL) {
        return FLEX_STATE_ERROR_INVALID_ARGUMENT;
    }

    /* Cannot finish if not currently processing */
    if (state->current_operation == FLEX_OPERATION_NONE) {
        state->last_error = FLEX_STATE_ERROR_NOT_PROCESSING;
        return FLEX_STATE_ERROR_NOT_PROCESSING;
    }

    state->current_operation = FLEX_OPERATION_NONE;

    /* If shutdown was requested while processing, remain STOPPING; else return to IDLE */
    if (state->lifecycle != FLEX_LIFECYCLE_STOPPING) {
        state->lifecycle = FLEX_LIFECYCLE_IDLE;
    }

    state->last_error = FLEX_STATE_OK;
    return FLEX_STATE_OK;
}

FlexStateStatus flex_state_request_shutdown(FlexStateManager *state)
{
    if (state == NULL) {
        return FLEX_STATE_ERROR_INVALID_ARGUMENT;
    }

    state->lifecycle = FLEX_LIFECYCLE_STOPPING;
    state->last_error = FLEX_STATE_OK;
    return FLEX_STATE_OK;
}

FlexStateStatus flex_state_reset_error(FlexStateManager *state)
{
    if (state == NULL) {
        return FLEX_STATE_ERROR_INVALID_ARGUMENT;
    }

    state->last_error = FLEX_STATE_OK;
    return FLEX_STATE_OK;
}

bool flex_state_is_idle(const FlexStateManager *state)
{
    if (state == NULL) {
        return false;
    }
    return state->lifecycle == FLEX_LIFECYCLE_IDLE &&
           state->current_operation == FLEX_OPERATION_NONE;
}

bool flex_state_is_processing(const FlexStateManager *state)
{
    if (state == NULL) {
        return false;
    }
    return state->lifecycle == FLEX_LIFECYCLE_PROCESSING;
}

bool flex_state_is_stopping(const FlexStateManager *state)
{
    if (state == NULL) {
        return false;
    }
    return state->lifecycle == FLEX_LIFECYCLE_STOPPING;
}

const char *flex_lifecycle_to_string(FlexLifecycleState lifecycle)
{
    switch (lifecycle) {
    case FLEX_LIFECYCLE_IDLE:
        return "FLEX_LIFECYCLE_IDLE";
    case FLEX_LIFECYCLE_PROCESSING:
        return "FLEX_LIFECYCLE_PROCESSING";
    case FLEX_LIFECYCLE_STOPPING:
        return "FLEX_LIFECYCLE_STOPPING";
    default:
        return "INVALID_LIFECYCLE";
    }
}

const char *flex_operation_to_string(FlexOperation operation)
{
    switch (operation) {
    case FLEX_OPERATION_NONE:
        return "FLEX_OPERATION_NONE";
    case FLEX_OPERATION_CHAT:
        return "FLEX_OPERATION_CHAT";
    case FLEX_OPERATION_VOICE:
        return "FLEX_OPERATION_VOICE";
    case FLEX_OPERATION_MEMORY:
        return "FLEX_OPERATION_MEMORY";
    default:
        return "INVALID_OPERATION";
    }
}

const char *flex_state_status_to_string(FlexStateStatus status)
{
    switch (status) {
    case FLEX_STATE_OK:
        return "FLEX_STATE_OK";
    case FLEX_STATE_ERROR_INVALID_ARGUMENT:
        return "FLEX_STATE_ERROR_INVALID_ARGUMENT";
    case FLEX_STATE_ERROR_INVALID_TRANSITION:
        return "FLEX_STATE_ERROR_INVALID_TRANSITION";
    case FLEX_STATE_ERROR_ALREADY_PROCESSING:
        return "FLEX_STATE_ERROR_ALREADY_PROCESSING";
    case FLEX_STATE_ERROR_NOT_PROCESSING:
        return "FLEX_STATE_ERROR_NOT_PROCESSING";
    case FLEX_STATE_ERROR_STOPPING:
        return "FLEX_STATE_ERROR_STOPPING";
    case FLEX_STATE_ERROR_ID_OVERFLOW:
        return "FLEX_STATE_ERROR_ID_OVERFLOW";
    default:
        return "INVALID_STATUS";
    }
}

FlexOperation flex_operation_from_command(FlexCommandType command)
{
    switch (command) {
    case FLEX_COMMAND_CHAT:
        return FLEX_OPERATION_CHAT;
    case FLEX_COMMAND_VOICE:
        return FLEX_OPERATION_VOICE;
    case FLEX_COMMAND_MEMORY:
        return FLEX_OPERATION_MEMORY;
    default:
        return FLEX_OPERATION_NONE;
    }
}
