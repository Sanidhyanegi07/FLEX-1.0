#ifndef FLEX_VOICE_FSM_H
#define FLEX_VOICE_FSM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_VOICE_TRANSCRIPT_MAX 1024

typedef enum {
    FLEX_VOICE_IDLE = 0,
    FLEX_VOICE_LISTENING = 1,
    FLEX_VOICE_TRANSCRIBING = 2,
    FLEX_VOICE_REVIEWING = 3,
    FLEX_VOICE_THINKING = 4,
    FLEX_VOICE_SPEAKING = 5,
    FLEX_VOICE_ERROR = 6
} flex_voice_state_t;

typedef enum {
    FLEX_VOICE_EVT_START_LISTEN = 1,
    FLEX_VOICE_EVT_STOP_LISTEN = 2,
    FLEX_VOICE_EVT_AUDIO_OVERFLOW = 3,
    FLEX_VOICE_EVT_TRANSCRIPT_READY = 4,
    FLEX_VOICE_EVT_CONFIRM_TRANSCRIPT = 5,
    FLEX_VOICE_EVT_EDIT_TRANSCRIPT = 6,
    FLEX_VOICE_EVT_DISCARD_TRANSCRIPT = 7,
    FLEX_VOICE_EVT_AI_RESPONSE_START = 8,
    FLEX_VOICE_EVT_TTS_PLAYBACK_START = 9,
    FLEX_VOICE_EVT_TTS_PLAYBACK_DONE = 10,
    FLEX_VOICE_EVT_FAIL = 11,
    FLEX_VOICE_EVT_RESET = 12
} flex_voice_event_t;

typedef struct {
    flex_voice_state_t state;
    char transcript[FLEX_VOICE_TRANSCRIPT_MAX];
    size_t transcript_len;
    uint32_t audio_duration_ms;
    size_t audio_samples_captured;
    float peak_rms;
    bool has_submitted_current_transcript;
    uint64_t session_turn_id;
    char last_error[128];
} flex_voice_fsm_t;

/**
 * Initializes the voice finite state machine.
 */
void flex_voice_fsm_init(flex_voice_fsm_t* fsm);

/**
 * Processes a voice lifecycle event.
 * Returns true if the transition was valid and accepted, false if rejected.
 */
bool flex_voice_fsm_handle_event(
    flex_voice_fsm_t* fsm,
    flex_voice_event_t event,
    const void* event_data
);

/**
 * Updates the editable transcript buffer while in REVIEWING state.
 */
bool flex_voice_fsm_update_transcript(
    flex_voice_fsm_t* fsm,
    const char* new_text
);

/**
 * Attempts to authorize backend AI submission for the confirmed transcript.
 * Guaranteed to succeed AT MOST ONCE per review cycle.
 */
bool flex_voice_fsm_authorize_submission(
    flex_voice_fsm_t* fsm,
    char* out_confirmed_text,
    size_t out_cap
);

/**
 * Resets voice state machine to IDLE.
 */
void flex_voice_fsm_reset(flex_voice_fsm_t* fsm);

/**
 * Returns string name of voice state.
 */
const char* flex_voice_state_name(flex_voice_state_t state);

/**
 * Returns string name of voice event.
 */
const char* flex_voice_event_name(flex_voice_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_VOICE_FSM_H */

