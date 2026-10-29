#ifndef FLEX_WORKSPACE_ANALYZER_H
#define FLEX_WORKSPACE_ANALYZER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_FILE_TYPE_UNKNOWN = 0,
    FLEX_FILE_TYPE_C_SOURCE = 1,
    FLEX_FILE_TYPE_CPP_SOURCE = 2,
    FLEX_FILE_TYPE_HEADER = 3,
    FLEX_FILE_TYPE_BUILD = 4,
    FLEX_FILE_TYPE_DOC = 5,
    FLEX_FILE_TYPE_MODEL = 6,
    FLEX_FILE_TYPE_MEDIA = 7
} flex_file_category_t;

typedef struct {
    size_t total_lines;
    size_t code_lines;
    size_t comment_lines;
    size_t blank_lines;
    size_t byte_size;
} flex_file_metrics_t;

/**
 * Normalizes a file path: converts Windows backslashes to forward slashes,
 * strips redundant slashes and trailing slashes.
 */
bool flex_path_normalize(
    const char* raw_path,
    char* out_normalized,
    size_t out_cap
);

/**
 * Security check: Verifies that path does not attempt directory traversal
 * attacks (e.g. "../" or escaping above root).
 */
bool flex_path_is_safe_relative(const char* path);

/**
 * Classifies file category based on filename and extension.
 */
flex_file_category_t flex_classify_file_extension(const char* filename);

/**
 * Simple wildcard / glob pattern matcher (supports '*' and '?').
 */
bool flex_glob_match(const char* pattern, const char* string);

/**
 * Calculates code metrics from text buffer.
 */
void flex_analyze_source_buffer(
    const char* text_buffer,
    size_t buffer_len,
    flex_file_metrics_t* out_metrics
);

/**
 * Returns string name of file category.
 */
const char* flex_file_category_name(flex_file_category_t cat);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_WORKSPACE_ANALYZER_H */

