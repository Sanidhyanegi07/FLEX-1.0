#ifndef FLEX_AI_STREAM_PARSER_H
#define FLEX_AI_STREAM_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_STREAM_TOKEN_MAX 1024
#define FLEX_STREAM_LINE_BUF_MAX 4096

typedef enum {
    FLEX_STREAM_EVT_NONE = 0,
    FLEX_STREAM_EVT_TOKEN = 1,
    FLEX_STREAM_EVT_DONE = 2,
    FLEX_STREAM_EVT_ERROR = 3
} flex_stream_event_type_t;

typedef struct {
    flex_stream_event_type_t type;
    char token[FLEX_STREAM_TOKEN_MAX];
    size_t token_len;
    bool is_done;
    uint64_t total_duration_ns;
    uint32_t eval_count;
    uint64_t eval_duration_ns;
    char error_detail[128];
} flex_stream_event_t;

typedef struct {
    char line_buffer[FLEX_STREAM_LINE_BUF_MAX];
    size_t line_buffer_len;
    uint64_t total_tokens_extracted;
    uint64_t total_lines_parsed;
    bool seen_done;
} flex_stream_parser_t;

/**
 * Initializes the stream parser.
 */
void flex_stream_parser_init(flex_stream_parser_t* parser);

/**
 * Feeds a chunk of raw network bytes (which may contain multiple lines or partial lines).
 * Fills out_events with extracted events up to max_events.
 * Returns number of events produced.
 */
size_t flex_stream_parser_feed(
    flex_stream_parser_t* parser,
    const char* chunk,
    size_t chunk_len,
    flex_stream_event_t* out_events,
    size_t max_events
);

/**
 * Parses a single complete NDJSON line.
 */
bool flex_stream_parse_ndjson_line(
    const char* line,
    size_t line_len,
    flex_stream_event_t* out_event
);

/**
 * Unescapes a JSON string literal into out_dest (e.g. \n -> newline, \" -> quote).
 */
size_t flex_json_unescape_string(
    const char* src,
    size_t src_len,
    char* out_dest,
    size_t dest_cap
);

/**
 * Resets parser state for a new stream.
 */
void flex_stream_parser_reset(flex_stream_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_AI_STREAM_PARSER_H */

