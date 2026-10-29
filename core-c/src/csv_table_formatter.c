#include "csv_table_formatter.h"
#include "string_util.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

bool flex_table_parse_csv(const char* csv_data, size_t len, FlexTableGrid* out_grid) {
    if (!csv_data || !out_grid) return false;
    
    memset(out_grid, 0, sizeof(FlexTableGrid));
    
    size_t row = 0;
    size_t col = 0;
    size_t cell_pos = 0;
    bool in_quote = false;
    
    for (size_t i = 0; i <= len && row < FLEX_TABLE_MAX_ROWS; ++i) {
        char c = (i < len) ? csv_data[i] : '\n';
        
        if (c == '"') {
            in_quote = !in_quote;
            continue;
        }
        
        if (!in_quote && (c == ',' || c == '\t' || c == '\n' || c == '\r')) {
            if (c == '\r') continue;
            
            out_grid->cells[row][col][cell_pos] = '\0';
            flex_str_trim(out_grid->cells[row][col]);
            
            size_t cell_len = strlen(out_grid->cells[row][col]);
            if (cell_len > out_grid->col_widths[col]) {
                out_grid->col_widths[col] = cell_len;
            }
            
            col++;
            if (col > out_grid->num_cols) {
                out_grid->num_cols = col;
            }
            cell_pos = 0;
            
            if (c == '\n') {
                if (col > 0) {
                    row++;
                    out_grid->num_rows = row;
                }
                col = 0;
            }
            continue;
        }
        
        if (col < FLEX_TABLE_MAX_COLS && cell_pos + 1 < FLEX_TABLE_CELL_CAPACITY) {
            out_grid->cells[row][col][cell_pos++] = c;
        }
    }
    
    /* Ensure minimum column width of 3 */
    for (size_t c = 0; c < out_grid->num_cols; ++c) {
        if (out_grid->col_widths[c] < 3) {
            out_grid->col_widths[c] = 3;
        }
    }
    
    return out_grid->num_rows > 0;
}

size_t flex_table_format_markdown(const FlexTableGrid* grid, char* out_buf, size_t out_capacity) {
    if (!grid || !out_buf || out_capacity == 0 || grid->num_rows == 0 || grid->num_cols == 0) {
        return 0;
    }
    
    size_t written = 0;
    
    for (size_t r = 0; r < grid->num_rows; ++r) {
        /* Print row */
        if (written + 2 < out_capacity) {
            out_buf[written++] = '|';
            out_buf[written++] = ' ';
        }
        
        for (size_t c = 0; c < grid->num_cols; ++c) {
            const char* cell = grid->cells[r][c];
            size_t cell_len = strlen(cell);
            size_t col_w = grid->col_widths[c];
            
            if (written + cell_len < out_capacity) {
                memcpy(&out_buf[written], cell, cell_len);
                written += cell_len;
            }
            
            /* Pad spaces */
            for (size_t s = cell_len; s < col_w && written + 1 < out_capacity; ++s) {
                out_buf[written++] = ' ';
            }
            
            if (written + 3 < out_capacity) {
                out_buf[written++] = ' ';
                out_buf[written++] = '|';
                if (c + 1 < grid->num_cols) {
                    out_buf[written++] = ' ';
                }
            }
        }
        
        if (written + 1 < out_capacity) {
            out_buf[written++] = '\n';
        }
        
        /* Print separator row after header row (row 0) */
        if (r == 0) {
            if (written + 2 < out_capacity) {
                out_buf[written++] = '|';
                out_buf[written++] = ' ';
            }
            for (size_t c = 0; c < grid->num_cols; ++c) {
                size_t col_w = grid->col_widths[c];
                for (size_t s = 0; s < col_w && written + 1 < out_capacity; ++s) {
                    out_buf[written++] = '-';
                }
                if (written + 3 < out_capacity) {
                    out_buf[written++] = ' ';
                    out_buf[written++] = '|';
                    if (c + 1 < grid->num_cols) {
                        out_buf[written++] = ' ';
                    }
                }
            }
            if (written + 1 < out_capacity) {
                out_buf[written++] = '\n';
            }
        }
    }
    
    out_buf[written] = '\0';
    return written;
}

size_t flex_table_format_ascii_box(const FlexTableGrid* grid, char* out_buf, size_t out_capacity) {
    if (!grid || !out_buf || out_capacity == 0 || grid->num_rows == 0) return 0;
    
    size_t written = 0;
    
    /* Top border: +---+---+ */
    if (written + 1 < out_capacity) out_buf[written++] = '+';
    for (size_t c = 0; c < grid->num_cols; ++c) {
        for (size_t s = 0; s < grid->col_widths[c] + 2 && written + 1 < out_capacity; ++s) {
            out_buf[written++] = '-';
        }
        if (written + 1 < out_capacity) out_buf[written++] = '+';
    }
    if (written + 1 < out_capacity) out_buf[written++] = '\n';
    
    for (size_t r = 0; r < grid->num_rows; ++r) {
        /* Data row: | val | val | */
        if (written + 1 < out_capacity) out_buf[written++] = '|';
        for (size_t c = 0; c < grid->num_cols; ++c) {
            if (written + 1 < out_capacity) out_buf[written++] = ' ';
            const char* cell = grid->cells[r][c];
            size_t cell_len = strlen(cell);
            if (written + cell_len < out_capacity) {
                memcpy(&out_buf[written], cell, cell_len);
                written += cell_len;
            }
            for (size_t s = cell_len; s < grid->col_widths[c] && written + 1 < out_capacity; ++s) {
                out_buf[written++] = ' ';
            }
            if (written + 2 < out_capacity) {
                out_buf[written++] = ' ';
                out_buf[written++] = '|';
            }
        }
        if (written + 1 < out_capacity) out_buf[written++] = '\n';
        
        /* Divider after header or at bottom */
        if (r == 0 || r + 1 == grid->num_rows) {
            if (written + 1 < out_capacity) out_buf[written++] = '+';
            for (size_t c = 0; c < grid->num_cols; ++c) {
                for (size_t s = 0; s < grid->col_widths[c] + 2 && written + 1 < out_capacity; ++s) {
                    out_buf[written++] = '-';
                }
                if (written + 1 < out_capacity) out_buf[written++] = '+';
            }
            if (written + 1 < out_capacity) out_buf[written++] = '\n';
        }
    }
    
    out_buf[written] = '\0';
    return written;
}

