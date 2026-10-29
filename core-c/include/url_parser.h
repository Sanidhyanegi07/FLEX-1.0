#ifndef FLEX_URL_PARSER_H
#define FLEX_URL_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_URL_MAX_SCHEME 16
#define FLEX_URL_MAX_HOST   128
#define FLEX_URL_MAX_PATH   256
#define FLEX_URL_MAX_QUERY  256

typedef enum {
    FLEX_URL_OK = 0,
    FLEX_URL_ERR_NULL_INPUT,
    FLEX_URL_ERR_EMPTY_INPUT,
    FLEX_URL_ERR_INVALID_SCHEME,
    FLEX_URL_ERR_INVALID_HOST,
    FLEX_URL_ERR_INVALID_PORT,
    FLEX_URL_ERR_BUFFER_OVERFLOW
} FlexUrlStatus;

typedef struct {
    char scheme[FLEX_URL_MAX_SCHEME];
    char host[FLEX_URL_MAX_HOST];
    uint16_t port;
    char path[FLEX_URL_MAX_PATH];
    char query[FLEX_URL_MAX_QUERY];
    bool has_port;
} FlexParsedUrl;

/**
 * Parse an HTTP/HTTPS or local Ollama URL (e.g. "http://127.0.0.1:11434/api/chat?model=qwen").
 */
FlexUrlStatus flex_url_parse(const char* url_str, FlexParsedUrl* out_url);

/**
 * Format a FlexParsedUrl back into an RFC 3986 normalized URL string.
 */
size_t flex_url_format(const FlexParsedUrl* url, char* out_buf, size_t out_capacity);

/**
 * Extract a specific query parameter value by key name.
 */
bool flex_url_get_query_param(const char* query, const char* key, char* out_val, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_URL_PARSER_H */

