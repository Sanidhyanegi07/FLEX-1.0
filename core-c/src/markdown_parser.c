#include "markdown_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static size_t append_str(char* dest, size_t dest_cap, size_t cur_len, const char* src) {
    if (!src || cur_len >= dest_cap) return cur_len;
    while (*src && cur_len + 1 < dest_cap) {
        dest[cur_len++] = *src++;
    }
    dest[cur_len] = '\0';
    return cur_len;
}

static size_t append_escaped_html(char* dest, size_t dest_cap, size_t cur_len, const char* text, size_t len) {
    if (!text || cur_len >= dest_cap) return cur_len;

    for (size_t i = 0; i < len && cur_len + 6 < dest_cap; ++i) {
        char c = text[i];
        if (c == '&') cur_len = append_str(dest, dest_cap, cur_len, "&amp;");
        else if (c == '<') cur_len = append_str(dest, dest_cap, cur_len, "&lt;");
        else if (c == '>') cur_len = append_str(dest, dest_cap, cur_len, "&gt;");
        else if (c == '"') cur_len = append_str(dest, dest_cap, cur_len, "&quot;");
        else dest[cur_len++] = c;
    }
    dest[cur_len] = '\0';
    return cur_len;
}

size_t flex_markdown_to_html(
    const char* markdown,
    size_t md_len,
    char* out_html,
    size_t html_cap
) {
    if (!markdown || !out_html || html_cap < 32 || md_len == 0) return 0;

    size_t cur = 0;
    out_html[0] = '\0';

    bool in_code_block = false;
    bool in_list = false;
    const char* p = markdown;
    const char* end = markdown + md_len;

    while (p < end && cur + 16 < html_cap) {
        const char* line_start = p;
        while (p < end && *p != '\n') p++;
        const char* line_end = p;
        if (p < end && *p == '\n') p++;

        size_t raw_line_len = (size_t)(line_end - line_start);

        /* Check for fenced code block toggle */
        if (raw_line_len >= 3 && line_start[0] == '`' && line_start[1] == '`' && line_start[2] == '`') {
            if (in_code_block) {
                cur = append_str(out_html, html_cap, cur, "</code></pre>\n");
                in_code_block = false;
            } else {
                if (in_list) {
                    cur = append_str(out_html, html_cap, cur, "</ul>\n");
                    in_list = false;
                }
                char lang[32] = {0};
                size_t l_idx = 0;
                for (size_t k = 3; k < raw_line_len && l_idx + 1 < sizeof(lang) && !isspace((unsigned char)line_start[k]); ++k) {
                    lang[l_idx++] = line_start[k];
                }
                cur = append_str(out_html, html_cap, cur, "<pre><code class=\"language-");
                if (l_idx > 0) cur = append_str(out_html, html_cap, cur, lang);
                else cur = append_str(out_html, html_cap, cur, "text");
                cur = append_str(out_html, html_cap, cur, "\">");
                in_code_block = true;
            }
            continue;
        }

        if (in_code_block) {
            cur = append_escaped_html(out_html, html_cap, cur, line_start, raw_line_len);
            cur = append_str(out_html, html_cap, cur, "\n");
            continue;
        }

        /* Skip leading whitespace for header and list detection */
        const char* trim_start = line_start;
        while (trim_start < line_end && isspace((unsigned char)*trim_start)) trim_start++;

        if (trim_start == line_end) {
            if (in_list) {
                cur = append_str(out_html, html_cap, cur, "</ul>\n");
                in_list = false;
            }
            continue;
        }

        /* Headers: #, ##, ### */
        if (trim_start[0] == '#') {
            if (in_list) {
                cur = append_str(out_html, html_cap, cur, "</ul>\n");
                in_list = false;
            }

            int h_level = 0;
            while (trim_start < line_end && *trim_start == '#' && h_level < 6) {
                h_level++;
                trim_start++;
            }
            while (trim_start < line_end && isspace((unsigned char)*trim_start)) trim_start++;

            char tag_open[8], tag_close[8];
            snprintf(tag_open, sizeof(tag_open), "<h%d>", h_level);
            snprintf(tag_close, sizeof(tag_close), "</h%d>\n", h_level);

            cur = append_str(out_html, html_cap, cur, tag_open);
            cur = append_escaped_html(out_html, html_cap, cur, trim_start, (size_t)(line_end - trim_start));
            cur = append_str(out_html, html_cap, cur, tag_close);
            continue;
        }

        /* Unordered list item: - or * */
        if ((trim_start[0] == '-' || trim_start[0] == '*') && trim_start + 1 < line_end && trim_start[1] == ' ') {
            if (!in_list) {
                cur = append_str(out_html, html_cap, cur, "<ul>\n");
                in_list = true;
            }
            trim_start += 2;
            cur = append_str(out_html, html_cap, cur, "<li>");
            cur = append_escaped_html(out_html, html_cap, cur, trim_start, (size_t)(line_end - trim_start));
            cur = append_str(out_html, html_cap, cur, "</li>\n");
            continue;
        }

        /* Standard paragraph */
        if (in_list) {
            cur = append_str(out_html, html_cap, cur, "</ul>\n");
            in_list = false;
        }

        cur = append_str(out_html, html_cap, cur, "<p>");
        cur = append_escaped_html(out_html, html_cap, cur, trim_start, (size_t)(line_end - trim_start));
        cur = append_str(out_html, html_cap, cur, "</p>\n");
    }

    if (in_code_block) {
        cur = append_str(out_html, html_cap, cur, "</code></pre>\n");
    }
    if (in_list) {
        cur = append_str(out_html, html_cap, cur, "</ul>\n");
    }

    return cur;
}

size_t flex_markdown_strip_to_plain(
    const char* markdown,
    size_t md_len,
    char* out_plain,
    size_t plain_cap
) {
    if (!markdown || !out_plain || plain_cap < 2 || md_len == 0) return 0;

    size_t out_idx = 0;
    for (size_t i = 0; i < md_len && out_idx + 1 < plain_cap; ++i) {
        char c = markdown[i];
        if (c == '`' || c == '*' || c == '#' || c == '>' || c == '~') {
            continue; /* Skip syntax char */
        }
        out_plain[out_idx++] = c;
    }

    out_plain[out_idx] = '\0';
    return out_idx;
}

size_t flex_markdown_extract_code_blocks(
    const char* markdown,
    size_t md_len,
    char out_langs[][32],
    char out_blocks[][512],
    size_t max_blocks
) {
    if (!markdown || md_len == 0 || !out_blocks || max_blocks == 0) return 0;

    size_t block_count = 0;
    bool in_block = false;
    size_t block_start = 0;
    char current_lang[32] = {0};

    for (size_t i = 0; i + 2 < md_len && block_count < max_blocks; ++i) {
        if ((i == 0 || markdown[i - 1] == '\n') &&
            markdown[i] == '`' && markdown[i + 1] == '`' && markdown[i + 2] == '`') {
            if (!in_block) {
                in_block = true;
                size_t line_end = i + 3;
                while (line_end < md_len && markdown[line_end] != '\n') {
                    line_end++;
                }
                size_t lang_len = line_end - (i + 3);
                if (lang_len >= 32) lang_len = 31;
                if (lang_len > 0 && out_langs) {
                    memcpy(current_lang, markdown + i + 3, lang_len);
                    current_lang[lang_len] = '\0';
                } else {
                    current_lang[0] = '\0';
                }
                block_start = (line_end < md_len) ? (line_end + 1) : line_end;
                i = line_end;
            } else {
                in_block = false;
                size_t block_len = i - block_start;
                if (block_len >= 512) block_len = 511;

                if (out_langs) {
                    strncpy(out_langs[block_count], current_lang, 31);
                    out_langs[block_count][31] = '\0';
                }
                memcpy(out_blocks[block_count], markdown + block_start, block_len);
                out_blocks[block_count][block_len] = '\0';
                block_count++;
                i += 2;
            }
        }
    }
    return block_count;
}

size_t flex_markdown_extract_outline(
    const char* markdown,
    size_t md_len,
    int out_levels[],
    char out_titles[][128],
    size_t max_headers
) {
    if (!markdown || md_len == 0 || !out_levels || !out_titles || max_headers == 0) return 0;

    size_t count = 0;
    for (size_t i = 0; i < md_len && count < max_headers; ++i) {
        if (i == 0 || markdown[i - 1] == '\n') {
            if (markdown[i] == '#') {
                int level = 0;
                size_t p = i;
                while (p < md_len && markdown[p] == '#' && level < 6) {
                    level++;
                    p++;
                }
                while (p < md_len && (markdown[p] == ' ' || markdown[p] == '\t')) {
                    p++;
                }
                size_t title_start = p;
                while (p < md_len && markdown[p] != '\n' && markdown[p] != '\r') {
                    p++;
                }
                size_t title_len = p - title_start;
                if (title_len >= 128) title_len = 127;

                out_levels[count] = level;
                memcpy(out_titles[count], markdown + title_start, title_len);
                out_titles[count][title_len] = '\0';
                count++;
                i = p;
            }
        }
    }
    return count;
}

size_t flex_markdown_clean_inline(
    const char* line,
    size_t line_len,
    char* out_buf,
    size_t out_cap
) {
    if (!line || line_len == 0 || !out_buf || out_cap < 2) {
        if (out_buf && out_cap > 0) out_buf[0] = '\0';
        return 0;
    }

    size_t out_idx = 0;
    for (size_t i = 0; i < line_len && out_idx + 1 < out_cap; ++i) {
        char c = line[i];
        if (c == '*' || c == '_' || c == '`' || c == '~') {
            continue;
        }
        out_buf[out_idx++] = c;
    }
    out_buf[out_idx] = '\0';
    return out_idx;
}
