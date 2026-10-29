#include "security_sandbox.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

static const char S_SHELL_FORBIDDEN[] = "&|;$`><\r\n\t";

bool flex_sandbox_is_shell_safe(const char* arg, size_t arg_len) {
    if (!arg) return false;
    if (arg_len == 0) return true;
    
    for (size_t i = 0; i < arg_len; ++i) {
        char c = arg[i];
        if (strchr(S_SHELL_FORBIDDEN, c) != NULL) {
            return false;
        }
    }
    return true;
}

static void normalize_slashes(char* path) {
    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (path[i] == '\\') {
            path[i] = '/';
        }
    }
}

FlexSecurityStatus flex_sandbox_validate_path(const char* base_workspace,
                                             const char* target_path) {
    if (!base_workspace || !target_path) {
        return FLEX_SEC_ERR_NULL_ARG;
    }
    
    if (strlen(target_path) >= FLEX_SANDBOX_MAX_PATH_LEN ||
        strlen(base_workspace) >= FLEX_SANDBOX_MAX_PATH_LEN) {
        return FLEX_SEC_ERR_ARG_TOO_LONG;
    }
    
    char norm_target[FLEX_SANDBOX_MAX_PATH_LEN];
    strncpy(norm_target, target_path, sizeof(norm_target) - 1);
    norm_target[sizeof(norm_target) - 1] = '\0';
    normalize_slashes(norm_target);
    
    /* Check for directory traversal sequences: "../", "/..", or standalone ".." */
    if (strstr(norm_target, "../") != NULL ||
        strstr(norm_target, "/..") != NULL ||
        strcmp(norm_target, "..") == 0) {
        return FLEX_SEC_ERR_PATH_TRAVERSAL;
    }
    
    char norm_base[FLEX_SANDBOX_MAX_PATH_LEN];
    strncpy(norm_base, base_workspace, sizeof(norm_base) - 1);
    norm_base[sizeof(norm_base) - 1] = '\0';
    normalize_slashes(norm_base);
    
    /* Remove trailing slash from base if present */
    size_t base_len = strlen(norm_base);
    if (base_len > 0 && norm_base[base_len - 1] == '/') {
        norm_base[base_len - 1] = '\0';
        base_len--;
    }
    
    /* If target is absolute, it must start with base_workspace */
    if (norm_target[0] == '/' || (isalpha((unsigned char)norm_target[0]) && norm_target[1] == ':')) {
        if (strncmp(norm_target, norm_base, base_len) != 0) {
            return FLEX_SEC_ERR_OUTSIDE_WORKSPACE;
        }
        /* Ensure boundary is exact (either exact match or followed by slash) */
        char boundary = norm_target[base_len];
        if (boundary != '\0' && boundary != '/') {
            return FLEX_SEC_ERR_OUTSIDE_WORKSPACE;
        }
    }
    
    return FLEX_SEC_OK;
}

void flex_sandbox_init_log(FlexSecurityAuditLog* log) {
    if (!log) return;
    memset(log, 0, sizeof(FlexSecurityAuditLog));
}

void flex_sandbox_record_audit(FlexSecurityAuditLog* log,
                               FlexSecurityStatus status,
                               const char* target,
                               const char* reason) {
    if (!log) return;
    
    size_t idx = log->head;
    log->entries[idx].timestamp_sec = (unsigned long)time(NULL);
    log->entries[idx].status = status;
    
    if (target) {
        strncpy(log->entries[idx].target, target, sizeof(log->entries[idx].target) - 1);
        log->entries[idx].target[sizeof(log->entries[idx].target) - 1] = '\0';
    } else {
        log->entries[idx].target[0] = '\0';
    }
    
    if (reason) {
        strncpy(log->entries[idx].reason, reason, sizeof(log->entries[idx].reason) - 1);
        log->entries[idx].reason[sizeof(log->entries[idx].reason) - 1] = '\0';
    } else {
        log->entries[idx].reason[0] = '\0';
    }
    
    if (status != FLEX_SEC_OK) {
        log->total_violations++;
    }
    
    log->head = (log->head + 1) % FLEX_SANDBOX_MAX_LOG_ENTRIES;
    if (log->count < FLEX_SANDBOX_MAX_LOG_ENTRIES) {
        log->count++;
    }
}

size_t flex_sandbox_get_violations(const FlexSecurityAuditLog* log) {
    if (!log) return 0;
    return log->total_violations;
}

size_t flex_sandbox_sanitize_arg(const char* input,
                                 char* output,
                                 size_t max_output_len) {
    if (!input || !output || max_output_len == 0) return 0;
    
    size_t out_idx = 0;
    for (size_t i = 0; input[i] != '\0' && out_idx + 1 < max_output_len; ++i) {
        char c = input[i];
        if (strchr(S_SHELL_FORBIDDEN, c) != NULL) {
            output[out_idx++] = '_';
        } else {
            output[out_idx++] = c;
        }
    }
    output[out_idx] = '\0';
    return out_idx;
}

