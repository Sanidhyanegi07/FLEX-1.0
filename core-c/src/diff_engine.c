#include "diff_engine.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    char lines[FLEX_DIFF_MAX_LINES][FLEX_DIFF_LINE_LEN];
    size_t count;
} LineBuffer;

static void split_lines(const char* text, LineBuffer* buf) {
    buf->count = 0;
    if (!text) return;
    
    size_t line_len = 0;
    for (size_t i = 0; text[i] != '\0' && buf->count < FLEX_DIFF_MAX_LINES; ++i) {
        if (text[i] == '\r') continue;
        if (text[i] == '\n') {
            buf->lines[buf->count][line_len] = '\0';
            buf->count++;
            line_len = 0;
        } else {
            if (line_len + 1 < FLEX_DIFF_LINE_LEN) {
                buf->lines[buf->count][line_len++] = text[i];
            }
        }
    }
    if (line_len > 0 && buf->count < FLEX_DIFF_MAX_LINES) {
        buf->lines[buf->count][line_len] = '\0';
        buf->count++;
    }
}

bool flex_diff_compute(const char* original,
                       const char* modified,
                       FlexDiffResult* out_diff) {
    if (!out_diff) return false;
    memset(out_diff, 0, sizeof(FlexDiffResult));
    
    LineBuffer orig_lines;
    LineBuffer mod_lines;
    split_lines(original, &orig_lines);
    split_lines(modified, &mod_lines);
    
    size_t i = 0;
    size_t j = 0;
    
    while (i < orig_lines.count || j < mod_lines.count) {
        if (out_diff->count >= FLEX_DIFF_MAX_LINES * 2) break;
        
        if (i < orig_lines.count && j < mod_lines.count) {
            if (strcmp(orig_lines.lines[i], mod_lines.lines[j]) == 0) {
                FlexDiffEntry* e = &out_diff->entries[out_diff->count++];
                e->type = FLEX_DIFF_EQUAL;
                strncpy(e->text, orig_lines.lines[i], FLEX_DIFF_LINE_LEN - 1);
                e->text[FLEX_DIFF_LINE_LEN - 1] = '\0';
                e->line_num_orig = i + 1;
                e->line_num_mod = j + 1;
                out_diff->unchanged++;
                i++;
                j++;
                continue;
            }
            
            /* Look ahead 1 line to find matches */
            bool match_in_mod = false;
            if (j + 1 < mod_lines.count && strcmp(orig_lines.lines[i], mod_lines.lines[j + 1]) == 0) {
                match_in_mod = true;
            }
            
            if (match_in_mod) {
                /* mod_lines[j] was inserted */
                FlexDiffEntry* e = &out_diff->entries[out_diff->count++];
                e->type = FLEX_DIFF_INSERT;
                strncpy(e->text, mod_lines.lines[j], FLEX_DIFF_LINE_LEN - 1);
                e->text[FLEX_DIFF_LINE_LEN - 1] = '\0';
                e->line_num_orig = 0;
                e->line_num_mod = j + 1;
                out_diff->additions++;
                j++;
            } else {
                /* orig_lines[i] was deleted or modified */
                FlexDiffEntry* edel = &out_diff->entries[out_diff->count++];
                edel->type = FLEX_DIFF_DELETE;
                strncpy(edel->text, orig_lines.lines[i], FLEX_DIFF_LINE_LEN - 1);
                edel->text[FLEX_DIFF_LINE_LEN - 1] = '\0';
                edel->line_num_orig = i + 1;
                edel->line_num_mod = 0;
                out_diff->deletions++;
                i++;
                
                if (out_diff->count < FLEX_DIFF_MAX_LINES * 2) {
                    FlexDiffEntry* eins = &out_diff->entries[out_diff->count++];
                    eins->type = FLEX_DIFF_INSERT;
                    strncpy(eins->text, mod_lines.lines[j], FLEX_DIFF_LINE_LEN - 1);
                    eins->text[FLEX_DIFF_LINE_LEN - 1] = '\0';
                    eins->line_num_orig = 0;
                    eins->line_num_mod = j + 1;
                    out_diff->additions++;
                    j++;
                }
            }
        } else if (i < orig_lines.count) {
            FlexDiffEntry* e = &out_diff->entries[out_diff->count++];
            e->type = FLEX_DIFF_DELETE;
            strncpy(e->text, orig_lines.lines[i], FLEX_DIFF_LINE_LEN - 1);
            e->text[FLEX_DIFF_LINE_LEN - 1] = '\0';
            e->line_num_orig = i + 1;
            e->line_num_mod = 0;
            out_diff->deletions++;
            i++;
        } else {
            FlexDiffEntry* e = &out_diff->entries[out_diff->count++];
            e->type = FLEX_DIFF_INSERT;
            strncpy(e->text, mod_lines.lines[j], FLEX_DIFF_LINE_LEN - 1);
            e->text[FLEX_DIFF_LINE_LEN - 1] = '\0';
            e->line_num_orig = 0;
            e->line_num_mod = j + 1;
            out_diff->additions++;
            j++;
        }
    }
    
    size_t total = out_diff->unchanged * 2 + out_diff->additions + out_diff->deletions;
    if (total > 0) {
        out_diff->similarity = (float)(out_diff->unchanged * 2) / (float)total;
    } else {
        out_diff->similarity = 1.0f;
    }
    
    return true;
}

size_t flex_diff_format_unified(const FlexDiffResult* diff,
                                const char* filename,
                                char* out_buf,
                                size_t out_capacity) {
    if (!diff || !out_buf || out_capacity == 0) return 0;
    
    const char* fn = filename ? filename : "file";
    int header_len = snprintf(out_buf, out_capacity, "--- a/%s\n+++ b/%s\n@@ -1 +1 @@\n", fn, fn);
    if (header_len < 0) return 0;
    size_t written = (size_t)header_len;
    
    for (size_t i = 0; i < diff->count; ++i) {
        char prefix = ' ';
        if (diff->entries[i].type == FLEX_DIFF_INSERT) prefix = '+';
        else if (diff->entries[i].type == FLEX_DIFF_DELETE) prefix = '-';
        
        if (written + 2 < out_capacity) {
            out_buf[written++] = prefix;
            size_t line_len = strlen(diff->entries[i].text);
            if (written + line_len + 1 < out_capacity) {
                memcpy(&out_buf[written], diff->entries[i].text, line_len);
                written += line_len;
                out_buf[written++] = '\n';
            }
        }
    }
    
    out_buf[written] = '\0';
    return written;
}

