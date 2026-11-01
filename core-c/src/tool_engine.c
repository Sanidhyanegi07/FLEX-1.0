#include "tool_engine.h"
#include "math_evaluator.h"
#include "workspace_analyzer.h"
#include "sysinfo_collector.h"
#include "security_sandbox.h"
#include "tool_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char* const S_BUILTIN_TOOLS[] = {
    "calculator",
    "workspace_read_file",
    "system_status"
};

static const char* const S_TOOL_DESCRIPTIONS[] = {
    "Evaluates arithmetic, floating-point math, parentheses, and expressions safely.",
    "Reads text and markdown files within the active project workspace boundaries.",
    "Inspects local host platform, CPU, RAM, and architectural runtime metrics."
};

static const char* const S_TOOL_USAGES[] = {
    "/calc <expression> (e.g. 2 * (10 + 5.5))",
    "/read <workspace_relative_path> (e.g. README.md)",
    "/sysinfo or /status (displays system diagnostics and C core status)"
};

#define S_BUILTIN_TOOL_COUNT (sizeof(S_BUILTIN_TOOLS) / sizeof(S_BUILTIN_TOOLS[0]))

size_t flex_tool_get_count(void) {
    return S_BUILTIN_TOOL_COUNT;
}

const char* flex_tool_get_name(size_t index) {
    if (index >= S_BUILTIN_TOOL_COUNT) return NULL;
    return S_BUILTIN_TOOLS[index];
}

const char* flex_tool_get_description(size_t index) {
    if (index >= S_BUILTIN_TOOL_COUNT) return NULL;
    return S_TOOL_DESCRIPTIONS[index];
}

const char* flex_tool_get_usage(size_t index) {
    if (index >= S_BUILTIN_TOOL_COUNT) return NULL;
    return S_TOOL_USAGES[index];
}

const char* flex_tool_status_to_string(flex_tool_status_t status) {
    switch (status) {
        case FLEX_TOOL_STATUS_OK:
            return "SUCCESS";
        case FLEX_TOOL_STATUS_INVALID_ARG:
            return "INVALID_ARGUMENT";
        case FLEX_TOOL_STATUS_SECURITY_VIOLATION:
            return "SECURITY_VIOLATION";
        case FLEX_TOOL_STATUS_IO_ERROR:
            return "IO_ERROR";
        case FLEX_TOOL_STATUS_MATH_ERROR:
            return "MATH_ERROR";
        case FLEX_TOOL_STATUS_UNSUPPORTED:
            return "UNSUPPORTED_TOOL";
        case FLEX_TOOL_STATUS_OVERFLOW:
            return "BUFFER_OVERFLOW";
        default:
            return "UNKNOWN_STATUS";
    }
}

bool flex_tool_is_valid_name(const char* name) {
    if (!name) return false;
    for (size_t i = 0; i < S_BUILTIN_TOOL_COUNT; ++i) {
        if (strcmp(name, S_BUILTIN_TOOLS[i]) == 0) return true;
    }
    if (strcmp(name, "calc") == 0) return true;
    if (strcmp(name, "read_file") == 0) return true;
    if (strcmp(name, "sysinfo") == 0) return true;
    return false;
}

static void init_meta(flex_tool_exec_meta_t* meta, const char* name) {
    if (!meta) return;
    memset(meta, 0, sizeof(flex_tool_exec_meta_t));
    if (name) {
        snprintf(meta->tool_name, sizeof(meta->tool_name), "%s", name);
    }
}

flex_tool_status_t flex_tool_calc_eval(
    const char* expr,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
) {
    init_meta(out_meta, "calculator");
    if (!out_buf || out_cap == 0) return FLEX_TOOL_STATUS_INVALID_ARG;
    out_buf[0] = '\0';

    if (!expr || expr[0] == '\0') {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_INVALID_ARG;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Empty or NULL expression");
        }
        snprintf(out_buf, out_cap, "Error: Expression cannot be empty.");
        return FLEX_TOOL_STATUS_INVALID_ARG;
    }

    FlexMathResult math_res = flex_math_evaluate(expr);
    if (math_res.status != FLEX_MATH_OK) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_MATH_ERROR;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                     "%.120s", math_res.error_msg);
        }
        snprintf(out_buf, out_cap, "Math Error: %s (at pos %zu)",
                 math_res.error_msg, math_res.error_pos);
        if (out_meta) out_meta->result_len = strlen(out_buf);
        return FLEX_TOOL_STATUS_MATH_ERROR;
    }

    /* Successful calculation */
    int written = snprintf(out_buf, out_cap,
                           "Calculation: %s = %g\nCalculation Result: %g",
                           expr, math_res.result, math_res.result);
    if (written < 0 || (size_t)written >= out_cap) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_OVERFLOW;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Output buffer overflow");
        }
        return FLEX_TOOL_STATUS_OVERFLOW;
    }

    if (out_meta) {
        out_meta->status = FLEX_TOOL_STATUS_OK;
        out_meta->is_error = false;
        out_meta->result_len = (size_t)written;
        snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                 "Result: %g", math_res.result);
    }
    return FLEX_TOOL_STATUS_OK;
}

static bool is_permitted_file_type(const char* filename) {
    if (!filename) return false;
    if (flex_workspace_is_supported_doc_ext(filename)) return true;

    const char* base = strrchr(filename, '/');
    if (!base) base = strrchr(filename, '\\');
    const char* name = base ? base + 1 : filename;
    if (strcmp(name, "CMakeLists.txt") == 0 || strcmp(name, "Makefile") == 0 ||
        strcmp(name, "LICENSE") == 0 || strcmp(name, "README") == 0) {
        return true;
    }

    const char* ext = strrchr(filename, '.');
    if (!ext) return false;
    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
        strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0 || strcmp(ext, ".cc") == 0 ||
        strcmp(ext, ".json") == 0 || strcmp(ext, ".ini") == 0 ||
        strcmp(ext, ".cmake") == 0 || strcmp(ext, ".txt") == 0 ||
        strcmp(ext, ".md") == 0 || strcmp(ext, ".csv") == 0 ||
        strcmp(ext, ".xml") == 0 || strcmp(ext, ".yaml") == 0 ||
        strcmp(ext, ".yml") == 0 || strcmp(ext, ".toml") == 0 ||
        strcmp(ext, ".log") == 0) {
        return true;
    }
    return false;
}

flex_tool_status_t flex_tool_read_file(
    const char* workspace_root,
    const char* rel_path,
    size_t max_bytes,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
) {
    init_meta(out_meta, "workspace_read_file");
    if (!out_buf || out_cap == 0) return FLEX_TOOL_STATUS_INVALID_ARG;
    out_buf[0] = '\0';

    if (!rel_path || rel_path[0] == '\0') {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_INVALID_ARG;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Empty path provided");
        }
        snprintf(out_buf, out_cap, "Error: File path cannot be empty.");
        return FLEX_TOOL_STATUS_INVALID_ARG;
    }

    /* Security check 1: Disallow path traversal */
    if (!flex_path_is_safe_relative(rel_path)) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_SECURITY_VIOLATION;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                     "Security rejection: directory traversal detected");
        }
        snprintf(out_buf, out_cap,
                 "Security Error: Access denied. Directory traversal outside workspace is forbidden.");
        return FLEX_TOOL_STATUS_SECURITY_VIOLATION;
    }

    /* Security check 2: File type classification */
    if (!is_permitted_file_type(rel_path)) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_SECURITY_VIOLATION;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                     "Unsupported or forbidden file extension");
        }
        snprintf(out_buf, out_cap,
                 "Security Error: Reading binary or executable files via workspace reader is restricted.");
        return FLEX_TOOL_STATUS_SECURITY_VIOLATION;
    }

    char full_path[FLEX_TOOL_MAX_PATH];
    if (workspace_root && workspace_root[0] != '\0') {
        snprintf(full_path, sizeof(full_path), "%s/%s", workspace_root, rel_path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", rel_path);
    }

    char norm_path[FLEX_TOOL_MAX_PATH];
    flex_path_normalize(full_path, norm_path, sizeof(norm_path));

    FILE* fp = fopen(norm_path, "rb");
    if (!fp) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_IO_ERROR;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                     "File not found or inaccessible");
        }
        snprintf(out_buf, out_cap, "IO Error: Cannot open file '%s'.", rel_path);
        return FLEX_TOOL_STATUS_IO_ERROR;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(fp);
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_IO_ERROR;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Failed to query file size");
        }
        snprintf(out_buf, out_cap, "IO Error: Failed to determine size of '%s'.", rel_path);
        return FLEX_TOOL_STATUS_IO_ERROR;
    }

    size_t limit = (max_bytes > 0 && max_bytes < FLEX_TOOL_DEFAULT_MAX_FILE_BYTES)
                       ? max_bytes : FLEX_TOOL_DEFAULT_MAX_FILE_BYTES;

    size_t bytes_to_read = (size_t)file_size;
    bool truncated = false;
    if (bytes_to_read > limit) {
        bytes_to_read = limit;
        truncated = true;
    }

    /* Temporary heap buffer to read file safely */
    char* raw_data = (char*)malloc(bytes_to_read + 1);
    if (!raw_data) {
        fclose(fp);
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_IO_ERROR;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Out of memory");
        }
        snprintf(out_buf, out_cap, "Error: Memory allocation failure while reading file.");
        return FLEX_TOOL_STATUS_IO_ERROR;
    }

    size_t actual_read = fread(raw_data, 1, bytes_to_read, fp);
    fclose(fp);
    raw_data[actual_read] = '\0';

    /* Verify encoding */
    int enc = flex_workspace_detect_text_encoding((const unsigned char*)raw_data, actual_read);
    if (enc == -1) {
        free(raw_data);
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_INVALID_ARG;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Binary content detected");
        }
        snprintf(out_buf, out_cap, "Validation Error: File '%s' contains binary bytes.", rel_path);
        return FLEX_TOOL_STATUS_INVALID_ARG;
    }

    /* Calculate code metrics */
    flex_file_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));
    flex_analyze_source_buffer(raw_data, actual_read, &metrics);

    int hdr_len = snprintf(out_buf, out_cap,
        "[Workspace File: %s | Size: %ld bytes | Lines: %zu | Code: %zu%s]\n```\n",
        rel_path, file_size, metrics.total_lines, metrics.code_lines,
        truncated ? " | Truncated to 1MB" : "");

    if (hdr_len > 0 && (size_t)hdr_len < out_cap) {
        size_t rem = out_cap - (size_t)hdr_len - 8; /* Reserve for "\n```\n" and null */
        size_t cpy_len = (actual_read < rem) ? actual_read : rem;
        memcpy(out_buf + hdr_len, raw_data, cpy_len);
        out_buf[hdr_len + cpy_len] = '\0';
        strncat(out_buf, "\n```", out_cap - strlen(out_buf) - 1);
    }

    free(raw_data);

    if (out_meta) {
        out_meta->status = FLEX_TOOL_STATUS_OK;
        out_meta->is_error = false;
        out_meta->result_len = strlen(out_buf);
        snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                 "Read %zu bytes (%zu lines)", actual_read, metrics.total_lines);
    }
    return FLEX_TOOL_STATUS_OK;
}

flex_tool_status_t flex_tool_read_authorized_file(
    const char* file_path,
    size_t max_bytes,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
) {
    init_meta(out_meta, "authorized_read_file");
    if (!out_buf || out_cap == 0) return FLEX_TOOL_STATUS_INVALID_ARG;
    out_buf[0] = '\0';

    if (!file_path || file_path[0] == '\0') {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_INVALID_ARG;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Empty file path");
        }
        snprintf(out_buf, out_cap, "Error: File path cannot be empty.");
        return FLEX_TOOL_STATUS_INVALID_ARG;
    }

    if (!is_permitted_file_type(file_path)) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_SECURITY_VIOLATION;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Unauthorized file extension");
        }
        snprintf(out_buf, out_cap, "Security Error: File type is not permitted for reading (must be text or source code).");
        return FLEX_TOOL_STATUS_SECURITY_VIOLATION;
    }

    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_IO_ERROR;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Cannot open authorized file");
        }
        snprintf(out_buf, out_cap, "IO Error: Cannot open file '%s'.", file_path);
        return FLEX_TOOL_STATUS_IO_ERROR;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(fp);
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_IO_ERROR;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Failed to query file size");
        }
        snprintf(out_buf, out_cap, "IO Error: Failed to determine size of '%s'.", file_path);
        return FLEX_TOOL_STATUS_IO_ERROR;
    }

    size_t limit = (max_bytes > 0 && max_bytes < FLEX_TOOL_DEFAULT_MAX_FILE_BYTES)
                       ? max_bytes : FLEX_TOOL_DEFAULT_MAX_FILE_BYTES;

    if ((size_t)file_size > limit) {
        fclose(fp);
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_OVERFLOW;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "File size exceeds 1 MB limit");
        }
        snprintf(out_buf, out_cap, "IO Error: File size (%ld bytes) exceeds maximum permitted limit (%zu bytes).", file_size, limit);
        return FLEX_TOOL_STATUS_OVERFLOW;
    }

    size_t bytes_to_read = (size_t)file_size;
    char* raw_data = (char*)malloc(bytes_to_read + 1);
    if (!raw_data) {
        fclose(fp);
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_IO_ERROR;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Out of memory");
        }
        snprintf(out_buf, out_cap, "Error: Memory allocation failure while reading file.");
        return FLEX_TOOL_STATUS_IO_ERROR;
    }

    size_t actual_read = fread(raw_data, 1, bytes_to_read, fp);
    fclose(fp);
    raw_data[actual_read] = '\0';

    int enc = flex_workspace_detect_text_encoding((const unsigned char*)raw_data, actual_read);
    if (enc == -1 || !flex_workspace_is_printable_text((const uint8_t*)raw_data, actual_read)) {
        free(raw_data);
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_SECURITY_VIOLATION;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Binary file detected");
        }
        snprintf(out_buf, out_cap, "Security Error: File contains binary or non-printable data.");
        return FLEX_TOOL_STATUS_SECURITY_VIOLATION;
    }

    flex_file_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));
    flex_analyze_source_buffer(raw_data, actual_read, &metrics);

    size_t copy_len = (actual_read < out_cap - 1) ? actual_read : (out_cap - 1);
    memcpy(out_buf, raw_data, copy_len);
    out_buf[copy_len] = '\0';
    free(raw_data);

    if (out_meta) {
        out_meta->status = FLEX_TOOL_STATUS_OK;
        out_meta->is_error = false;
        out_meta->result_len = strlen(out_buf);
        snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                 "Read %zu bytes (%zu lines)", actual_read, metrics.total_lines);
    }
    return FLEX_TOOL_STATUS_OK;
}

flex_tool_status_t flex_tool_system_info(
    const char* app_version,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
) {
    init_meta(out_meta, "system_status");
    if (!out_buf || out_cap == 0) return FLEX_TOOL_STATUS_INVALID_ARG;
    out_buf[0] = '\0';

    FlexSystemInfo info;
    memset(&info, 0, sizeof(info));
    flex_sysinfo_collect(&info, 3600);

    const char* ver = (app_version && app_version[0] != '\0') ? app_version : "1.0.0";
    size_t written = flex_sysinfo_format_status(&info, ver, out_buf, out_cap);

    if (written == 0) {
        if (out_meta) {
            out_meta->status = FLEX_TOOL_STATUS_OVERFLOW;
            out_meta->is_error = true;
            snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Buffer overflow formatting sysinfo");
        }
        return FLEX_TOOL_STATUS_OVERFLOW;
    }

    if (out_meta) {
        out_meta->status = FLEX_TOOL_STATUS_OK;
        out_meta->is_error = false;
        out_meta->result_len = written;
        snprintf(out_meta->result_summary, sizeof(out_meta->result_summary),
                 "OS: %s | RAM: %zu MB", info.os_name, info.total_ram_mb);
    }
    return FLEX_TOOL_STATUS_OK;
}

flex_tool_status_t flex_tool_dispatch(
    const char* tool_name,
    const char* args_str,
    const char* workspace_root,
    char* out_buf,
    size_t out_cap,
    flex_tool_exec_meta_t* out_meta
) {
    if (!tool_name) return FLEX_TOOL_STATUS_UNSUPPORTED;

    if (strcmp(tool_name, "calculator") == 0 || strcmp(tool_name, "calc") == 0) {
        return flex_tool_calc_eval(args_str, out_buf, out_cap, out_meta);
    }

    if (strcmp(tool_name, "workspace_read_file") == 0 || strcmp(tool_name, "read_file") == 0) {
        return flex_tool_read_file(workspace_root, args_str, 0, out_buf, out_cap, out_meta);
    }

    if (strcmp(tool_name, "system_status") == 0 || strcmp(tool_name, "sysinfo") == 0) {
        return flex_tool_system_info(args_str, out_buf, out_cap, out_meta);
    }

    init_meta(out_meta, tool_name);
    if (out_meta) {
        out_meta->status = FLEX_TOOL_STATUS_UNSUPPORTED;
        out_meta->is_error = true;
        snprintf(out_meta->result_summary, sizeof(out_meta->result_summary), "Unknown tool name");
    }
    if (out_buf && out_cap > 0) {
        snprintf(out_buf, out_cap, "Error: Unknown native tool '%s'.", tool_name);
    }
    return FLEX_TOOL_STATUS_UNSUPPORTED;
}
