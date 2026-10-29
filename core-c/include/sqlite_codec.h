#ifndef FLEX_SQLITE_CODEC_H
#define FLEX_SQLITE_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Escapes single quotes and special characters for safe SQL string literal construction.
 * Replaces ' with '' and strips control characters.
 */
size_t flex_sql_escape_string(
    const char* in_str,
    char* out_escaped,
    size_t out_cap
);

/**
 * Builds a SQL query to fetch conversations ordered by updated_at descending with limit/offset.
 */
size_t flex_sql_build_list_conversations_query(
    char* out_buf,
    size_t buf_cap,
    int limit,
    int offset
);

/**
 * Builds a SQL query to update conversation title and updated_at timestamp.
 */
size_t flex_sql_build_update_conversation_query(
    char* out_buf,
    size_t buf_cap,
    const char* conv_id,
    const char* new_title,
    uint64_t now_ts
);

/**
 * Builds a SQL query to delete a conversation (cascades to messages).
 */
size_t flex_sql_build_delete_conversation_query(
    char* out_buf,
    size_t buf_cap,
    const char* conv_id
);

/**
 * Builds a SQL query to count total messages in a conversation.
 */
size_t flex_sql_build_count_messages_query(
    char* out_buf,
    size_t buf_cap,
    const char* conv_id
);

/**
 * Builds a SQL query to search messages by content substring.
 */
size_t flex_sql_build_search_messages_query(
    char* out_buf,
    size_t buf_cap,
    const char* search_term,
    int limit
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_SQLITE_CODEC_H */

