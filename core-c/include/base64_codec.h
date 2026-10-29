#ifndef FLEX_BASE64_CODEC_H
#define FLEX_BASE64_CODEC_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_BASE64_OK = 0,
    FLEX_BASE64_ERR_NULL_ARG,
    FLEX_BASE64_ERR_BUFFER_TOO_SMALL,
    FLEX_BASE64_ERR_INVALID_CHAR,
    FLEX_BASE64_ERR_INVALID_LENGTH
} FlexBase64Status;

/**
 * Calculate the required buffer size to encode `src_len` bytes into Base64 (including null terminator).
 */
size_t flex_base64_encoded_size(size_t src_len);

/**
 * Calculate maximum decoded size for `base64_len` characters.
 */
size_t flex_base64_decoded_size(size_t base64_len);

/**
 * Encode raw binary bytes into Base64 string.
 * If url_safe is true, uses '-' and '_' instead of '+' and '/'.
 */
FlexBase64Status flex_base64_encode(const unsigned char* src,
                                    size_t src_len,
                                    char* dst,
                                    size_t dst_capacity,
                                    bool url_safe,
                                    size_t* out_written);

/**
 * Decode Base64 string into raw binary bytes.
 */
FlexBase64Status flex_base64_decode(const char* src,
                                    size_t src_len,
                                    unsigned char* dst,
                                    size_t dst_capacity,
                                    size_t* out_written);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_BASE64_CODEC_H */

