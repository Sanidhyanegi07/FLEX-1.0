#ifndef FLEX_JSON_VALIDATOR_H
#define FLEX_JSON_VALIDATOR_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_JSON_MAX_DEPTH 32
#define FLEX_JSON_MAX_KEY_LEN 64
#define FLEX_JSON_MAX_VALUE_LEN 256

typedef enum {
    FLEX_JSON_OK = 0,
    FLEX_JSON_ERR_NULL_INPUT,
    FLEX_JSON_ERR_EMPTY_INPUT,
    FLEX_JSON_ERR_UNEXPECTED_CHAR,
    FLEX_JSON_ERR_UNTERMINATED_STRING,
    FLEX_JSON_ERR_UNTERMINATED_OBJECT,
    FLEX_JSON_ERR_UNTERMINATED_ARRAY,
    FLEX_JSON_ERR_EXPECTED_COLON,
    FLEX_JSON_ERR_EXPECTED_COMMA,
    FLEX_JSON_ERR_INVALID_LITERAL,
    FLEX_JSON_ERR_INVALID_NUMBER,
    FLEX_JSON_ERR_MAX_DEPTH_EXCEEDED
} FlexJsonStatus;

typedef enum {
    FLEX_JSON_VAL_UNKNOWN = 0,
    FLEX_JSON_VAL_OBJECT,
    FLEX_JSON_VAL_ARRAY,
    FLEX_JSON_VAL_STRING,
    FLEX_JSON_VAL_NUMBER,
    FLEX_JSON_VAL_BOOL,
    FLEX_JSON_VAL_NULL
} FlexJsonValueType;

typedef struct {
    FlexJsonStatus status;
    size_t error_offset;
    size_t line;
    size_t column;
    const char* message;
} FlexJsonError;

/**
 * Validate syntax of a JSON document string.
 */
bool flex_json_validate(const char* json_str, size_t len, FlexJsonError* out_error);

/**
 * Extract a string value for a top-level key from a JSON object.
 * Copies unescaped value into out_buf (null terminated).
 */
bool flex_json_get_string(const char* json_str,
                          size_t len,
                          const char* key,
                          char* out_buf,
                          size_t out_buf_len);

/**
 * Extract an integer value for a top-level key from a JSON object.
 */
bool flex_json_get_int(const char* json_str,
                       size_t len,
                       const char* key,
                       long long* out_val);

/**
 * Extract a boolean value for a top-level key from a JSON object.
 */
bool flex_json_get_bool(const char* json_str,
                        size_t len,
                        const char* key,
                        bool* out_val);

/**
 * Determine the type of the root JSON value (OBJECT, ARRAY, STRING, etc.).
 */
FlexJsonValueType flex_json_root_type(const char* json_str, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_JSON_VALIDATOR_H */
