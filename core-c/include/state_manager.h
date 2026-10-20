#ifndef FLEX_STATE_MANAGER_H
#define FLEX_STATE_MANAGER_H

#include "command_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Application lifecycle states.
 */
typedef enum {
    FLEX_LIFECYCLE_IDLE = 0,        /**< Ready to accept operations */
    FLEX_LIFECYCLE_PROCESSING,      /**< Actively executing an operation */
    FLEX_LIFECYCLE_STOPPING         /**< Shutdown requested; draining or stopped */
} FlexLifecycleState;

typedef FlexLifecycleState FlexLifecycle;

/**
 * Supported active operations.
 */
typedef enum {
    FLEX_OPERATION_NONE = 0,        /**< No operation currently active */
    FLEX_OPERATION_CHAT,            /**< Chat processing */
    FLEX_OPERATION_VOICE,           /**< Voice processing */
    FLEX_OPERATION_MEMORY           /**< Memory processing */
} FlexOperation;

/**
 * State manager status / error codes.
 */
typedef enum {
    FLEX_STATE_OK = 0,                      /**< Operation succeeded */
    FLEX_STATE_ERROR_INVALID_ARGUMENT,      /**< NULL pointer or invalid parameter passed */
    FLEX_STATE_ERROR_INVALID_TRANSITION,    /**< General transition violation */
    FLEX_STATE_ERROR_ALREADY_PROCESSING,    /**< Cannot begin operation while already PROCESSING */
    FLEX_STATE_ERROR_NOT_PROCESSING,        /**< Cannot finish operation when not in PROCESSING state */
    FLEX_STATE_ERROR_STOPPING,              /**< Cannot begin operation while in STOPPING state */
    FLEX_STATE_ERROR_ID_OVERFLOW            /**< Operation ID overflow protection triggered */
} FlexStateStatus;

/**
 * Caller-owned state manager structure.
 * No heap allocation or global mutable state is used.
 */
typedef struct {
    FlexLifecycleState lifecycle;       /**< Current application lifecycle */
    FlexOperation current_operation;    /**< Currently active operation */
    uint64_t current_operation_id;      /**< Monotonically increasing operation ID */
    FlexStateStatus last_error;         /**< Last recorded status or error */
} FlexStateManager;

typedef FlexStateManager FlexState;

/* Lifecycle and Transition Functions */
FlexStateStatus flex_state_init(FlexStateManager *state);
FlexStateStatus flex_state_begin_operation(FlexStateManager *state, FlexOperation operation);
FlexStateStatus flex_state_finish_operation(FlexStateManager *state);
FlexStateStatus flex_state_request_shutdown(FlexStateManager *state);
FlexStateStatus flex_state_reset_error(FlexStateManager *state);

/* Query Functions */
bool flex_state_is_idle(const FlexStateManager *state);
bool flex_state_is_processing(const FlexStateManager *state);
bool flex_state_is_stopping(const FlexStateManager *state);

/* Helper & Conversion Functions */
const char *flex_lifecycle_to_string(FlexLifecycleState lifecycle);
const char *flex_operation_to_string(FlexOperation operation);
const char *flex_state_status_to_string(FlexStateStatus status);
FlexOperation flex_operation_from_command(FlexCommandType command);

#endif /* FLEX_STATE_MANAGER_H */
