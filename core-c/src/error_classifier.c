#include "error_classifier.h"
#include <string.h>
#include <stdio.h>

void flex_error_init_success(flex_error_t* error) {
    if (!error) return;
    memset(error, 0, sizeof(flex_error_t));
    error->category = FLEX_ERR_CAT_NONE;
    error->code = 0;
    error->severity = FLEX_SEV_INFO;
    error->is_retryable = false;
    error->retry_delay_ms = 0;
    error->http_status = 200;
    strncpy(error->user_message, "Operation successful", sizeof(error->user_message) - 1);
}

void flex_classify_http_error(
    int http_status,
    const char* response_body,
    flex_error_t* out_error
) {
    if (!out_error) return;
    memset(out_error, 0, sizeof(flex_error_t));
    out_error->http_status = http_status;
    out_error->code = http_status;

    if (response_body && *response_body) {
        snprintf(out_error->technical_detail, sizeof(out_error->technical_detail), "%.250s", response_body);
    }

    switch (http_status) {
        case 200:
        case 201:
        case 204:
            out_error->category = FLEX_ERR_CAT_NONE;
            out_error->severity = FLEX_SEV_INFO;
            strncpy(out_error->user_message, "Success", sizeof(out_error->user_message) - 1);
            break;

        case 400:
            out_error->category = FLEX_ERR_CAT_AI_MODEL;
            out_error->severity = FLEX_SEV_ERROR;
            out_error->is_retryable = false;
            strncpy(out_error->user_message, "Invalid request format sent to local AI model.", sizeof(out_error->user_message) - 1);
            break;

        case 404:
            out_error->category = FLEX_ERR_CAT_AI_MODEL;
            out_error->severity = FLEX_SEV_ERROR;
            out_error->is_retryable = false;
            strncpy(out_error->user_message, "Requested AI model was not found on local Ollama service. Run 'ollama pull <model>' to download it.", sizeof(out_error->user_message) - 1);
            break;

        case 408:
            out_error->category = FLEX_ERR_CAT_NETWORK;
            out_error->severity = FLEX_SEV_WARNING;
            out_error->is_retryable = true;
            out_error->retry_delay_ms = 1000;
            strncpy(out_error->user_message, "AI generation request timed out. Retrying may succeed.", sizeof(out_error->user_message) - 1);
            break;

        case 429:
            out_error->category = FLEX_ERR_CAT_POLICY;
            out_error->severity = FLEX_SEV_WARNING;
            out_error->is_retryable = true;
            out_error->retry_delay_ms = 3000;
            strncpy(out_error->user_message, "Rate limit reached. Please wait a moment before sending another prompt.", sizeof(out_error->user_message) - 1);
            break;

        case 500:
            out_error->category = FLEX_ERR_CAT_AI_MODEL;
            out_error->severity = FLEX_SEV_ERROR;
            out_error->is_retryable = true;
            out_error->retry_delay_ms = 2000;
            strncpy(out_error->user_message, "Internal engine error inside Ollama daemon.", sizeof(out_error->user_message) - 1);
            break;

        case 502:
        case 503:
        case 504:
            out_error->category = FLEX_ERR_CAT_NETWORK;
            out_error->severity = FLEX_SEV_ERROR;
            out_error->is_retryable = true;
            out_error->retry_delay_ms = 2000;
            strncpy(out_error->user_message, "Cannot connect to local Ollama service. Ensure 'ollama serve' is running at http://127.0.0.1:11434.", sizeof(out_error->user_message) - 1);
            break;

        default:
            if (http_status <= 0) {
                out_error->category = FLEX_ERR_CAT_NETWORK;
                out_error->severity = FLEX_SEV_ERROR;
                out_error->is_retryable = true;
                out_error->retry_delay_ms = 2000;
                strncpy(out_error->user_message, "Network connection refused: Ollama daemon is unreachable.", sizeof(out_error->user_message) - 1);
            } else {
                out_error->category = FLEX_ERR_CAT_NETWORK;
                out_error->severity = FLEX_SEV_ERROR;
                out_error->is_retryable = false;
                snprintf(out_error->user_message, sizeof(out_error->user_message), "HTTP error %d encountered during inference.", http_status);
            }
            break;
    }
}

void flex_classify_core_error(
    int core_status,
    const char* detail,
    flex_error_t* out_error
) {
    if (!out_error) return;
    memset(out_error, 0, sizeof(flex_error_t));
    out_error->category = FLEX_ERR_CAT_CORE;
    out_error->code = core_status;

    if (detail && *detail) {
        snprintf(out_error->technical_detail, sizeof(out_error->technical_detail), "%.250s", detail);
    }

    switch (core_status) {
        case 0:
            out_error->category = FLEX_ERR_CAT_NONE;
            out_error->severity = FLEX_SEV_INFO;
            strncpy(out_error->user_message, "Core operation completed successfully.", sizeof(out_error->user_message) - 1);
            break;

        case 1: /* Null or invalid pointer */
            out_error->severity = FLEX_SEV_ERROR;
            strncpy(out_error->user_message, "Invalid null parameter supplied to C core engine.", sizeof(out_error->user_message) - 1);
            break;

        case 2: /* Empty input */
            out_error->severity = FLEX_SEV_WARNING;
            strncpy(out_error->user_message, "Input was empty or contained only whitespace.", sizeof(out_error->user_message) - 1);
            break;

        case 3: /* Input too long */
            out_error->category = FLEX_ERR_CAT_POLICY;
            out_error->severity = FLEX_SEV_WARNING;
            strncpy(out_error->user_message, "Input exceeded the maximum bounded limit of 1024 bytes.", sizeof(out_error->user_message) - 1);
            break;

        case 4: /* Invalid command */
            out_error->severity = FLEX_SEV_WARNING;
            strncpy(out_error->user_message, "Unrecognized command verb.", sizeof(out_error->user_message) - 1);
            break;

        case 5: /* Invalid state transition */
            out_error->severity = FLEX_SEV_ERROR;
            strncpy(out_error->user_message, "Cannot dispatch command: Core engine is currently busy.", sizeof(out_error->user_message) - 1);
            break;

        default:
            out_error->severity = FLEX_SEV_ERROR;
            snprintf(out_error->user_message, sizeof(out_error->user_message), "C Core reported status code %d.", core_status);
            break;
    }
}

void flex_classify_storage_error(
    int sqlite_code,
    const char* sqlite_message,
    flex_error_t* out_error
) {
    if (!out_error) return;
    memset(out_error, 0, sizeof(flex_error_t));
    out_error->category = FLEX_ERR_CAT_STORAGE;
    out_error->code = sqlite_code;

    if (sqlite_message && *sqlite_message) {
        snprintf(out_error->technical_detail, sizeof(out_error->technical_detail), "SQLite code %d: %.230s", sqlite_code, sqlite_message);
    }

    switch (sqlite_code) {
        case 0: /* SQLITE_OK */
            out_error->category = FLEX_ERR_CAT_NONE;
            out_error->severity = FLEX_SEV_INFO;
            strncpy(out_error->user_message, "Database operation successful.", sizeof(out_error->user_message) - 1);
            break;

        case 5: /* SQLITE_BUSY */
        case 6: /* SQLITE_LOCKED */
            out_error->severity = FLEX_SEV_WARNING;
            out_error->is_retryable = true;
            out_error->retry_delay_ms = 500;
            strncpy(out_error->user_message, "Database is temporarily locked by another task. Retrying...", sizeof(out_error->user_message) - 1);
            break;

        case 11: /* SQLITE_CORRUPT */
            out_error->severity = FLEX_SEV_FATAL;
            out_error->is_retryable = false;
            strncpy(out_error->user_message, "Conversation database file is corrupted and will be safely isolated.", sizeof(out_error->user_message) - 1);
            break;

        case 13: /* SQLITE_FULL */
            out_error->severity = FLEX_SEV_FATAL;
            out_error->is_retryable = false;
            strncpy(out_error->user_message, "Disk is full. Cannot save conversation messages.", sizeof(out_error->user_message) - 1);
            break;

        case 14: /* SQLITE_CANTOPEN */
            out_error->severity = FLEX_SEV_ERROR;
            out_error->is_retryable = false;
            strncpy(out_error->user_message, "Unable to open conversation database file. Check directory permissions.", sizeof(out_error->user_message) - 1);
            break;

        case 19: /* SQLITE_CONSTRAINT */
            out_error->severity = FLEX_SEV_ERROR;
            out_error->is_retryable = false;
            strncpy(out_error->user_message, "Database constraint violation (foreign key or unique sequence).", sizeof(out_error->user_message) - 1);
            break;

        default:
            out_error->severity = FLEX_SEV_ERROR;
            snprintf(out_error->user_message, sizeof(out_error->user_message), "SQLite error code %d occurred during database access.", sqlite_code);
            break;
    }
}

void flex_classify_voice_error(
    flex_error_category_t subcategory,
    int error_code,
    const char* detail,
    flex_error_t* out_error
) {
    if (!out_error) return;
    memset(out_error, 0, sizeof(flex_error_t));
    out_error->category = subcategory;
    out_error->code = error_code;

    if (detail && *detail) {
        snprintf(out_error->technical_detail, sizeof(out_error->technical_detail), "%.250s", detail);
    }

    if (subcategory == FLEX_ERR_CAT_AUDIO) {
        out_error->severity = FLEX_SEV_ERROR;
        out_error->is_retryable = true;
        out_error->retry_delay_ms = 1000;
        if (error_code == 1) {
            strncpy(out_error->user_message, "No active audio input device detected. Check Windows sound settings.", sizeof(out_error->user_message) - 1);
        } else if (error_code == 2) {
            strncpy(out_error->user_message, "Microphone access blocked. Enable microphone permissions in Windows Settings.", sizeof(out_error->user_message) - 1);
        } else {
            strncpy(out_error->user_message, "WASAPI audio capture failure.", sizeof(out_error->user_message) - 1);
        }
    } else if (subcategory == FLEX_ERR_CAT_VOICE_STT) {
        out_error->severity = FLEX_SEV_ERROR;
        out_error->is_retryable = false;
        if (error_code == 1) {
            strncpy(out_error->user_message, "Whisper model not found. Run scripts/download_whisper_model.ps1 to download ggml-tiny.en.bin.", sizeof(out_error->user_message) - 1);
        } else if (error_code == 2) {
            out_error->severity = FLEX_SEV_INFO;
            strncpy(out_error->user_message, "No speech recognized (transcription was empty).", sizeof(out_error->user_message) - 1);
        } else {
            strncpy(out_error->user_message, "Whisper speech-to-text inference failure.", sizeof(out_error->user_message) - 1);
        }
    } else if (subcategory == FLEX_ERR_CAT_VOICE_TTS) {
        out_error->severity = FLEX_SEV_WARNING;
        out_error->is_retryable = false;
        strncpy(out_error->user_message, "Windows SAPI text-to-speech synthesis failed.", sizeof(out_error->user_message) - 1);
    } else {
        out_error->severity = FLEX_SEV_ERROR;
        strncpy(out_error->user_message, "Voice subsystem encountered an unclassified error.", sizeof(out_error->user_message) - 1);
    }
}

size_t flex_error_to_json(
    const flex_error_t* error,
    char* out_buf,
    size_t buf_cap
) {
    if (!error || !out_buf || buf_cap < 32) return 0;

    int written = snprintf(
        out_buf, buf_cap,
        "{\"category\":\"%s\",\"code\":%d,\"severity\":\"%s\",\"retryable\":%s,\"http_status\":%d,\"message\":\"%s\",\"detail\":\"%s\"}",
        flex_error_category_name(error->category),
        error->code,
        flex_error_severity_name(error->severity),
        error->is_retryable ? "true" : "false",
        error->http_status,
        error->user_message,
        error->technical_detail
    );

    if (written < 0 || (size_t)written >= buf_cap) {
        out_buf[buf_cap - 1] = '\0';
        return buf_cap - 1;
    }
    return (size_t)written;
}

const char* flex_error_category_name(flex_error_category_t category) {
    switch (category) {
        case FLEX_ERR_CAT_NONE: return "NONE";
        case FLEX_ERR_CAT_CORE: return "CORE";
        case FLEX_ERR_CAT_NETWORK: return "NETWORK";
        case FLEX_ERR_CAT_AI_MODEL: return "AI_MODEL";
        case FLEX_ERR_CAT_STORAGE: return "STORAGE";
        case FLEX_ERR_CAT_AUDIO: return "AUDIO";
        case FLEX_ERR_CAT_VOICE_STT: return "VOICE_STT";
        case FLEX_ERR_CAT_VOICE_TTS: return "VOICE_TTS";
        case FLEX_ERR_CAT_POLICY: return "POLICY";
        case FLEX_ERR_CAT_CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

const char* flex_error_severity_name(flex_error_severity_t severity) {
    switch (severity) {
        case FLEX_SEV_INFO: return "INFO";
        case FLEX_SEV_WARNING: return "WARNING";
        case FLEX_SEV_ERROR: return "ERROR";
        case FLEX_SEV_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

