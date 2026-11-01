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

/**
 * Builds a SQL query to list memory entries ordered by updated_at descending with limit.
 */
size_t flex_sql_build_list_memories_query(
    char* out_buf,
    size_t buf_cap,
    int limit
);

/**
 * Builds a SQL query to delete a memory entry by ID.
 */
size_t flex_sql_build_delete_memory_query(
    char* out_buf,
    size_t buf_cap,
    const char* memory_id
);

/**
 * Builds a SQL query to search memory entries by key, content, or category.
 */
size_t flex_sql_build_search_memories_query(
    char* out_buf,
    size_t buf_cap,
    const char* query,
    int limit
);

/**
 * Builds standard DDL for creating the conversations SQLite table.
 */
size_t flex_sql_build_create_conversations_table_query(
    char* out_buf,
    size_t buf_cap
);

/**
 * Builds standard DDL for creating the messages SQLite table.
 */
size_t flex_sql_build_create_messages_table_query(
    char* out_buf,
    size_t buf_cap
);

/**
 * Builds standard DDL for creating the memory_entries SQLite table.
 */
size_t flex_sql_build_create_memory_entries_table_query(
    char* out_buf,
    size_t buf_cap
);

/**
 * Builds standard DDL for creating the schema_version SQLite table.
 */
size_t flex_sql_build_create_schema_version_table_query(
    char* out_buf,
    size_t buf_cap
);

/**
 * Builds SQL transaction control queries (BEGIN, COMMIT, ROLLBACK).
 */
size_t flex_sql_build_begin_transaction_query(char* out_buf, size_t buf_cap);
size_t flex_sql_build_commit_transaction_query(char* out_buf, size_t buf_cap);
size_t flex_sql_build_rollback_transaction_query(char* out_buf, size_t buf_cap);

/**
 * Builds SQLite maintenance queries (VACUUM, ANALYZE).
 */
size_t flex_sql_build_vacuum_query(char* out_buf, size_t buf_cap);
size_t flex_sql_build_analyze_query(char* out_buf, size_t buf_cap);

/**
 * Builds a SQL query to insert a memory fact record into memory_entries.
 */
size_t flex_sql_build_insert_memory_query(
    char* out_buf,
    size_t buf_cap,
    const char* id,
    const char* key,
    const char* content,
    const char* category,
    const char* created_at,
    const char* updated_at
);

/**
 * Builds a SQL query to count total memory entries, optionally filtered by category.
 */
size_t flex_sql_build_count_memories_query(
    char* out_buf,
    size_t buf_cap,
    const char* category
);

/**
 * Validates a table or column identifier to prevent SQL injection.
 */
bool flex_sql_validate_identifier(const char* identifier);

/**
 * Checks whether an untrusted input string contains SQL injection danger tokens.
 */
bool flex_sql_has_injection_tokens(const char* text);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_SQLITE_CODEC_H */

