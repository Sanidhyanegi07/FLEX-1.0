#include "base64_codec.h"
#include <string.h>
#include <stdint.h>

static const char S_BASE64_STD[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char S_BASE64_URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t flex_base64_encoded_size(size_t src_len) {
    return ((src_len + 2) / 3) * 4 + 1;
}

size_t flex_base64_decoded_size(size_t base64_len) {
    return (base64_len / 4) * 3;
}

FlexBase64Status flex_base64_encode(const unsigned char* src,
                                    size_t src_len,
                                    char* dst,
                                    size_t dst_capacity,
                                    bool url_safe,
                                    size_t* out_written) {
    if (!dst) return FLEX_BASE64_ERR_NULL_ARG;
    if (src_len > 0 && !src) return FLEX_BASE64_ERR_NULL_ARG;
    
    size_t req_size = flex_base64_encoded_size(src_len);
    if (dst_capacity < req_size) return FLEX_BASE64_ERR_BUFFER_TOO_SMALL;
    
    const char* table = url_safe ? S_BASE64_URL : S_BASE64_STD;
    size_t d = 0;
    
    for (size_t i = 0; i < src_len; i += 3) {
        size_t chunk_len = src_len - i;
        unsigned int val = (unsigned int)src[i] << 16;
        if (chunk_len > 1) val |= (unsigned int)src[i + 1] << 8;
        if (chunk_len > 2) val |= (unsigned int)src[i + 2];
        
        dst[d++] = table[(val >> 18) & 0x3F];
        dst[d++] = table[(val >> 12) & 0x3F];
        dst[d++] = (chunk_len > 1) ? table[(val >> 6) & 0x3F] : '=';
        dst[d++] = (chunk_len > 2) ? table[val & 0x3F] : '=';
    }
    
    dst[d] = '\0';
    if (out_written) *out_written = d;
    return FLEX_BASE64_OK;
}

/* Static 256-byte O(1) decode lookup table covering standard and URL-safe Base64 */
static const int8_t S_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, 62, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static inline int decode_char(char c) {
    return (int)S_DECODE_TABLE[(unsigned char)c];
}

FlexBase64Status flex_base64_decode(const char* src,
                                    size_t src_len,
                                    unsigned char* dst,
                                    size_t dst_capacity,
                                    size_t* out_written) {
    if (!src || !dst) return FLEX_BASE64_ERR_NULL_ARG;
    if (src_len == 0) {
        if (out_written) *out_written = 0;
        return FLEX_BASE64_OK;
    }
    
    if (src_len % 4 != 0) return FLEX_BASE64_ERR_INVALID_LENGTH;
    
    size_t d = 0;
    for (size_t i = 0; i < src_len; i += 4) {
        int b0 = decode_char(src[i]);
        int b1 = decode_char(src[i + 1]);
        int b2 = (src[i + 2] == '=') ? 0 : decode_char(src[i + 2]);
        int b3 = (src[i + 3] == '=') ? 0 : decode_char(src[i + 3]);
        
        if (b0 < 0 || b1 < 0 || (src[i + 2] != '=' && b2 < 0) || (src[i + 3] != '=' && b3 < 0)) {
            return FLEX_BASE64_ERR_INVALID_CHAR;
        }
        
        unsigned int val = ((unsigned int)b0 << 18) |
                           ((unsigned int)b1 << 12) |
                           ((unsigned int)b2 << 6)  |
                           (unsigned int)b3;
        
        if (d >= dst_capacity) return FLEX_BASE64_ERR_BUFFER_TOO_SMALL;
        dst[d++] = (unsigned char)((val >> 16) & 0xFF);
        
        if (src[i + 2] != '=') {
            if (d >= dst_capacity) return FLEX_BASE64_ERR_BUFFER_TOO_SMALL;
            dst[d++] = (unsigned char)((val >> 8) & 0xFF);
        }
        if (src[i + 3] != '=') {
            if (d >= dst_capacity) return FLEX_BASE64_ERR_BUFFER_TOO_SMALL;
            dst[d++] = (unsigned char)(val & 0xFF);
        }
    }
    
    if (out_written) *out_written = d;
    return FLEX_BASE64_OK;
}

