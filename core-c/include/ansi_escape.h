#ifndef FLEX_ANSI_ESCAPE_H
#define FLEX_ANSI_ESCAPE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_ANSI_RESET = 0,
    FLEX_ANSI_BOLD = 1,
    FLEX_ANSI_DIM = 2,
    FLEX_ANSI_UNDERLINE = 4,
    FLEX_ANSI_FG_BLACK = 30,
    FLEX_ANSI_FG_RED = 31,
    FLEX_ANSI_FG_GREEN = 32,
    FLEX_ANSI_FG_YELLOW = 33,
    FLEX_ANSI_FG_BLUE = 34,
    FLEX_ANSI_FG_MAGENTA = 35,
    FLEX_ANSI_FG_CYAN = 36,
    FLEX_ANSI_FG_WHITE = 37
} FlexAnsiCode;

/**
 * Strips all ANSI escape sequences (colors, cursor codes) from input text.
 * Writes clean plaintext into out_buf. Appends null terminator.
 */
size_t flex_ansi_strip(const char* input, size_t input_len, char* out_buf, size_t out_capacity);

/**
 * Formats text with an ANSI styling code (e.g. bold cyan) and trailing reset code.
 */
size_t flex_ansi_wrap(const char* text, FlexAnsiCode code, char* out_buf, size_t out_capacity);

/**
 * Checks if a string contains any ANSI escape sequences.
 */
bool flex_ansi_has_escapes(const char* text, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_ANSI_ESCAPE_H */

