#include "response_formatter.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

void flex_md_tracker_init(flex_markdown_tracker_t* tracker) {
    if (!tracker) return;
    memset(tracker, 0, sizeof(flex_markdown_tracker_t));
    tracker->current_context = FLEX_MD_TEXT;
}

flex_md_context_t flex_md_tracker_feed_token(
    flex_markdown_tracker_t* tracker,
    const char* token,
    size_t token_len
) {
    if (!tracker || !token || token_len == 0) {
        return tracker ? tracker->current_context : FLEX_MD_TEXT;
    }

    for (size_t i = 0; i < token_len; ++i) {
        char c = token[i];
        if (c == '`') {
            tracker->backtick_streak++;
            if (tracker->backtick_streak == 3) {
                if (tracker->current_context == FLEX_MD_CODE_BLOCK) {
                    tracker->current_context = FLEX_MD_TEXT;
                    tracker->code_block_lang[0] = '\0';
                } else {
                    tracker->current_context = FLEX_MD_CODE_BLOCK;
                    tracker->total_code_blocks++;
                }
                tracker->backtick_streak = 0;
            }
        } else {
            if (tracker->backtick_streak == 1) {
                if (tracker->current_context == FLEX_MD_TEXT) {
                    tracker->current_context = FLEX_MD_INLINE_CODE;
                } else if (tracker->current_context == FLEX_MD_INLINE_CODE) {
                    tracker->current_context = FLEX_MD_TEXT;
                }
            }
            tracker->backtick_streak = 0;
        }
    }

    return tracker->current_context;
}

size_t flex_escape_html_entities(
    const char* raw_text,
    size_t raw_len,
    char* out_escaped,
    size_t out_cap
) {
    if (!raw_text || !out_escaped || out_cap < 2) return 0;

    size_t out_idx = 0;
    for (size_t i = 0; i < raw_len && out_idx + 1 < out_cap; ++i) {
        char c = raw_text[i];
        if (c == '&' && out_idx + 5 < out_cap) {
            memcpy(out_escaped + out_idx, "&amp;", 5);
            out_idx += 5;
        } else if (c == '<' && out_idx + 4 < out_cap) {
            memcpy(out_escaped + out_idx, "&lt;", 4);
            out_idx += 4;
        } else if (c == '>' && out_idx + 4 < out_cap) {
            memcpy(out_escaped + out_idx, "&gt;", 4);
            out_idx += 4;
        } else if (c == '"' && out_idx + 6 < out_cap) {
            memcpy(out_escaped + out_idx, "&quot;", 6);
            out_idx += 6;
        } else if (c == '\'' && out_idx + 6 < out_cap) {
            memcpy(out_escaped + out_idx, "&#39;", 5);
            out_idx += 5;
        } else {
            out_escaped[out_idx++] = c;
        }
    }

    out_escaped[out_idx] = '\0';
    return out_idx;
}

size_t flex_format_preview_snippet(
    const char* content,
    char* out_snippet,
    size_t snippet_cap
) {
    if (!content || !out_snippet || snippet_cap < 4) return 0;

    size_t out_idx = 0;
    bool in_whitespace = false;
    bool has_char = false;

    /* Skip leading whitespace */
    const char* p = content;
    while (*p && isspace((unsigned char)*p)) p++;

    while (*p && out_idx + 4 < snippet_cap) {
        char c = *p;
        if (isspace((unsigned char)c)) {
            if (!in_whitespace && has_char) {
                out_snippet[out_idx++] = ' ';
                in_whitespace = true;
            }
        } else if (c != '`' && c != '*' && c != '#') {
            out_snippet[out_idx++] = c;
            in_whitespace = false;
            has_char = true;
        }
        p++;
    }

    if (*p != '\0') {
        out_snippet[out_idx++] = '.';
        out_snippet[out_idx++] = '.';
        out_snippet[out_idx++] = '.';
    }

    out_snippet[out_idx] = '\0';
    return out_idx;
}

bool flex_format_timestamp(
    uint64_t timestamp_ms,
    char* out_str,
    size_t str_cap
) {
    if (!out_str || str_cap < 20) return false;

    time_t raw_sec = (time_t)(timestamp_ms / 1000);
    struct tm* tm_info = localtime(&raw_sec);
    if (!tm_info) {
        snprintf(out_str, str_cap, "Unknown");
        return false;
    }

    strftime(out_str, str_cap, "%Y-%m-%d %H:%M:%S", tm_info);
    return true;
}

