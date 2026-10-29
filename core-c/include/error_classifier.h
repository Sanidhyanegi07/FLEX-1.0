#ifndef FLEX_ERROR_CLASSIFIER_H
#define FLEX_ERROR_CLASSIFIER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_ERR_CAT_NONE = 0,
    FLEX_ERR_CAT_CORE = 1,
    FLEX_ERR_CAT_NETWORK = 2,
    FLEX_ERR_CAT_AI_MODEL = 3,
    FLEX_ERR_CAT_STORAGE = 4,
    FLEX_ERR_CAT_AUDIO = 5,
    FLEX_ERR_CAT_VOICE_STT = 6,
    FLEX_ERR_CAT_VOICE_TTS = 7,
    FLEX_ERR_CAT_POLICY = 8,
    FLEX_ERR_CAT_CANCELLED = 9
} flex_error_category_t;

typedef enum {
    FLEX_SEV_INFO = 0,
    FLEX_SEV_WARNING = 1,
    FLEX_SEV_ERROR = 2,
    FLEX_SEV_FATAL = 3
} flex_error_severity_t;

typedef struct {
    flex_error_category_t category;
    int code;
    flex_error_severity_t severity;
    bool is_retryable;
    uint32_t retry_delay_ms;
    int http_status;
    char user_message[256];
    char technical_detail[256];
} flex_error_t;

/**
 * Initializes an empty/successful error structure.
 */
void flex_error_init_success(flex_error_t* error);

/**
 * Classifies an HTTP status code and response body from Ollama or external services.
 */
void flex_classify_http_error(
    int http_status,
    const char* response_body,
    flex_error_t* out_error
);

/**
 * Classifies a C Core return code (from command parser, state manager, or orchestrator).
 */
void flex_classify_core_error(
    int core_status,
    const char* detail,
    flex_error_t* out_error
);

/**
 * Classifies an SQLite database error code and message.
 */
void flex_classify_storage_error(
    int sqlite_code,
    const char* sqlite_message,
    flex_error_t* out_error
);

/**
 * Classifies audio hardware capture, Whisper STT, or SAPI TTS errors.
 */
void flex_classify_voice_error(
    flex_error_category_t subcategory,
    int error_code,
    const char* detail,
    flex_error_t* out_error
);

/**
 * Serializes a flex_error_t to a JSON formatted string in out_buf.
 */
size_t flex_error_to_json(
    const flex_error_t* error,
    char* out_buf,
    size_t buf_cap
);

/**
 * Returns human-readable name for category.
 */
const char* flex_error_category_name(flex_error_category_t category);

/**
 * Returns human-readable name for severity.
 */
const char* flex_error_severity_name(flex_error_severity_t severity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_ERROR_CLASSIFIER_H */

