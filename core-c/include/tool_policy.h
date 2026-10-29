#ifndef FLEX_TOOL_POLICY_H
#define FLEX_TOOL_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_MAX_REGISTERED_TOOLS 16
#define FLEX_TOOL_NAME_MAX 32
#define FLEX_TOOL_DESC_MAX 128
#define FLEX_TOOL_ARGS_SCHEMA_MAX 256

typedef enum {
    FLEX_TOOL_PERM_READ_ONLY = 0,
    FLEX_TOOL_PERM_WORKSPACE_WRITE = 1,
    FLEX_TOOL_PERM_SYSTEM_EXEC = 2
} flex_tool_perm_level_t;

typedef enum {
    FLEX_TOOL_AUTH_APPROVED = 0,
    FLEX_TOOL_REJECT_UNKNOWN = 1,
    FLEX_TOOL_REJECT_DISABLED = 2,
    FLEX_TOOL_REJECT_NEEDS_USER_CONFIRM = 3,
    FLEX_TOOL_REJECT_INVALID_ARGS = 4,
    FLEX_TOOL_REJECT_ARGS_TOO_LONG = 5
} flex_tool_auth_status_t;

typedef struct {
    char name[FLEX_TOOL_NAME_MAX];
    char description[FLEX_TOOL_DESC_MAX];
    char args_schema[FLEX_TOOL_ARGS_SCHEMA_MAX];
    flex_tool_perm_level_t perm_level;
    bool enabled;
    uint32_t call_count;
} flex_tool_definition_t;

typedef struct {
    flex_tool_auth_status_t status;
    bool is_authorized;
    bool requires_confirmation;
    char reason[128];
} flex_tool_verdict_t;

typedef struct {
    size_t tool_count;
    flex_tool_definition_t tools[FLEX_MAX_REGISTERED_TOOLS];
    bool allow_mutating_tools;
    uint64_t total_invocations;
} flex_tool_registry_t;

/**
 * Initializes default tool registry with built-in tools.
 */
void flex_tool_registry_init_defaults(flex_tool_registry_t* registry);

/**
 * Registers a new tool into the registry.
 */
bool flex_tool_register(
    flex_tool_registry_t* registry,
    const char* name,
    const char* description,
    const char* args_schema,
    flex_tool_perm_level_t perm_level
);

/**
 * Validates and authorizes a tool invocation request.
 */
flex_tool_auth_status_t flex_tool_validate_invocation(
    flex_tool_registry_t* registry,
    const char* tool_name,
    const char* json_args,
    size_t args_len,
    bool user_has_confirmed,
    flex_tool_verdict_t* out_verdict
);

/**
 * Checks if a tool exists and is enabled.
 */
bool flex_tool_is_available(
    const flex_tool_registry_t* registry,
    const char* tool_name
);

/**
 * Sets tool enabled state.
 */
bool flex_tool_set_enabled(
    flex_tool_registry_t* registry,
    const char* tool_name,
    bool enabled
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_TOOL_POLICY_H */

