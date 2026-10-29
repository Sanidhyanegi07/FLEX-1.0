#include "voice_fsm.h"
#include <string.h>
#include <stdio.h>

void flex_voice_fsm_init(flex_voice_fsm_t* fsm) {
    if (!fsm) return;
    memset(fsm, 0, sizeof(flex_voice_fsm_t));
    fsm->state = FLEX_VOICE_IDLE;
}

bool flex_voice_fsm_handle_event(
    flex_voice_fsm_t* fsm,
    flex_voice_event_t event,
    const void* event_data
) {
    if (!fsm) return false;

    if (event == FLEX_VOICE_EVT_RESET) {
        flex_voice_fsm_reset(fsm);
        return true;
    }

    if (event == FLEX_VOICE_EVT_FAIL) {
        fsm->state = FLEX_VOICE_ERROR;
        if (event_data) {
            snprintf(fsm->last_error, sizeof(fsm->last_error), "%.127s", (const char*)event_data);
        }
        return true;
    }

    switch (fsm->state) {
        case FLEX_VOICE_IDLE:
            if (event == FLEX_VOICE_EVT_START_LISTEN) {
                fsm->state = FLEX_VOICE_LISTENING;
                fsm->transcript[0] = '\0';
                fsm->transcript_len = 0;
                fsm->audio_duration_ms = 0;
                fsm->audio_samples_captured = 0;
                fsm->peak_rms = 0.0f;
                fsm->has_submitted_current_transcript = false;
                fsm->last_error[0] = '\0';
                return true;
            }
            break;

        case FLEX_VOICE_LISTENING:
            if (event == FLEX_VOICE_EVT_STOP_LISTEN || event == FLEX_VOICE_EVT_AUDIO_OVERFLOW) {
                fsm->state = FLEX_VOICE_TRANSCRIBING;
                return true;
            }
            break;

        case FLEX_VOICE_TRANSCRIBING:
            if (event == FLEX_VOICE_EVT_TRANSCRIPT_READY) {
                fsm->state = FLEX_VOICE_REVIEWING;
                if (event_data) {
                    snprintf(fsm->transcript, sizeof(fsm->transcript), "%.1023s", (const char*)event_data);
                    fsm->transcript_len = strlen(fsm->transcript);
                }
                return true;
            }
            break;

        case FLEX_VOICE_REVIEWING:
            if (event == FLEX_VOICE_EVT_EDIT_TRANSCRIPT) {
                if (event_data) {
                    return flex_voice_fsm_update_transcript(fsm, (const char*)event_data);
                }
                return true;
            }
            if (event == FLEX_VOICE_EVT_CONFIRM_TRANSCRIPT) {
                if (fsm->has_submitted_current_transcript) {
                    /* Invariant: Cannot submit same transcript twice */
                    return false;
                }
                fsm->state = FLEX_VOICE_THINKING;
                fsm->has_submitted_current_transcript = true;
                return true;
            }
            if (event == FLEX_VOICE_EVT_DISCARD_TRANSCRIPT) {
                fsm->state = FLEX_VOICE_IDLE;
                fsm->transcript[0] = '\0';
                fsm->transcript_len = 0;
                return true;
            }
            break;

        case FLEX_VOICE_THINKING:
            if (event == FLEX_VOICE_EVT_AI_RESPONSE_START) {
                /* AI response is ready */
                return true;
            }
            if (event == FLEX_VOICE_EVT_TTS_PLAYBACK_START) {
                fsm->state = FLEX_VOICE_SPEAKING;
                return true;
            }
            if (event == FLEX_VOICE_EVT_TTS_PLAYBACK_DONE) {
                fsm->state = FLEX_VOICE_IDLE;
                return true;
            }
            break;

        case FLEX_VOICE_SPEAKING:
            if (event == FLEX_VOICE_EVT_TTS_PLAYBACK_DONE) {
                fsm->state = FLEX_VOICE_IDLE;
                return true;
            }
            break;

        case FLEX_VOICE_ERROR:
            if (event == FLEX_VOICE_EVT_RESET || event == FLEX_VOICE_EVT_START_LISTEN) {
                fsm->state = (event == FLEX_VOICE_EVT_START_LISTEN) ? FLEX_VOICE_LISTENING : FLEX_VOICE_IDLE;
                fsm->last_error[0] = '\0';
                return true;
            }
            break;
    }

    return false;
}

bool flex_voice_fsm_update_transcript(
    flex_voice_fsm_t* fsm,
    const char* new_text
) {
    if (!fsm || fsm->state != FLEX_VOICE_REVIEWING || !new_text) {
        return false;
    }
    snprintf(fsm->transcript, sizeof(fsm->transcript), "%.1023s", new_text);
    fsm->transcript_len = strlen(fsm->transcript);
    return true;
}

bool flex_voice_fsm_authorize_submission(
    flex_voice_fsm_t* fsm,
    char* out_confirmed_text,
    size_t out_cap
) {
    if (!fsm || fsm->state != FLEX_VOICE_REVIEWING) {
        return false;
    }

    if (fsm->has_submitted_current_transcript) {
        return false; /* Enforces exactly one submission per review cycle */
    }

    if (fsm->transcript_len == 0) {
        return false; /* Cannot submit empty transcript */
    }

    if (out_confirmed_text && out_cap > 0) {
        snprintf(out_confirmed_text, out_cap, "%.1023s", fsm->transcript);
    }

    fsm->has_submitted_current_transcript = true;
    fsm->state = FLEX_VOICE_THINKING;
    return true;
}

void flex_voice_fsm_reset(flex_voice_fsm_t* fsm) {
    if (!fsm) return;
    fsm->state = FLEX_VOICE_IDLE;
    fsm->transcript[0] = '\0';
    fsm->transcript_len = 0;
    fsm->audio_duration_ms = 0;
    fsm->audio_samples_captured = 0;
    fsm->peak_rms = 0.0f;
    fsm->has_submitted_current_transcript = false;
    fsm->session_turn_id = 0;
    fsm->last_error[0] = '\0';
}

const char* flex_voice_state_name(flex_voice_state_t state) {
    switch (state) {
        case FLEX_VOICE_IDLE: return "IDLE";
        case FLEX_VOICE_LISTENING: return "LISTENING";
        case FLEX_VOICE_TRANSCRIBING: return "TRANSCRIBING";
        case FLEX_VOICE_REVIEWING: return "REVIEWING";
        case FLEX_VOICE_THINKING: return "THINKING";
        case FLEX_VOICE_SPEAKING: return "SPEAKING";
        case FLEX_VOICE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* flex_voice_event_name(flex_voice_event_t event) {
    switch (event) {
        case FLEX_VOICE_EVT_START_LISTEN: return "START_LISTEN";
        case FLEX_VOICE_EVT_STOP_LISTEN: return "STOP_LISTEN";
        case FLEX_VOICE_EVT_AUDIO_OVERFLOW: return "AUDIO_OVERFLOW";
        case FLEX_VOICE_EVT_TRANSCRIPT_READY: return "TRANSCRIPT_READY";
        case FLEX_VOICE_EVT_CONFIRM_TRANSCRIPT: return "CONFIRM_TRANSCRIPT";
        case FLEX_VOICE_EVT_EDIT_TRANSCRIPT: return "EDIT_TRANSCRIPT";
        case FLEX_VOICE_EVT_DISCARD_TRANSCRIPT: return "DISCARD_TRANSCRIPT";
        case FLEX_VOICE_EVT_AI_RESPONSE_START: return "AI_RESPONSE_START";
        case FLEX_VOICE_EVT_TTS_PLAYBACK_START: return "TTS_PLAYBACK_START";
        case FLEX_VOICE_EVT_TTS_PLAYBACK_DONE: return "TTS_PLAYBACK_DONE";
        case FLEX_VOICE_EVT_FAIL: return "FAIL";
        case FLEX_VOICE_EVT_RESET: return "RESET";
        default: return "UNKNOWN";
    }
}

