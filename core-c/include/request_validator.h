#ifndef FLEX_REQUEST_VALIDATOR_H
#define FLEX_REQUEST_VALIDATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_REQ_MAX_PROMPT_LEN 1024
#define FLEX_REQ_MAX_TITLE_LEN 128
#define FLEX_REQ_MAX_ROLE_LEN 16

typedef enum {
    FLEX_REQ_VALID = 0,
    FLEX_REQ_ERR_NULL_PTR = 1,
    FLEX_REQ_ERR_EMPTY = 2,
    FLEX_REQ_ERR_TOO_LONG = 3,
    FLEX_REQ_ERR_INVALID_UTF8 = 4,
    FLEX_REQ_ERR_HARMFUL_CTRL_CHARS = 5,
    FLEX_REQ_ERR_SUSPICIOUS_PATTERN = 6,
    FLEX_REQ_ERR_INVALID_ROLE = 7,
    FLEX_REQ_ERR_BUFFER_TOO_SMALL = 8
} flex_req_status_t;

typedef struct {
    flex_req_status_t status;
    size_t error_offset;
    size_t sanitized_length;
    size_t estimated_tokens;
    bool has_suspicious_patterns;
    char error_detail[128];
} flex_prompt_validation_result_t;

/**
 * Validates a user prompt or command string.
 * Enforces bounded length (<= max_len <= FLEX_REQ_MAX_PROMPT_LEN),
 * validates UTF-8 well-formedness, checks control characters, and detects injection patterns.
 */
flex_req_status_t flex_validate_chat_prompt(
    const char* prompt,
    size_t max_len,
    flex_prompt_validation_result_t* out_result
);

/**
 * Validates whether a message role string is strictly one of:
 * "user", "assistant", "system", "tool".
 */
bool flex_validate_role(const char* role);

/**
 * Validates and sanitizes a conversation title.
 * Strips control characters, normalizes whitespace, and truncates to out_cap.
 */
flex_req_status_t flex_validate_conversation_title(
    const char* raw_title,
    char* out_sanitized,
    size_t out_cap
);

/**
 * Estimates token count using a deterministic fast heuristic
 * (word boundaries, punctuation, numbers, and subword length approximations).
 */
size_t flex_estimate_token_count(const char* text);

/**
 * Validates whether a byte sequence is strictly valid UTF-8.
 * Returns true if valid, false otherwise. If false, out_error_offset receives bad byte index.
 */
bool flex_is_valid_utf8(const char* text, size_t len, size_t* out_error_offset);

/**
 * Converts a validation status code to a human-readable string.
 */
const char* flex_req_status_to_string(flex_req_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_REQUEST_VALIDATOR_H */

