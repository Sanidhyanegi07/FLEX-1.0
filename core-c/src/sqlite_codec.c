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

