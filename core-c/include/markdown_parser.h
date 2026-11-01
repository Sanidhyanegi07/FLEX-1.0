#ifndef FLEX_MARKDOWN_PARSER_H
#define FLEX_MARKDOWN_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_MD_NODE_TEXT = 0,
    FLEX_MD_NODE_HEADER = 1,
    FLEX_MD_NODE_PARAGRAPH = 2,
    FLEX_MD_NODE_CODE_BLOCK = 3,
    FLEX_MD_NODE_INLINE_CODE = 4,
    FLEX_MD_NODE_BLOCKQUOTE = 5,
    FLEX_MD_NODE_LIST_ITEM = 6,
    FLEX_MD_NODE_HR = 7
} flex_md_node_type_t;

typedef struct {
    flex_md_node_type_t type;
    int header_level;
    char lang[32];
    const char* text_start;
    size_t text_len;
} flex_md_token_t;

/**
 * Converts a markdown document into rich HTML suitable for Qt text widgets.
 * Handles headers, code blocks, bold, italics, quotes, and lists.
 */
size_t flex_markdown_to_html(
    const char* markdown,
    size_t md_len,
    char* out_html,
    size_t html_cap
);

/**
 * Strips all markdown syntax to generate plain clean text.
 */
size_t flex_markdown_strip_to_plain(
    const char* markdown,
    size_t md_len,
    char* out_plain,
    size_t plain_cap
);

/**
 * Extracts all code blocks (fenced by ```) from markdown into an array of buffers.
 */
size_t flex_markdown_extract_code_blocks(
    const char* markdown,
    size_t md_len,
    char out_langs[][32],
    char out_blocks[][512],
    size_t max_blocks
);

/**
 * Extracts document outline headers (#, ##, ###) with their levels and titles.
 */
size_t flex_markdown_extract_outline(
    const char* markdown,
    size_t md_len,
    int out_levels[],
    char out_titles[][128],
    size_t max_headers
);

/**
 * Strips formatting characters from a single markdown inline line.
 */
size_t flex_markdown_clean_inline(
    const char* line,
    size_t line_len,
    char* out_buf,
    size_t out_cap
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_MARKDOWN_PARSER_H */

