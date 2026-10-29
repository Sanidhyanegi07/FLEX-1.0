#ifndef FLEX_UNICODE_SUPPORT_H
#define FLEX_UNICODE_SUPPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_UTF8_OK = 0,
    FLEX_UTF8_ERR_NULL_INPUT,
    FLEX_UTF8_ERR_INVALID_START_BYTE,
    FLEX_UTF8_ERR_INCOMPLETE_SEQUENCE,
    FLEX_UTF8_ERR_OVERLONG_ENCODING,
    FLEX_UTF8_ERR_SURROGATE_CODEPOINT,
    FLEX_UTF8_ERR_OUT_OF_RANGE
} FlexUtf8Status;

/**
 * Validate that an entire buffer conforms to RFC 3629 UTF-8 specification.
 * Returns true if 100% valid.
 */
bool flex_utf8_validate(const char* str, size_t len, FlexUtf8Status* out_status, size_t* out_err_offset);

/**
 * Count the number of Unicode codepoints (characters) in a valid UTF-8 string.
 */
size_t flex_utf8_strlen(const char* str, size_t byte_len);

/**
 * Safely truncate a UTF-8 string to a maximum byte length without splitting
 * any multi-byte sequence. Appends null terminator.
 */
size_t flex_utf8_truncate_safe(const char* src, size_t src_len, size_t max_bytes, char* dst, size_t dst_capacity);

/**
 * Decode the next UTF-8 codepoint starting at `src`.
 * Writes codepoint value to `out_codepoint` and returns byte length (1-4), or 0 on error.
 */
size_t flex_utf8_next_codepoint(const char* src, size_t remaining_bytes, uint32_t* out_codepoint);

/**
 * Encode a single 32-bit Unicode codepoint into UTF-8 bytes (up to 4 bytes).
 * Writes bytes into `dst` and returns number of bytes written.
 */
size_t flex_utf8_encode_codepoint(uint32_t codepoint, char* dst, size_t dst_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_UNICODE_SUPPORT_H */

