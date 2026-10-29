#include "conversation_record.h"
#include <string.h>
#include <stdio.h>

const char* const FLEX_SQL_CREATE_CONVERSATIONS_TABLE =
    "CREATE TABLE IF NOT EXISTS conversations ("
    "id TEXT PRIMARY KEY, "
    "title TEXT NOT NULL, "
    "created_at INTEGER NOT NULL, "
    "updated_at INTEGER NOT NULL"
    ");";

const char* const FLEX_SQL_CREATE_MESSAGES_TABLE =
    "CREATE TABLE IF NOT EXISTS messages ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "conversation_id TEXT NOT NULL, "
    "role TEXT NOT NULL, "
    "content TEXT NOT NULL, "
    "sequence_num INTEGER NOT NULL, "
    "timestamp INTEGER NOT NULL, "
    "operation_id INTEGER DEFAULT 0, "
    "FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE"
    ");";

const char* const FLEX_SQL_CREATE_CONVERSATION_ID_INDEX =
    "CREATE INDEX IF NOT EXISTS idx_messages_conversation_id "
    "ON messages(conversation_id);";

const char* const FLEX_SQL_CREATE_SEQUENCE_INDEX =
    "CREATE INDEX IF NOT EXISTS idx_messages_sequence "
    "ON messages(conversation_id, sequence_num);";

bool flex_conv_record_init(
    flex_conv_record_t* conv,
    const char* id,
    const char* title,
    uint64_t now_timestamp
) {
    if (!conv || !id || *id == '\0') return false;
    memset(conv, 0, sizeof(flex_conv_record_t));

    snprintf(conv->id, sizeof(conv->id), "%.63s", id);
    if (title && *title != '\0') {
        snprintf(conv->title, sizeof(conv->title), "%.127s", title);
    } else {
        snprintf(conv->title, sizeof(conv->title), "New Chat");
    }

    conv->created_at = now_timestamp;
    conv->updated_at = now_timestamp;
    conv->message_count = 0;
    return true;
}

bool flex_msg_record_init(
    flex_msg_record_t* msg,
    uint64_t id,
    const char* conversation_id,
    const char* role,
    const char* content,
    uint64_t sequence_num,
    uint64_t timestamp,
    uint64_t operation_id
) {
    if (!msg || !conversation_id || *conversation_id == '\0' || !role || !content) {
        return false;
    }
    memset(msg, 0, sizeof(flex_msg_record_t));

    msg->id = id;
    snprintf(msg->conversation_id, sizeof(msg->conversation_id), "%.63s", conversation_id);
    snprintf(msg->role, sizeof(msg->role), "%.15s", role);
    snprintf(msg->content, sizeof(msg->content), "%.4095s", content);
    msg->content_len = strlen(msg->content);
    msg->sequence_num = sequence_num;
    msg->timestamp = timestamp;
    msg->operation_id = operation_id;
    msg->duration_ms = 0;

    return true;
}

bool flex_validate_sequence_progression(
    uint64_t current_max_seq,
    uint64_t next_seq
) {
    if (next_seq == 0) return false;
    if (current_max_seq == 0) return true;
    return (next_seq == current_max_seq + 1);
}

static void escape_json_string(const char* in, char* out, size_t out_cap) {
    if (!in || !out || out_cap == 0) return;
    size_t out_idx = 0;
    for (size_t i = 0; in[i] && out_idx + 2 < out_cap; ++i) {
        char c = in[i];
        if (c == '"') {
            out[out_idx++] = '\\';
            out[out_idx++] = '"';
        } else if (c == '\\') {
            out[out_idx++] = '\\';
            out[out_idx++] = '\\';
        } else if (c == '\n') {
            out[out_idx++] = '\\';
            out[out_idx++] = 'n';
        } else if (c == '\r') {
            out[out_idx++] = '\\';
            out[out_idx++] = 'r';
        } else if (c == '\t') {
            out[out_idx++] = '\\';
            out[out_idx++] = 't';
        } else if ((unsigned char)c >= 32) {
            out[out_idx++] = c;
        }
    }
    out[out_idx] = '\0';
}

size_t flex_conv_record_to_json(
    const flex_conv_record_t* conv,
    char* out_buf,
    size_t buf_cap
) {
    if (!conv || !out_buf || buf_cap < 32) return 0;

    char escaped_title[256];
    escape_json_string(conv->title, escaped_title, sizeof(escaped_title));

    int written = snprintf(
        out_buf, buf_cap,
        "{\"id\":\"%s\",\"title\":\"%s\",\"created_at\":%llu,\"updated_at\":%llu,\"message_count\":%zu}",
        conv->id,
        escaped_title,
        (unsigned long long)conv->created_at,
        (unsigned long long)conv->updated_at,
        conv->message_count
    );

    if (written < 0 || (size_t)written >= buf_cap) {
        out_buf[buf_cap - 1] = '\0';
        return buf_cap - 1;
    }
    return (size_t)written;
}

size_t flex_msg_record_to_json(
    const flex_msg_record_t* msg,
    char* out_buf,
    size_t buf_cap
) {
    if (!msg || !out_buf || buf_cap < 32) return 0;

    char escaped_content[2048];
    escape_json_string(msg->content, escaped_content, sizeof(escaped_content));

    int written = snprintf(
        out_buf, buf_cap,
        "{\"id\":%llu,\"conversation_id\":\"%s\",\"role\":\"%s\",\"content\":\"%s\",\"sequence_num\":%llu,\"timestamp\":%llu,\"operation_id\":%llu}",
        (unsigned long long)msg->id,
        msg->conversation_id,
        msg->role,
        escaped_content,
        (unsigned long long)msg->sequence_num,
        (unsigned long long)msg->timestamp,
        (unsigned long long)msg->operation_id
    );

    if (written < 0 || (size_t)written >= buf_cap) {
        out_buf[buf_cap - 1] = '\0';
        return buf_cap - 1;
    }
    return (size_t)written;
}

size_t flex_sql_build_insert_message_query(
    char* out_buf,
    size_t buf_cap
) {
    const char query[] =
        "INSERT INTO messages (conversation_id, role, content, sequence_num, timestamp, operation_id) "
        "VALUES (:conv_id, :role, :content, :seq_num, :ts, :op_id);";

    size_t qlen = sizeof(query) - 1;
    if (!out_buf || buf_cap <= qlen) return 0;

    memcpy(out_buf, query, qlen + 1);
    return qlen;
}

size_t flex_sql_build_select_messages_query(
    char* out_buf,
    size_t buf_cap
) {
    const char query[] =
        "SELECT id, conversation_id, role, content, sequence_num, timestamp, operation_id "
        "FROM messages WHERE conversation_id = :conv_id "
        "ORDER BY sequence_num ASC LIMIT :limit OFFSET :offset;";

    size_t qlen = sizeof(query) - 1;
    if (!out_buf || buf_cap <= qlen) return 0;

    memcpy(out_buf, query, qlen + 1);
    return qlen;
}

