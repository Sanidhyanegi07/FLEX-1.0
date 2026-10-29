#include "ansi_escape.h"
#include <string.h>
#include <stdio.h>

bool flex_ansi_has_escapes(const char* text, size_t len) {
    if (!text || len == 0) return false;
    
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '\x1B' && i + 1 < len && text[i + 1] == '[') {
            return true;
        }
    }
    return false;
}

size_t flex_ansi_strip(const char* input, size_t input_len, char* out_buf, size_t out_capacity) {
    if (!out_buf || out_capacity == 0) return 0;
    if (!input || input_len == 0) {
        out_buf[0] = '\0';
        return 0;
    }
    
    size_t out_idx = 0;
    size_t i = 0;
    
    while (i < input_len && out_idx + 1 < out_capacity) {
        if (input[i] == '\x1B' && i + 1 < input_len && input[i + 1] == '[') {
            /* Skip until terminating character ('m', 'H', 'J', etc.) */
            i += 2;
            while (i < input_len && (input[i] < '@' || input[i] > '~')) {
                i++;
            }
            if (i < input_len) {
                i++; /* Skip final command character */
            }
        } else {
            out_buf[out_idx++] = input[i++];
        }
    }
    
    out_buf[out_idx] = '\0';
    return out_idx;
}

size_t flex_ansi_wrap(const char* text, FlexAnsiCode code, char* out_buf, size_t out_capacity) {
    if (!text || !out_buf || out_capacity == 0) return 0;
    
    int written = snprintf(out_buf, out_capacity, "\x1B[%dm%s\x1B[0m", (int)code, text);
    if (written < 0 || (size_t)written >= out_capacity) {
        out_buf[out_capacity - 1] = '\0';
        return out_capacity - 1;
    }
    return (size_t)written;
}

