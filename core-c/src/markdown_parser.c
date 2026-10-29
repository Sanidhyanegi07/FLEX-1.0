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

