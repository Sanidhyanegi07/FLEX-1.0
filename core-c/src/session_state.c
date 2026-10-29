#include "session_state.h"
#include <string.h>
#include <stdio.h>

void flex_session_init(
    flex_session_tracker_t* tracker,
    const char* session_id,
    const char* conversation_id,
    size_t max_context_tokens
) {
    if (!tracker) return;
    memset(tracker, 0, sizeof(flex_session_tracker_t));

    if (session_id) {
        snprintf(tracker->session_id, sizeof(tracker->session_id), "%.63s", session_id);
    } else {
        snprintf(tracker->session_id, sizeof(tracker->session_id), "sess_default");
    }

    if (conversation_id) {
        snprintf(tracker->active_conversation_id, sizeof(tracker->active_conversation_id), "%.63s", conversation_id);
    }

    tracker->phase = FLEX_SESSION_STATE_IDLE;
    tracker->next_turn_id = 1;
    tracker->turn_count = 0;
    tracker->max_context_tokens = (max_context_tokens > 0) ? max_context_tokens : 4096;
    tracker->total_session_tokens = 0;
    tracker->active_operation_id = 0;
}

bool flex_session_transition(
    flex_session_tracker_t* tracker,
    flex_session_phase_t next_phase
) {
    if (!tracker) return false;

    flex_session_phase_t cur = tracker->phase;

    /* Transition Validation Table */
    switch (cur) {
        case FLEX_SESSION_STATE_IDLE:
            if (next_phase == FLEX_SESSION_STATE_INPUT_VALIDATED ||
                next_phase == FLEX_SESSION_STATE_ERROR) {
                tracker->phase = next_phase;
                return true;
            }
            break;

        case FLEX_SESSION_STATE_INPUT_VALIDATED:
            if (next_phase == FLEX_SESSION_STATE_DISPATCHED ||
                next_phase == FLEX_SESSION_STATE_ERROR ||
                next_phase == FLEX_SESSION_STATE_IDLE) {
                tracker->phase = next_phase;
                return true;
            }
            break;

        case FLEX_SESSION_STATE_DISPATCHED:
            if (next_phase == FLEX_SESSION_STATE_STREAMING ||
                next_phase == FLEX_SESSION_STATE_COMPLETED ||
                next_phase == FLEX_SESSION_STATE_ERROR ||
                next_phase == FLEX_SESSION_STATE_CANCELLED) {
                tracker->phase = next_phase;
                return true;
            }
            break;

        case FLEX_SESSION_STATE_STREAMING:
            if (next_phase == FLEX_SESSION_STATE_COMPLETED ||
                next_phase == FLEX_SESSION_STATE_ERROR ||
                next_phase == FLEX_SESSION_STATE_CANCELLED) {
                tracker->phase = next_phase;
                return true;
            }
            break;

        case FLEX_SESSION_STATE_COMPLETED:
        case FLEX_SESSION_STATE_ERROR:
        case FLEX_SESSION_STATE_CANCELLED:
            if (next_phase == FLEX_SESSION_STATE_IDLE ||
                next_phase == FLEX_SESSION_STATE_INPUT_VALIDATED) {
                tracker->phase = next_phase;
                return true;
            }
            break;
    }

    return false;
}

bool flex_session_record_turn(
    flex_session_tracker_t* tracker,
    const char* role,
    const char* content,
    size_t token_estimate,
    uint32_t duration_ms,
    uint64_t* out_turn_id
) {
    if (!tracker || !role) return false;

    if (tracker->turn_count >= FLEX_SESSION_MAX_TURNS) {
        /* Shift older turns down by 1 if not system */
        size_t start_shift = 0;
        if (strcmp(tracker->turns[0].role, "system") == 0) {
            start_shift = 1;
        }

        if (tracker->turn_count > start_shift) {
            tracker->total_session_tokens -= tracker->turns[start_shift].token_estimate;
            for (size_t i = start_shift; i < tracker->turn_count - 1; ++i) {
                tracker->turns[i] = tracker->turns[i + 1];
            }
            tracker->turn_count--;
        }
    }

    size_t idx = tracker->turn_count;
    flex_session_turn_t* t = &tracker->turns[idx];
    memset(t, 0, sizeof(flex_session_turn_t));

    t->turn_id = tracker->next_turn_id++;
    snprintf(t->role, sizeof(t->role), "%.15s", role);

    if (content) {
        t->full_content_len = strlen(content);
        snprintf(t->snippet, sizeof(t->snippet), "%.255s", content);
    }

    t->token_estimate = token_estimate;
    t->duration_ms = duration_ms;

    tracker->turn_count++;
    tracker->total_session_tokens += token_estimate;

    if (out_turn_id) {
        *out_turn_id = t->turn_id;
    }

    /* Auto-trim if exceeding max context budget */
    flex_session_trim_to_budget(tracker, tracker->max_context_tokens);

    return true;
}

size_t flex_session_trim_to_budget(
    flex_session_tracker_t* tracker,
    size_t max_budget_tokens
) {
    if (!tracker || max_budget_tokens == 0) return 0;

    size_t trimmed_count = 0;
    while (tracker->total_session_tokens > max_budget_tokens && tracker->turn_count > 1) {
        size_t drop_idx = 0;
        /* Preserve system prompt if it's the first turn */
        if (strcmp(tracker->turns[0].role, "system") == 0) {
            if (tracker->turn_count <= 2) {
                break; /* Do not drop the last active conversation turn */
            }
            drop_idx = 1;
        }

        tracker->total_session_tokens -= tracker->turns[drop_idx].token_estimate;

        for (size_t i = drop_idx; i < tracker->turn_count - 1; ++i) {
            tracker->turns[i] = tracker->turns[i + 1];
        }
        tracker->turn_count--;
        trimmed_count++;
    }

    return trimmed_count;
}

bool flex_session_can_accept_input(const flex_session_tracker_t* tracker) {
    if (!tracker) return false;
    return (tracker->phase == FLEX_SESSION_STATE_IDLE ||
            tracker->phase == FLEX_SESSION_STATE_COMPLETED ||
            tracker->phase == FLEX_SESSION_STATE_ERROR ||
            tracker->phase == FLEX_SESSION_STATE_CANCELLED);
}

void flex_session_reset_to_idle(flex_session_tracker_t* tracker) {
    if (!tracker) return;
    tracker->phase = FLEX_SESSION_STATE_IDLE;
    tracker->active_operation_id = 0;
}

const char* flex_session_phase_name(flex_session_phase_t phase) {
    switch (phase) {
        case FLEX_SESSION_STATE_IDLE: return "IDLE";
        case FLEX_SESSION_STATE_INPUT_VALIDATED: return "INPUT_VALIDATED";
        case FLEX_SESSION_STATE_DISPATCHED: return "DISPATCHED";
        case FLEX_SESSION_STATE_STREAMING: return "STREAMING";
        case FLEX_SESSION_STATE_COMPLETED: return "COMPLETED";
        case FLEX_SESSION_STATE_ERROR: return "ERROR";
        case FLEX_SESSION_STATE_CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

