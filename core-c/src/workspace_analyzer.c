#include "workspace_analyzer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

bool flex_path_normalize(
    const char* raw_path,
    char* out_normalized,
    size_t out_cap
) {
    if (!raw_path || !out_normalized || out_cap < 2) return false;

    size_t out_idx = 0;
    bool prev_was_slash = false;

    for (size_t i = 0; raw_path[i] && out_idx + 1 < out_cap; ++i) {
        char c = raw_path[i];
        if (c == '\\' || c == '/') {
            if (!prev_was_slash) {
                out_normalized[out_idx++] = '/';
                prev_was_slash = true;
            }
        } else {
            out_normalized[out_idx++] = c;
            prev_was_slash = false;
        }
    }

    /* Strip trailing slash unless root "/" */
    if (out_idx > 1 && out_normalized[out_idx - 1] == '/') {
        out_idx--;
    }

    out_normalized[out_idx] = '\0';
    return true;
}

bool flex_path_is_safe_relative(const char* path) {
    if (!path || *path == '\0') return false;

    /* Absolute paths with drive letters or leading slash are not safe relative paths */
    if (path[0] == '/' || path[0] == '\\') return false;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return false;

    /* Check for ".." traversal components */
    const char* p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.') {
            /* Check if it's isolated by slashes or string boundaries */
            bool start_bound = (p == path || p[-1] == '/' || p[-1] == '\\');
            bool end_bound = (p[2] == '\0' || p[2] == '/' || p[2] == '\\');
            if (start_bound && end_bound) {
                return false; /* Path traversal attempt detected */
            }
        }
        p++;
    }

    return true;
}

flex_file_category_t flex_classify_file_extension(const char* filename) {
    if (!filename) return FLEX_FILE_TYPE_UNKNOWN;

    /* Special filename checks */
    if (strstr(filename, "CMakeLists.txt") || strstr(filename, "CMakePresets.json") ||
        strstr(filename, "Makefile")) {
        return FLEX_FILE_TYPE_BUILD;
    }

    const char* dot = strrchr(filename, '.');
    if (!dot) return FLEX_FILE_TYPE_UNKNOWN;

    dot++; /* Skip dot */

    char ext[16];
    size_t i = 0;
    while (dot[i] && i + 1 < sizeof(ext)) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
        i++;
    }
    ext[i] = '\0';

    if (strcmp(ext, "c") == 0) return FLEX_FILE_TYPE_C_SOURCE;
    if (strcmp(ext, "cpp") == 0 || strcmp(ext, "cc") == 0 || strcmp(ext, "cxx") == 0) return FLEX_FILE_TYPE_CPP_SOURCE;
    if (strcmp(ext, "h") == 0 || strcmp(ext, "hpp") == 0 || strcmp(ext, "hxx") == 0) return FLEX_FILE_TYPE_HEADER;
    if (strcmp(ext, "ps1") == 0 || strcmp(ext, "bat") == 0 || strcmp(ext, "sh") == 0 || strcmp(ext, "cmake") == 0) return FLEX_FILE_TYPE_BUILD;
    if (strcmp(ext, "md") == 0 || strcmp(ext, "txt") == 0 || strcmp(ext, "json") == 0) return FLEX_FILE_TYPE_DOC;
    if (strcmp(ext, "bin") == 0 || strcmp(ext, "gguf") == 0) return FLEX_FILE_TYPE_MODEL;
    if (strcmp(ext, "wav") == 0 || strcmp(ext, "png") == 0 || strcmp(ext, "jpg") == 0 || strcmp(ext, "ico") == 0) return FLEX_FILE_TYPE_MEDIA;

    return FLEX_FILE_TYPE_UNKNOWN;
}

bool flex_glob_match(const char* pattern, const char* string) {
    if (!pattern || !string) return false;

    while (*pattern && *string) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return true; /* Trailing * matches everything */
            while (*string) {
                if (flex_glob_match(pattern, string)) return true;
                string++;
            }
            return false;
        } else if (*pattern == '?' || *pattern == *string) {
            pattern++;
            string++;
        } else {
            return false;
        }
    }

    while (*pattern == '*') pattern++;
    return (*pattern == '\0' && *string == '\0');
}

void flex_analyze_source_buffer(
    const char* text_buffer,
    size_t buffer_len,
    flex_file_metrics_t* out_metrics
) {
    if (!out_metrics) return;
    memset(out_metrics, 0, sizeof(flex_file_metrics_t));
    if (!text_buffer || buffer_len == 0) return;

    out_metrics->byte_size = buffer_len;
    bool in_block_comment = false;
    const char* p = text_buffer;
    const char* end = text_buffer + buffer_len;

    while (p < end) {
        const char* line_start = p;
        while (p < end && *p != '\n') p++;
        const char* line_end = p;
        if (p < end && *p == '\n') p++;

        out_metrics->total_lines++;

        /* Trim line whitespace */
        while (line_start < line_end && isspace((unsigned char)*line_start)) line_start++;
        while (line_end > line_start && isspace((unsigned char)line_end[-1])) line_end--;

        if (line_start == line_end) {
            out_metrics->blank_lines++;
            continue;
        }

        if (in_block_comment) {
            out_metrics->comment_lines++;
            if (strstr(line_start, "*/")) {
                in_block_comment = false;
            }
            continue;
        }

        if (line_start[0] == '/' && line_start + 1 < line_end && line_start[1] == '/') {
            out_metrics->comment_lines++;
            continue;
        }

        if (line_start[0] == '/' && line_start + 1 < line_end && line_start[1] == '*') {
            out_metrics->comment_lines++;
            if (!strstr(line_start + 2, "*/")) {
                in_block_comment = true;
            }
            continue;
        }

        out_metrics->code_lines++;
    }
}

const char* flex_file_category_name(flex_file_category_t cat) {
    switch (cat) {
        case FLEX_FILE_TYPE_C_SOURCE: return "C_SOURCE";
        case FLEX_FILE_TYPE_CPP_SOURCE: return "CPP_SOURCE";
        case FLEX_FILE_TYPE_HEADER: return "HEADER";
        case FLEX_FILE_TYPE_BUILD: return "BUILD";
        case FLEX_FILE_TYPE_DOC: return "DOC";
        case FLEX_FILE_TYPE_MODEL: return "MODEL";
        case FLEX_FILE_TYPE_MEDIA: return "MEDIA";
        default: return "UNKNOWN";
    }
}

