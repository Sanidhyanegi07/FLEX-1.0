#ifndef FLEX_SECURITY_SANDBOX_H
#define FLEX_SECURITY_SANDBOX_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_SANDBOX_MAX_PATH_LEN    260
#define FLEX_SANDBOX_MAX_LOG_ENTRIES 32
#define FLEX_SANDBOX_MAX_REASON_LEN  128

typedef enum {
    FLEX_SEC_OK = 0,
    FLEX_SEC_ERR_NULL_ARG,
    FLEX_SEC_ERR_INJECTION_DETECTED,
    FLEX_SEC_ERR_PATH_TRAVERSAL,
    FLEX_SEC_ERR_OUTSIDE_WORKSPACE,
    FLEX_SEC_ERR_DISALLOWED_COMMAND,
    FLEX_SEC_ERR_ARG_TOO_LONG
} FlexSecurityStatus;

typedef struct {
    unsigned long timestamp_sec;
    FlexSecurityStatus status;
    char reason[FLEX_SANDBOX_MAX_REASON_LEN];
    char target[64];
} FlexSecurityAuditEntry;

typedef struct {
    FlexSecurityAuditEntry entries[FLEX_SANDBOX_MAX_LOG_ENTRIES];
    size_t count;
    size_t head;
    size_t total_violations;
} FlexSecurityAuditLog;

/**
 * Check if an argument string contains shell injection characters:
 * & | ; $ ` > < \n \r \0
 */
bool flex_sandbox_is_shell_safe(const char* arg, size_t arg_len);

/**
 * Validate that a target path is strictly contained within a workspace base path.
 * Rejects path traversal sequences like "..", ".\..", etc.
 */
FlexSecurityStatus flex_sandbox_validate_path(const char* base_workspace,
                                             const char* target_path);

/**
 * Initialize a security audit log.
 */
void flex_sandbox_init_log(FlexSecurityAuditLog* log);

/**
 * Record a security event or violation into the ring buffer.
 */
void flex_sandbox_record_audit(FlexSecurityAuditLog* log,
                               FlexSecurityStatus status,
                               const char* target,
                               const char* reason);

/**
 * Count the number of recorded violations in the audit log.
 */
size_t flex_sandbox_get_violations(const FlexSecurityAuditLog* log);

/**
 * Sanitize an argument by replacing or stripping shell metacharacters.
 */
size_t flex_sandbox_sanitize_arg(const char* input,
                                 char* output,
                                 size_t max_output_len);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_SECURITY_SANDBOX_H */

