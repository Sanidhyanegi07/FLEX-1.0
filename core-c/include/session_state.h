#ifndef FLEX_SESSION_STATE_H
#define FLEX_SESSION_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_SESSION_MAX_TURNS 64
#define FLEX_SESSION_ID_MAX 64
#define FLEX_SESSION_SNIPPET_MAX 256

typedef enum {
    FLEX_SESSION_STATE_IDLE = 0,
    FLEX_SESSION_STATE_INPUT_VALIDATED = 1,
    FLEX_SESSION_STATE_DISPATCHED = 2,
    FLEX_SESSION_STATE_STREAMING = 3,
    FLEX_SESSION_STATE_COMPLETED = 4,
    FLEX_SESSION_STATE_ERROR = 5,
    FLEX_SESSION_STATE_CANCELLED = 6
} flex_session_phase_t;

typedef struct {
    uint64_t turn_id;
    char role[16];
    char snippet[FLEX_SESSION_SNIPPET_MAX];
    size_t full_content_len;
    size_t token_estimate;
    uint64_t timestamp_ms;
    uint32_t duration_ms;
} flex_session_turn_t;

typedef struct {
    char session_id[FLEX_SESSION_ID_MAX];
    char active_conversation_id[FLEX_SESSION_ID_MAX];
    flex_session_phase_t phase;
    uint64_t next_turn_id;
    size_t turn_count;
    size_t max_context_tokens;
    size_t total_session_tokens;
    flex_session_turn_t turns[FLEX_SESSION_MAX_TURNS];
    uint64_t active_operation_id;
} flex_session_tracker_t;

/**
 * Initializes a session tracker.
 */
void flex_session_init(
    flex_session_tracker_t* tracker,
    const char* session_id,
    const char* conversation_id,
    size_t max_context_tokens
);

/**
 * Validates and records a state transition. Returns true if transition is legal.
 */
bool flex_session_transition(
    flex_session_tracker_t* tracker,
    flex_session_phase_t next_phase
);

/**
 * Records a new message turn into the session tracker.
 */
bool flex_session_record_turn(
    flex_session_tracker_t* tracker,
    const char* role,
    const char* content,
    size_t token_estimate,
    uint32_t duration_ms,
    uint64_t* out_turn_id
);

/**
 * Trims oldest non-system turns if total context tokens exceed limit.
 * Returns number of turns trimmed.
 */
size_t flex_session_trim_to_budget(
    flex_session_tracker_t* tracker,
    size_t max_budget_tokens
);

/**
 * Checks whether session is currently allowed to accept a new prompt.
 */
bool flex_session_can_accept_input(const flex_session_tracker_t* tracker);

/**
 * Resets active operation and returns session phase to IDLE.
 */
void flex_session_reset_to_idle(flex_session_tracker_t* tracker);

/**
 * Returns string name of session phase.
 */
const char* flex_session_phase_name(flex_session_phase_t phase);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_SESSION_STATE_H */

