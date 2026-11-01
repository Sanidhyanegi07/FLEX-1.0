#ifndef FLEX_TOOL_ENGINE_H
#define FLEX_TOOL_ENGINE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_TOOL_MAX_PATH 512
#define FLEX_TOOL_MAX_RESULT_BUF 8192
#define FLEX_TOOL_DEFAULT_MAX_FILE_BYTES (1024 * 1024) /* 1 MB */

typedef enum {
    FLEX_TOOL_STATUS_OK = 0,
    FLEX_TOOL_STATUS_INVALID_ARG = 1,
    FLEX_TOOL_STATUS_SECURITY_VIOLATION = 2,
    FLEX_TOOL_STATUS_IO_ERROR = 3,
    FLEX_TOOL_STATUS_MATH_ERROR = 4,
    FLEX_TOOL_STATUS_UNSUPPORTED = 5,
    FLEX_TOOL_STATUS_OVERFLOW = 6
} flex_tool_status_t;

typedef struct {
    flex_tool_status_t status;
    char tool_name[32];
    char result_summary[128];
    size_t result_len;
    bool is_error;
} flex_tool_exec_meta_t;

/**
 * Returns number of built-in native tools.
 */
size_t flex_tool_get_count(void);

/**
 * Returns name of built-in tool at specified index, or NULL if out of range.
 */
const char* flex_tool_get_name(size_t index);

/**
 * Validates whether tool name is a known built-in tool.
 */
bool flex_tool_is_valid_name(const char* name);

/**
 * Returns human-readable description of built-in tool.
 */
const char* flex_tool_get_description(size_t index);

/**
 * Returns parameter usage guidance for built-in tool.
 */
const char* flex_tool_get_usage(size_t index);

/**
 * Returns status code name for tool execution status.
 */
const char* flex_tool_status_to_string(flex_tool_status_t status);

/**
 * Evaluates a mathematical expression safely using the C11 math_evaluator.
 * Handles decimals, parentheses, operator precedence, and division-by-zero.
 */
flex_tool_status_t flex_tool_calc_eval(
    const char* expr,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
);

/**
 * Reads a workspace text or markdown file safely with boundary enforcement.
 * Checks for path traversal (../), validates file existence and extension,
 * enforces byte limits, and returns safe bounded text with metrics.
 */
flex_tool_status_t flex_tool_read_file(
    const char* workspace_root,
    const char* rel_path,
    size_t max_bytes,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
);

/**
 * Collects local system and architecture diagnostics into formatted text.
 */
flex_tool_status_t flex_tool_system_info(
    const char* app_version,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
);

/**
 * Unified native tool dispatcher.
 * Dispatches to 'calculator', 'workspace_read_file', or 'system_status'.
 */
flex_tool_status_t flex_tool_dispatch(
    const char* tool_name,
    const char* args_str,
    const char* workspace_root,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_TOOL_ENGINE_H */

