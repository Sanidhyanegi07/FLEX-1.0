#ifndef FLEX_CONVERSATION_RECORD_H
#define FLEX_CONVERSATION_RECORD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_CONV_ID_MAX 64
#define FLEX_CONV_TITLE_MAX 128
#define FLEX_MSG_ROLE_MAX 16
#define FLEX_MSG_CONTENT_MAX 4096

typedef struct {
    char id[FLEX_CONV_ID_MAX];
    char title[FLEX_CONV_TITLE_MAX];
    uint64_t created_at;
    uint64_t updated_at;
    size_t message_count;
} flex_conv_record_t;

typedef struct {
    uint64_t id;
    char conversation_id[FLEX_CONV_ID_MAX];
    char role[FLEX_MSG_ROLE_MAX];
    char content[FLEX_MSG_CONTENT_MAX];
    size_t content_len;
    uint64_t sequence_num;
    uint64_t timestamp;
    uint64_t operation_id;
    uint32_t duration_ms;
} flex_msg_record_t;

/* Database Schema Version & SQL Constants */
#define FLEX_STORAGE_SCHEMA_VERSION 1

extern const char* const FLEX_SQL_CREATE_CONVERSATIONS_TABLE;
extern const char* const FLEX_SQL_CREATE_MESSAGES_TABLE;
extern const char* const FLEX_SQL_CREATE_CONVERSATION_ID_INDEX;
extern const char* const FLEX_SQL_CREATE_SEQUENCE_INDEX;

/**
 * Initializes a conversation record.
 */
bool flex_conv_record_init(
    flex_conv_record_t* conv,
    const char* id,
    const char* title,
    uint64_t now_timestamp
);

/**
 * Initializes a message record with validation.
 */
bool flex_msg_record_init(
    flex_msg_record_t* msg,
    uint64_t id,
    const char* conversation_id,
    const char* role,
    const char* content,
    uint64_t sequence_num,
    uint64_t timestamp,
    uint64_t operation_id
);

/**
 * Validates that a new message sequence number is strictly monotonic.
 */
bool flex_validate_sequence_progression(
    uint64_t current_max_seq,
    uint64_t next_seq
);

/**
 * Serializes a conversation record to a JSON string.
 */
size_t flex_conv_record_to_json(
    const flex_conv_record_t* conv,
    char* out_buf,
    size_t buf_cap
);

/**
 * Serializes a message record to a JSON string.
 */
size_t flex_msg_record_to_json(
    const flex_msg_record_t* msg,
    char* out_buf,
    size_t buf_cap
);

/**
 * Builds a parameterized SQL query string for inserting a message.
 */
size_t flex_sql_build_insert_message_query(
    char* out_buf,
    size_t buf_cap
);

/**
 * Builds a parameterized SQL query string for fetching messages with pagination.
 */
size_t flex_sql_build_select_messages_query(
    char* out_buf,
    size_t buf_cap
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_CONVERSATION_RECORD_H */

