#ifndef FLEX_DIFF_ENGINE_H
#define FLEX_DIFF_ENGINE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_DIFF_MAX_LINES    64
#define FLEX_DIFF_LINE_LEN     128

typedef enum {
    FLEX_DIFF_EQUAL = 0,
    FLEX_DIFF_INSERT,
    FLEX_DIFF_DELETE
} FlexDiffOpType;

typedef struct {
    FlexDiffOpType type;
    char text[FLEX_DIFF_LINE_LEN];
    size_t line_num_orig;
    size_t line_num_mod;
} FlexDiffEntry;

typedef struct {
    FlexDiffEntry entries[FLEX_DIFF_MAX_LINES * 2];
    size_t count;
    size_t additions;
    size_t deletions;
    size_t unchanged;
    float similarity;
} FlexDiffResult;

/**
 * Compute line diff between original and modified text buffers.
 */
bool flex_diff_compute(const char* original,
                       const char* modified,
                       FlexDiffResult* out_diff);

/**
 * Format diff result into a unified diff string.
 */
size_t flex_diff_format_unified(const FlexDiffResult* diff,
                                const char* filename,
                                char* out_buf,
                                size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_DIFF_ENGINE_H */

