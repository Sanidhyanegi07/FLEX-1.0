#include "sqlite_codec.h"
#include <string.h>
#include <stdio.h>

size_t flex_sql_escape_string(
    const char* in_str,
    char* out_escaped,
    size_t out_cap
) {
    if (!in_str || !out_escaped || out_cap < 2) return 0;

    size_t out_idx = 0;
    for (size_t i = 0; in_str[i] && out_idx + 2 < out_cap; ++i) {
        char c = in_str[i];
        if (c == '\'') {
            out_escaped[out_idx++] = '\'';
            out_escaped[out_idx++] = '\'';
        } else if ((unsigned char)c >= 32 || c == '\n' || c == '\r' || c == '\t') {
            out_escaped[out_idx++] = c;
        }
    }

    out_escaped[out_idx] = '\0';
    return out_idx;
}

size_t flex_sql_build_list_conversations_query(
    char* out_buf,
    size_t buf_cap,
    int limit,
    int offset
) {
    if (!out_buf || buf_cap < 64) return 0;
    if (limit <= 0) limit = 50;
    if (offset < 0) offset = 0;

    int written = snprintf(
        out_buf, buf_cap,
        "SELECT id, title, created_at, updated_at "
        "FROM conversations ORDER BY updated_at DESC LIMIT %d OFFSET %d;",
        limit, offset
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_update_conversation_query(
    char* out_buf,
    size_t buf_cap,
    const char* conv_id,
    const char* new_title,
    uint64_t now_ts
) {
    if (!out_buf || !conv_id || !new_title || buf_cap < 128) return 0;

    char escaped_title[256];
    flex_sql_escape_string(new_title, escaped_title, sizeof(escaped_title));

    char escaped_id[64];
    flex_sql_escape_string(conv_id, escaped_id, sizeof(escaped_id));

    int written = snprintf(
        out_buf, buf_cap,
        "UPDATE conversations SET title = '%s', updated_at = %llu WHERE id = '%s';",
        escaped_title,
        (unsigned long long)now_ts,
        escaped_id
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_delete_conversation_query(
    char* out_buf,
    size_t buf_cap,
    const char* conv_id
) {
    if (!out_buf || !conv_id || buf_cap < 64) return 0;

    char escaped_id[64];
    flex_sql_escape_string(conv_id, escaped_id, sizeof(escaped_id));

    int written = snprintf(
        out_buf, buf_cap,
        "DELETE FROM conversations WHERE id = '%s';",
        escaped_id
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_count_messages_query(
    char* out_buf,
    size_t buf_cap,
    const char* conv_id
) {
    if (!out_buf || !conv_id || buf_cap < 64) return 0;

    char escaped_id[64];
    flex_sql_escape_string(conv_id, escaped_id, sizeof(escaped_id));

    int written = snprintf(
        out_buf, buf_cap,
        "SELECT COUNT(*) FROM messages WHERE conversation_id = '%s';",
        escaped_id
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_search_messages_query(
    char* out_buf,
    size_t buf_cap,
    const char* search_term,
    int limit
) {
    if (!out_buf || !search_term || buf_cap < 128) return 0;
    if (limit <= 0) limit = 20;

    char escaped_term[128];
    flex_sql_escape_string(search_term, escaped_term, sizeof(escaped_term));

    int written = snprintf(
        out_buf, buf_cap,
        "SELECT id, conversation_id, role, content, sequence_num, timestamp, operation_id "
        "FROM messages WHERE content LIKE '%%%s%%' ORDER BY timestamp DESC LIMIT %d;",
        escaped_term, limit
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_list_memories_query(
    char* out_buf,
    size_t buf_cap,
    int limit
) {
    if (!out_buf || buf_cap < 64) return 0;
    if (limit <= 0) limit = 100;

    int written = snprintf(
        out_buf, buf_cap,
        "SELECT id, key, content, category, created_at, updated_at "
        "FROM memory_entries ORDER BY updated_at DESC LIMIT %d;",
        limit
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_delete_memory_query(
    char* out_buf,
    size_t buf_cap,
    const char* memory_id
) {
    if (!out_buf || !memory_id || buf_cap < 64) return 0;

    char escaped_id[64];
    flex_sql_escape_string(memory_id, escaped_id, sizeof(escaped_id));

    int written = snprintf(
        out_buf, buf_cap,
        "DELETE FROM memory_entries WHERE id = '%s';",
        escaped_id
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_search_memories_query(
    char* out_buf,
    size_t buf_cap,
    const char* query,
    int limit
) {
    if (!out_buf || !query || buf_cap < 128) return 0;
    if (limit <= 0) limit = 20;

    char escaped_q[128];
    flex_sql_escape_string(query, escaped_q, sizeof(escaped_q));

    int written = snprintf(
        out_buf, buf_cap,
        "SELECT id, key, content, category, created_at, updated_at "
        "FROM memory_entries WHERE key LIKE '%%%s%%' OR content LIKE '%%%s%%' OR category LIKE '%%%s%%' "
        "ORDER BY updated_at DESC LIMIT %d;",
        escaped_q, escaped_q, escaped_q, limit
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_create_conversations_table_query(
    char* out_buf,
    size_t buf_cap
) {
    if (!out_buf || buf_cap < 160) return 0;
    const char* ddl = "CREATE TABLE IF NOT EXISTS conversations ("
                      "id TEXT PRIMARY KEY, "
                      "title TEXT NOT NULL, "
                      "created_at TEXT NOT NULL, "
                      "updated_at TEXT NOT NULL);";
    size_t len = strlen(ddl);
    if (len >= buf_cap) return 0;
    memcpy(out_buf, ddl, len + 1);
    return len;
}

size_t flex_sql_build_create_messages_table_query(
    char* out_buf,
    size_t buf_cap
) {
    if (!out_buf || buf_cap < 280) return 0;
    const char* ddl = "CREATE TABLE IF NOT EXISTS messages ("
                      "id TEXT PRIMARY KEY, "
                      "conversation_id TEXT NOT NULL, "
                      "role TEXT NOT NULL, "
                      "content TEXT NOT NULL, "
                      "sequence_num INTEGER NOT NULL, "
                      "timestamp TEXT NOT NULL, "
                      "model TEXT, "
                      "operation_id INTEGER, "
                      "FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE);";
    size_t len = strlen(ddl);
    if (len >= buf_cap) return 0;
    memcpy(out_buf, ddl, len + 1);
    return len;
}

size_t flex_sql_build_create_memory_entries_table_query(
    char* out_buf,
    size_t buf_cap
) {
    if (!out_buf || buf_cap < 220) return 0;
    const char* ddl = "CREATE TABLE IF NOT EXISTS memory_entries ("
                      "id TEXT PRIMARY KEY, "
                      "key TEXT NOT NULL, "
                      "content TEXT NOT NULL, "
                      "category TEXT NOT NULL, "
                      "created_at TEXT NOT NULL, "
                      "updated_at TEXT NOT NULL);";
    size_t len = strlen(ddl);
    if (len >= buf_cap) return 0;
    memcpy(out_buf, ddl, len + 1);
    return len;
}

size_t flex_sql_build_create_schema_version_table_query(
    char* out_buf,
    size_t buf_cap
) {
    if (!out_buf || buf_cap < 120) return 0;
    const char* ddl = "CREATE TABLE IF NOT EXISTS schema_version ("
                      "version INTEGER PRIMARY KEY, "
                      "applied_at TEXT NOT NULL);";
    size_t len = strlen(ddl);
    if (len >= buf_cap) return 0;
    memcpy(out_buf, ddl, len + 1);
    return len;
}

size_t flex_sql_build_begin_transaction_query(char* out_buf, size_t buf_cap) {
    if (!out_buf || buf_cap < 20) return 0;
    const char* q = "BEGIN TRANSACTION;";
    size_t len = strlen(q);
    memcpy(out_buf, q, len + 1);
    return len;
}

size_t flex_sql_build_commit_transaction_query(char* out_buf, size_t buf_cap) {
    if (!out_buf || buf_cap < 10) return 0;
    const char* q = "COMMIT;";
    size_t len = strlen(q);
    memcpy(out_buf, q, len + 1);
    return len;
}

size_t flex_sql_build_rollback_transaction_query(char* out_buf, size_t buf_cap) {
    if (!out_buf || buf_cap < 12) return 0;
    const char* q = "ROLLBACK;";
    size_t len = strlen(q);
    memcpy(out_buf, q, len + 1);
    return len;
}

size_t flex_sql_build_vacuum_query(char* out_buf, size_t buf_cap) {
    if (!out_buf || buf_cap < 10) return 0;
    const char* q = "VACUUM;";
    size_t len = strlen(q);
    memcpy(out_buf, q, len + 1);
    return len;
}

size_t flex_sql_build_analyze_query(char* out_buf, size_t buf_cap) {
    if (!out_buf || buf_cap < 10) return 0;
    const char* q = "ANALYZE;";
    size_t len = strlen(q);
    memcpy(out_buf, q, len + 1);
    return len;
}

size_t flex_sql_build_insert_memory_query(
    char* out_buf,
    size_t buf_cap,
    const char* id,
    const char* key,
    const char* content,
    const char* category,
    const char* created_at,
    const char* updated_at
) {
    if (!out_buf || !id || !key || !content || !category || !created_at || !updated_at) return 0;
    if (buf_cap < 256) return 0;

    char esc_id[64];
    char esc_key[128];
    char esc_content[1024];
    char esc_cat[64];
    char esc_created[64];
    char esc_updated[64];

    flex_sql_escape_string(id, esc_id, sizeof(esc_id));
    flex_sql_escape_string(key, esc_key, sizeof(esc_key));
    flex_sql_escape_string(content, esc_content, sizeof(esc_content));
    flex_sql_escape_string(category, esc_cat, sizeof(esc_cat));
    flex_sql_escape_string(created_at, esc_created, sizeof(esc_created));
    flex_sql_escape_string(updated_at, esc_updated, sizeof(esc_updated));

    int written = snprintf(
        out_buf, buf_cap,
        "INSERT INTO memory_entries (id, key, content, category, created_at, updated_at) "
        "VALUES ('%s', '%s', '%s', '%s', '%s', '%s');",
        esc_id, esc_key, esc_content, esc_cat, esc_created, esc_updated
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

size_t flex_sql_build_count_memories_query(
    char* out_buf,
    size_t buf_cap,
    const char* category
) {
    if (!out_buf || buf_cap < 64) return 0;

    int written;
    if (category && *category != '\0') {
        char esc_cat[64];
        flex_sql_escape_string(category, esc_cat, sizeof(esc_cat));
        written = snprintf(
            out_buf, buf_cap,
            "SELECT COUNT(*) FROM memory_entries WHERE category = '%s';",
            esc_cat
        );
    } else {
        written = snprintf(
            out_buf, buf_cap,
            "SELECT COUNT(*) FROM memory_entries;"
        );
    }

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}

bool flex_sql_validate_identifier(const char* identifier) {
    if (!identifier || *identifier == '\0') return false;
    size_t len = strlen(identifier);
    if (len > 64) return false;

    if ((identifier[0] >= '0' && identifier[0] <= '9')) {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = identifier[i];
        bool is_alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool is_digit = (c >= '0' && c <= '9');
        bool is_underscore = (c == '_');
        if (!is_alpha && !is_digit && !is_underscore) {
            return false;
        }
    }
    return true;
}

bool flex_sql_has_injection_tokens(const char* text) {
    if (!text) return false;

    const char* dangerous_patterns[] = {
        "--",
        ";",
        "/*",
        "*/",
        "UNION",
        "union",
        "DROP",
        "drop",
        "XP_",
        "xp_",
        "EXEC",
        "exec",
        NULL
    };

    for (int i = 0; dangerous_patterns[i] != NULL; ++i) {
        if (strstr(text, dangerous_patterns[i]) != NULL) {
            return true;
        }
    }
    return false;
}
