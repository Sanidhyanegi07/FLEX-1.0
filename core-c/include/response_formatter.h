#ifndef FLEX_RESPONSE_FORMATTER_H
#define FLEX_RESPONSE_FORMATTER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_MD_TEXT = 0,
    FLEX_MD_INLINE_CODE = 1,
    FLEX_MD_CODE_BLOCK = 2
} flex_md_context_t;

typedef struct {
    flex_md_context_t current_context;
    char code_block_lang[32];
    bool in_list;
    uint32_t backtick_streak;
    uint32_t total_code_blocks;
} flex_markdown_tracker_t;

/**
 * Initializes markdown context tracker.
 */
void flex_md_tracker_init(flex_markdown_tracker_t* tracker);

/**
 * Feeds a new token string and updates markdown context (code blocks vs text).
 */
flex_md_context_t flex_md_tracker_feed_token(
    flex_markdown_tracker_t* tracker,
    const char* token,
    size_t token_len
);

/**
 * Escapes HTML characters (&, <, >, ", ') into safe entities.
 */
size_t flex_escape_html_entities(
    const char* raw_text,
    size_t raw_len,
    char* out_escaped,
    size_t out_cap
);

/**
 * Generates a clean single-line snippet for conversation card previews.
 */
size_t flex_format_preview_snippet(
    const char* content,
    char* out_snippet,
    size_t snippet_cap
);

/**
 * Formats epoch millisecond timestamp into a readable date/time string.
 */
bool flex_format_timestamp(
    uint64_t timestamp_ms,
    char* out_str,
    size_t str_cap
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_RESPONSE_FORMATTER_H */

