#include "unicode_support.h"
#include <string.h>

size_t flex_utf8_next_codepoint(const char* src, size_t remaining_bytes, uint32_t* out_codepoint) {
    if (!src || remaining_bytes == 0) return 0;
    
    unsigned char b0 = (unsigned char)src[0];
    
    /* 1-byte ASCII (0x00 .. 0x7F) */
    if (b0 <= 0x7F) {
        if (out_codepoint) *out_codepoint = b0;
        return 1;
    }
    
    /* 2-byte sequence (0xC2 .. 0xDF) */
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        if (remaining_bytes < 2) return 0;
        unsigned char b1 = (unsigned char)src[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        
        uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        if (out_codepoint) *out_codepoint = cp;
        return 2;
    }
    
    /* 3-byte sequence (0xE0 .. 0xEF) */
    if (b0 >= 0xE0 && b0 <= 0xEF) {
        if (remaining_bytes < 3) return 0;
        unsigned char b1 = (unsigned char)src[1];
        unsigned char b2 = (unsigned char)src[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
        
        /* Overlong and surrogate checks */
        if (b0 == 0xE0 && b1 < 0xA0) return 0;
        if (b0 == 0xED && b1 > 0x9F) return 0; /* Surrogates U+D800..U+DFFF */
        
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) |
                      ((uint32_t)(b1 & 0x3F) << 6)  |
                      (uint32_t)(b2 & 0x3F);
        if (out_codepoint) *out_codepoint = cp;
        return 3;
    }
    
    /* 4-byte sequence (0xF0 .. 0xF4) */
    if (b0 >= 0xF0 && b0 <= 0xF4) {
        if (remaining_bytes < 4) return 0;
        unsigned char b1 = (unsigned char)src[1];
        unsigned char b2 = (unsigned char)src[2];
        unsigned char b3 = (unsigned char)src[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
        
        /* Overlong and max codepoint U+10FFFF checks */
        if (b0 == 0xF0 && b1 < 0x90) return 0;
        if (b0 == 0xF4 && b1 > 0x8F) return 0;
        
        uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) |
                      ((uint32_t)(b1 & 0x3F) << 12) |
                      ((uint32_t)(b2 & 0x3F) << 6)  |
                      (uint32_t)(b3 & 0x3F);
        if (out_codepoint) *out_codepoint = cp;
        return 4;
    }
    
    return 0;
}

bool flex_utf8_validate(const char* str, size_t len, FlexUtf8Status* out_status, size_t* out_err_offset) {
    if (!str) {
        if (out_status) *out_status = FLEX_UTF8_ERR_NULL_INPUT;
        if (out_err_offset) *out_err_offset = 0;
        return false;
    }
    
    size_t i = 0;
    while (i < len) {
        uint32_t cp = 0;
        size_t n = flex_utf8_next_codepoint(&str[i], len - i, &cp);
        if (n == 0) {
            if (out_status) *out_status = FLEX_UTF8_ERR_INVALID_START_BYTE;
            if (out_err_offset) *out_err_offset = i;
            return false;
        }
        i += n;
    }
    
    if (out_status) *out_status = FLEX_UTF8_OK;
    if (out_err_offset) *out_err_offset = 0;
    return true;
}

size_t flex_utf8_strlen(const char* str, size_t byte_len) {
    if (!str || byte_len == 0) return 0;
    
    size_t count = 0;
    size_t i = 0;
    while (i < byte_len) {
        uint32_t cp = 0;
        size_t n = flex_utf8_next_codepoint(&str[i], byte_len - i, &cp);
        if (n == 0) {
            /* Skip 1 invalid byte */
            i++;
        } else {
            i += n;
        }
        count++;
    }
    return count;
}

size_t flex_utf8_truncate_safe(const char* src, size_t src_len, size_t max_bytes, char* dst, size_t dst_capacity) {
    if (!src || !dst || dst_capacity == 0) return 0;
    
    size_t limit = (src_len < max_bytes) ? src_len : max_bytes;
    if (limit >= dst_capacity) {
        limit = dst_capacity - 1;
    }
    
    size_t valid_bytes = 0;
    size_t i = 0;
    while (i < limit) {
        uint32_t cp = 0;
        size_t n = flex_utf8_next_codepoint(&src[i], limit - i, &cp);
        if (n == 0 || i + n > limit) {
            break;
        }
        i += n;
        valid_bytes = i;
    }
    
    if (valid_bytes > 0) {
        memcpy(dst, src, valid_bytes);
    }
    dst[valid_bytes] = '\0';
    return valid_bytes;
}

size_t flex_utf8_encode_codepoint(uint32_t codepoint, char* dst, size_t dst_capacity) {
    if (!dst || dst_capacity == 0) return 0;
    
    if (codepoint <= 0x7F) {
        if (dst_capacity < 1) return 0;
        dst[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        if (dst_capacity < 2) return 0;
        dst[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
        dst[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        if (dst_capacity < 3) return 0;
        dst[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
        dst[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        if (dst_capacity < 4) return 0;
        dst[0] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
        dst[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        dst[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        dst[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

