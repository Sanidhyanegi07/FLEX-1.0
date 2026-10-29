#include "ai_stream_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

void flex_stream_parser_init(flex_stream_parser_t* parser) {
    if (!parser) return;
    memset(parser, 0, sizeof(flex_stream_parser_t));
}

void flex_stream_parser_reset(flex_stream_parser_t* parser) {
    if (!parser) return;
    parser->line_buffer_len = 0;
    parser->total_tokens_extracted = 0;
    parser->total_lines_parsed = 0;
    parser->seen_done = false;
    parser->line_buffer[0] = '\0';
}

size_t flex_json_unescape_string(
    const char* src,
    size_t src_len,
    char* out_dest,
    size_t dest_cap
) {
    if (!src || !out_dest || dest_cap == 0) return 0;

    size_t out_idx = 0;
    for (size_t i = 0; i < src_len && out_idx + 1 < dest_cap; ++i) {
        if (src[i] == '\\' && i + 1 < src_len) {
            char next = src[++i];
            switch (next) {
                case '"': out_dest[out_idx++] = '"'; break;
                case '\\': out_dest[out_idx++] = '\\'; break;
                case '/': out_dest[out_idx++] = '/'; break;
                case 'b': out_dest[out_idx++] = '\b'; break;
                case 'f': out_dest[out_idx++] = '\f'; break;
                case 'n': out_dest[out_idx++] = '\n'; break;
                case 'r': out_dest[out_idx++] = '\r'; break;
                case 't': out_dest[out_idx++] = '\t'; break;
                case 'u': {
                    /* Hex decode 4 chars if available */
                    if (i + 4 < src_len) {
                        char hex[5] = { src[i+1], src[i+2], src[i+3], src[i+4], '\0' };
                        long cp = strtol(hex, NULL, 16);
                        i += 4;
                        if (cp > 0 && cp <= 0x7F) {
                            out_dest[out_idx++] = (char)cp;
                        } else if (cp <= 0x7FF && out_idx + 2 < dest_cap) {
                            out_dest[out_idx++] = (char)(0xC0 | (cp >> 6));
                            out_dest[out_idx++] = (char)(0x80 | (cp & 0x3F));
                        } else if (out_idx + 3 < dest_cap) {
                            out_dest[out_idx++] = (char)(0xE0 | (cp >> 12));
                            out_dest[out_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            out_dest[out_idx++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default:
                    out_dest[out_idx++] = next;
                    break;
            }
        } else {
            out_dest[out_idx++] = src[i];
        }
    }

    out_dest[out_idx] = '\0';
    return out_idx;
}

static const char* find_json_key(const char* json, size_t len, const char* key) {
    if (!json || !key || len == 0) return NULL;
    size_t key_len = strlen(key);

    for (size_t i = 0; i + key_len + 2 < len; ++i) {
        if (json[i] == '"' && memcmp(json + i + 1, key, key_len) == 0 && json[i + 1 + key_len] == '"') {
            size_t j = i + 1 + key_len + 1;
            while (j < len && (json[j] == ' ' || json[j] == '\t' || json[j] == ':')) {
                if (json[j] == ':') {
                    j++;
                    while (j < len && (json[j] == ' ' || json[j] == '\t')) j++;
                    return json + j;
                }
                j++;
            }
        }
    }
    return NULL;
}

bool flex_stream_parse_ndjson_line(
    const char* line,
    size_t line_len,
    flex_stream_event_t* out_event
) {
    if (!line || !out_event || line_len == 0) return false;
    memset(out_event, 0, sizeof(flex_stream_event_t));
    out_event->type = FLEX_STREAM_EVT_NONE;

    /* Check done flag */
    const char* done_ptr = find_json_key(line, line_len, "done");
    if (done_ptr) {
        if (strncmp(done_ptr, "true", 4) == 0) {
            out_event->is_done = true;
            out_event->type = FLEX_STREAM_EVT_DONE;
        }
    }

    /* Check total_duration */
    const char* total_dur_ptr = find_json_key(line, line_len, "total_duration");
    if (total_dur_ptr) {
        out_event->total_duration_ns = (uint64_t)strtoull(total_dur_ptr, NULL, 10);
    }

    /* Check eval_count */
    const char* eval_cnt_ptr = find_json_key(line, line_len, "eval_count");
    if (eval_cnt_ptr) {
        out_event->eval_count = (uint32_t)strtoul(eval_cnt_ptr, NULL, 10);
    }

    /* Check eval_duration */
    const char* eval_dur_ptr = find_json_key(line, line_len, "eval_duration");
    if (eval_dur_ptr) {
        out_event->eval_duration_ns = (uint64_t)strtoull(eval_dur_ptr, NULL, 10);
    }

    /* Extract content string inside message: {"content":"..."} */
    const char* content_ptr = find_json_key(line, line_len, "content");
    if (content_ptr && *content_ptr == '"') {
        content_ptr++; /* skip opening quote */
        const char* p = content_ptr;
        bool escape = false;
        while ((size_t)(p - line) < line_len && *p) {
            if (escape) {
                escape = false;
            } else if (*p == '\\') {
                escape = true;
            } else if (*p == '"') {
                break;
            }
            p++;
        }

        size_t raw_token_len = (size_t)(p - content_ptr);
        out_event->token_len = flex_json_unescape_string(
            content_ptr, raw_token_len,
            out_event->token, sizeof(out_event->token)
        );

        if (out_event->token_len > 0) {
            out_event->type = out_event->is_done ? FLEX_STREAM_EVT_DONE : FLEX_STREAM_EVT_TOKEN;
        }
    }

    /* If no content but is_done */
    if (out_event->is_done && out_event->type == FLEX_STREAM_EVT_NONE) {
        out_event->type = FLEX_STREAM_EVT_DONE;
    }

    return (out_event->type != FLEX_STREAM_EVT_NONE);
}

size_t flex_stream_parser_feed(
    flex_stream_parser_t* parser,
    const char* chunk,
    size_t chunk_len,
    flex_stream_event_t* out_events,
    size_t max_events
) {
    if (!parser || !chunk || chunk_len == 0 || !out_events || max_events == 0) {
        return 0;
    }

    size_t event_count = 0;
    size_t chunk_idx = 0;

    while (chunk_idx < chunk_len && event_count < max_events) {
        char c = chunk[chunk_idx++];

        if (c == '\n' || c == '\r') {
            if (parser->line_buffer_len > 0) {
                parser->line_buffer[parser->line_buffer_len] = '\0';
                flex_stream_event_t evt;
                if (flex_stream_parse_ndjson_line(parser->line_buffer, parser->line_buffer_len, &evt)) {
                    out_events[event_count++] = evt;
                    parser->total_lines_parsed++;
                    if (evt.token_len > 0) {
                        parser->total_tokens_extracted++;
                    }
                    if (evt.is_done) {
                        parser->seen_done = true;
                    }
                }
                parser->line_buffer_len = 0;
            }
        } else {
            if (parser->line_buffer_len + 1 < FLEX_STREAM_LINE_BUF_MAX) {
                parser->line_buffer[parser->line_buffer_len++] = c;
            } else {
                /* Line buffer overflow protection: flush line */
                parser->line_buffer_len = 0;
            }
        }
    }

    return event_count;
}

