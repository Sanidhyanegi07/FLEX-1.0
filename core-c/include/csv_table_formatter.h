#ifndef FLEX_CSV_TABLE_FORMATTER_H
#define FLEX_CSV_TABLE_FORMATTER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_TABLE_MAX_COLS     8
#define FLEX_TABLE_MAX_ROWS     32
#define FLEX_TABLE_CELL_CAPACITY 48

typedef enum {
    FLEX_ALIGN_LEFT = 0,
    FLEX_ALIGN_CENTER,
    FLEX_ALIGN_RIGHT
} FlexColumnAlign;

typedef struct {
    char cells[FLEX_TABLE_MAX_ROWS][FLEX_TABLE_MAX_COLS][FLEX_TABLE_CELL_CAPACITY];
    size_t col_widths[FLEX_TABLE_MAX_COLS];
    FlexColumnAlign alignments[FLEX_TABLE_MAX_COLS];
    size_t num_rows;
    size_t num_cols;
} FlexTableGrid;

/**
 * Parse CSV / comma-separated string into table grid.
 */
bool flex_table_parse_csv(const char* csv_data, size_t len, FlexTableGrid* out_grid);

/**
 * Format table grid into a clean Markdown table string (e.g. | Col1 | Col2 |\n|:---|:---|\n| A | B |).
 */
size_t flex_table_format_markdown(const FlexTableGrid* grid, char* out_buf, size_t out_capacity);

/**
 * Format table grid into an ASCII box/grid representation.
 */
size_t flex_table_format_ascii_box(const FlexTableGrid* grid, char* out_buf, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_CSV_TABLE_FORMATTER_H */

