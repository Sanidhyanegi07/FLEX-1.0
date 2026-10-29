#include "tool_policy.h"
#include <string.h>
#include <stdio.h>

void flex_tool_registry_init_defaults(flex_tool_registry_t* registry) {
    if (!registry) return;
    memset(registry, 0, sizeof(flex_tool_registry_t));
    registry->allow_mutating_tools = false; /* Safe default */

    /* Tool 1: Workspace file search (read-only) */
    flex_tool_register(
        registry,
        "workspace_search",
        "Searches files and filenames in the active workspace project tree",
        "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\"}},\"required\":[\"pattern\"]}",
        FLEX_TOOL_PERM_READ_ONLY
    );

    /* Tool 2: Memory search (read-only) */
    flex_tool_register(
        registry,
        "memory_search",
        "Queries past conversation history and saved context memories",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"]}",
        FLEX_TOOL_PERM_READ_ONLY
    );

    /* Tool 3: System diagnostics status (read-only) */
    flex_tool_register(
        registry,
        "system_status",
        "Returns active process PID, lifecycle status, and memory statistics",
        "{\"type\":\"object\",\"properties\":{}}",
        FLEX_TOOL_PERM_READ_ONLY
    );

    /* Tool 4: Fast calculator (read-only) */
    flex_tool_register(
        registry,
        "calculator",
        "Evaluates mathematical expressions safely",
        "{\"type\":\"object\",\"properties\":{\"expr\":{\"type\":\"string\"}},\"required\":[\"expr\"]}",
        FLEX_TOOL_PERM_READ_ONLY
    );

    /* Tool 5: Workspace file write (mutating, requires user confirmation) */
    flex_tool_register(
        registry,
        "workspace_write_file",
        "Creates or overwrites a file inside the active workspace project",
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}",
        FLEX_TOOL_PERM_WORKSPACE_WRITE
    );
}

bool flex_tool_register(
    flex_tool_registry_t* registry,
    const char* name,
    const char* description,
    const char* args_schema,
    flex_tool_perm_level_t perm_level
) {
    if (!registry || !name || registry->tool_count >= FLEX_MAX_REGISTERED_TOOLS) {
        return false;
    }

    /* Check for duplicate name */
    for (size_t i = 0; i < registry->tool_count; ++i) {
        if (strcmp(registry->tools[i].name, name) == 0) {
            return false;
        }
    }

    size_t idx = registry->tool_count;
    flex_tool_definition_t* t = &registry->tools[idx];
    memset(t, 0, sizeof(flex_tool_definition_t));

    snprintf(t->name, sizeof(t->name), "%.31s", name);
    if (description) {
        snprintf(t->description, sizeof(t->description), "%.127s", description);
    }
    if (args_schema) {
        snprintf(t->args_schema, sizeof(t->args_schema), "%.255s", args_schema);
    }
    t->perm_level = perm_level;
    t->enabled = true;
    t->call_count = 0;

    registry->tool_count++;
    return true;
}

flex_tool_auth_status_t flex_tool_validate_invocation(
    flex_tool_registry_t* registry,
    const char* tool_name,
    const char* json_args,
    size_t args_len,
    bool user_has_confirmed,
    flex_tool_verdict_t* out_verdict
) {
    if (out_verdict) {
        memset(out_verdict, 0, sizeof(flex_tool_verdict_t));
        out_verdict->is_authorized = false;
    }

    if (!registry || !tool_name) {
        if (out_verdict) {
            out_verdict->status = FLEX_TOOL_REJECT_UNKNOWN;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Null registry or tool name");
        }
        return FLEX_TOOL_REJECT_UNKNOWN;
    }

    /* 1. Find tool */
    flex_tool_definition_t* tool = NULL;
    for (size_t i = 0; i < registry->tool_count; ++i) {
        if (strcmp(registry->tools[i].name, tool_name) == 0) {
            tool = &registry->tools[i];
            break;
        }
    }

    if (!tool) {
        if (out_verdict) {
            out_verdict->status = FLEX_TOOL_REJECT_UNKNOWN;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Tool '%.31s' is not registered", tool_name);
        }
        return FLEX_TOOL_REJECT_UNKNOWN;
    }

    /* 2. Check enabled */
    if (!tool->enabled) {
        if (out_verdict) {
            out_verdict->status = FLEX_TOOL_REJECT_DISABLED;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Tool '%.31s' is currently disabled", tool_name);
        }
        return FLEX_TOOL_REJECT_DISABLED;
    }

    if (json_args && args_len == 0) {
        args_len = strlen(json_args);
    }

    /* 3. Check args bounds */
    if (args_len > 1024) {
        if (out_verdict) {
            out_verdict->status = FLEX_TOOL_REJECT_ARGS_TOO_LONG;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Tool arguments length (%zu) exceeds 1024 bytes limit", args_len);
        }
        return FLEX_TOOL_REJECT_ARGS_TOO_LONG;
    }

    /* 4. Check JSON syntax (simple balanced brace validation) */
    if (json_args && args_len > 0) {
        int brace_depth = 0;
        bool in_quotes = false;
        bool escape = false;

        for (size_t k = 0; k < args_len && json_args[k]; ++k) {
            char c = json_args[k];
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                in_quotes = !in_quotes;
                continue;
            }
            if (!in_quotes) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth--;
            }
        }

        if (brace_depth != 0 || in_quotes) {
            if (out_verdict) {
                out_verdict->status = FLEX_TOOL_REJECT_INVALID_ARGS;
                snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Tool arguments contain malformed JSON syntax");
            }
            return FLEX_TOOL_REJECT_INVALID_ARGS;
        }
    }

    /* 5. Permission level and user confirmation gating */
    if (tool->perm_level >= FLEX_TOOL_PERM_WORKSPACE_WRITE) {
        if (!user_has_confirmed) {
            if (out_verdict) {
                out_verdict->status = FLEX_TOOL_REJECT_NEEDS_USER_CONFIRM;
                out_verdict->requires_confirmation = true;
                snprintf(out_verdict->reason, sizeof(out_verdict->reason),
                         "Mutating tool '%.31s' requires explicit user authorization", tool_name);
            }
            return FLEX_TOOL_REJECT_NEEDS_USER_CONFIRM;
        }
    }

    /* Authorized */
    tool->call_count++;
    registry->total_invocations++;

    if (out_verdict) {
        out_verdict->status = FLEX_TOOL_AUTH_APPROVED;
        out_verdict->is_authorized = true;
        snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Tool invocation approved");
    }

    return FLEX_TOOL_AUTH_APPROVED;
}

bool flex_tool_is_available(
    const flex_tool_registry_t* registry,
    const char* tool_name
) {
    if (!registry || !tool_name) return false;
    for (size_t i = 0; i < registry->tool_count; ++i) {
        if (strcmp(registry->tools[i].name, tool_name) == 0) {
            return registry->tools[i].enabled;
        }
    }
    return false;
}

bool flex_tool_set_enabled(
    flex_tool_registry_t* registry,
    const char* tool_name,
    bool enabled
) {
    if (!registry || !tool_name) return false;
    for (size_t i = 0; i < registry->tool_count; ++i) {
        if (strcmp(registry->tools[i].name, tool_name) == 0) {
            registry->tools[i].enabled = enabled;
            return true;
        }
    }
    return false;
}
