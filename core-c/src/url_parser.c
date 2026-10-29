#include "url_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

FlexUrlStatus flex_url_parse(const char* url_str, FlexParsedUrl* out_url) {
    if (!url_str) return FLEX_URL_ERR_NULL_INPUT;
    if (!out_url) return FLEX_URL_ERR_NULL_INPUT;
    
    memset(out_url, 0, sizeof(FlexParsedUrl));
    size_t len = strlen(url_str);
    if (len == 0) return FLEX_URL_ERR_EMPTY_INPUT;
    
    /* 1. Extract Scheme (e.g. "http://") */
    const char* p = url_str;
    const char* scheme_end = strstr(p, "://");
    if (!scheme_end) {
        /* Default to http if no scheme specified */
        strncpy(out_url->scheme, "http", sizeof(out_url->scheme) - 1);
    } else {
        size_t s_len = (size_t)(scheme_end - p);
        if (s_len >= sizeof(out_url->scheme)) return FLEX_URL_ERR_INVALID_SCHEME;
        for (size_t i = 0; i < s_len; ++i) {
            out_url->scheme[i] = (char)tolower((unsigned char)p[i]);
        }
        out_url->scheme[s_len] = '\0';
        p = scheme_end + 3; /* skip "://" */
    }
    
    /* 2. Extract Host & Port */
    const char* path_start = strchr(p, '/');
    const char* query_start = strchr(p, '?');
    const char* host_end = p + strlen(p);
    
    if (path_start && path_start < host_end) host_end = path_start;
    if (query_start && query_start < host_end) host_end = query_start;
    
    const char* colon = NULL;
    for (const char* q = p; q < host_end; ++q) {
        if (*q == ':') {
            colon = q;
            break;
        }
    }
    
    if (colon) {
        size_t h_len = (size_t)(colon - p);
        if (h_len >= sizeof(out_url->host)) return FLEX_URL_ERR_INVALID_HOST;
        memcpy(out_url->host, p, h_len);
        out_url->host[h_len] = '\0';
        
        char port_str[16];
        size_t port_len = (size_t)(host_end - (colon + 1));
        if (port_len >= sizeof(port_str)) return FLEX_URL_ERR_INVALID_PORT;
        memcpy(port_str, colon + 1, port_len);
        port_str[port_len] = '\0';
        
        char* endptr = NULL;
        long port_val = strtol(port_str, &endptr, 10);
        if (endptr == port_str || port_val <= 0 || port_val > 65535) {
            return FLEX_URL_ERR_INVALID_PORT;
        }
        out_url->port = (uint16_t)port_val;
        out_url->has_port = true;
    } else {
        size_t h_len = (size_t)(host_end - p);
        if (h_len >= sizeof(out_url->host)) return FLEX_URL_ERR_INVALID_HOST;
        memcpy(out_url->host, p, h_len);
        out_url->host[h_len] = '\0';
        
        if (strcmp(out_url->scheme, "https") == 0) {
            out_url->port = 443;
        } else {
            out_url->port = 80;
        }
        out_url->has_port = false;
    }
    
    /* 3. Extract Path */
    if (path_start) {
        const char* p_end = query_start ? query_start : (path_start + strlen(path_start));
        size_t path_len = (size_t)(p_end - path_start);
        if (path_len >= sizeof(out_url->path)) return FLEX_URL_ERR_BUFFER_OVERFLOW;
        memcpy(out_url->path, path_start, path_len);
        out_url->path[path_len] = '\0';
    } else {
        strncpy(out_url->path, "/", sizeof(out_url->path) - 1);
    }
    
    /* 4. Extract Query */
    if (query_start) {
        const char* q_data = query_start + 1;
        size_t q_len = strlen(q_data);
        if (q_len >= sizeof(out_url->query)) return FLEX_URL_ERR_BUFFER_OVERFLOW;
        strncpy(out_url->query, q_data, sizeof(out_url->query) - 1);
        out_url->query[sizeof(out_url->query) - 1] = '\0';
    }
    
    return FLEX_URL_OK;
}

size_t flex_url_format(const FlexParsedUrl* url, char* out_buf, size_t out_capacity) {
    if (!url || !out_buf || out_capacity == 0) return 0;
    
    int written = 0;
    if (url->has_port) {
        if (url->query[0] != '\0') {
            written = snprintf(out_buf, out_capacity, "%s://%s:%u%s?%s",
                               url->scheme, url->host, url->port, url->path, url->query);
        } else {
            written = snprintf(out_buf, out_capacity, "%s://%s:%u%s",
                               url->scheme, url->host, url->port, url->path);
        }
    } else {
        if (url->query[0] != '\0') {
            written = snprintf(out_buf, out_capacity, "%s://%s%s?%s",
                               url->scheme, url->host, url->path, url->query);
        } else {
            written = snprintf(out_buf, out_capacity, "%s://%s%s",
                               url->scheme, url->host, url->path);
        }
    }
    
    return (written > 0 && (size_t)written < out_capacity) ? (size_t)written : 0;
}

bool flex_url_get_query_param(const char* query, const char* key, char* out_val, size_t out_capacity) {
    if (!query || !key || !out_val || out_capacity == 0) return false;
    
    size_t key_len = strlen(key);
    const char* p = query;
    
    while (p && *p != '\0') {
        const char* eq = strchr(p, '=');
        const char* amp = strchr(p, '&');
        
        if (eq && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
            const char* val_start = eq + 1;
            size_t val_len = amp ? (size_t)(amp - val_start) : strlen(val_start);
            if (val_len >= out_capacity) val_len = out_capacity - 1;
            memcpy(out_val, val_start, val_len);
            out_val[val_len] = '\0';
            return true;
        }
        
        if (!amp) break;
        p = amp + 1;
    }
    
    return false;
}

