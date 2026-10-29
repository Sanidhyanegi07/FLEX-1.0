#include "kv_store.h"
#include "string_util.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

void flex_kv_init(FlexKvStore* store) {
    if (!store) return;
    memset(store, 0, sizeof(FlexKvStore));
}

bool flex_kv_set(FlexKvStore* store, const char* key, const char* value) {
    if (!store || !key || !value) return false;
    
    /* Update existing */
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use && strcmp(store->pairs[i].key, key) == 0) {
            strncpy(store->pairs[i].value, value, FLEX_KV_MAX_VAL - 1);
            store->pairs[i].value[FLEX_KV_MAX_VAL - 1] = '\0';
            return true;
        }
    }
    
    /* Find free slot */
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (!store->pairs[i].in_use) {
            strncpy(store->pairs[i].key, key, FLEX_KV_MAX_KEY - 1);
            store->pairs[i].key[FLEX_KV_MAX_KEY - 1] = '\0';
            strncpy(store->pairs[i].value, value, FLEX_KV_MAX_VAL - 1);
            store->pairs[i].value[FLEX_KV_MAX_VAL - 1] = '\0';
            store->pairs[i].in_use = true;
            store->count++;
            return true;
        }
    }
    
    return false;
}

const char* flex_kv_get_str(const FlexKvStore* store, const char* key) {
    if (!store || !key) return NULL;
    
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use && strcmp(store->pairs[i].key, key) == 0) {
            return store->pairs[i].value;
        }
    }
    return NULL;
}

long long flex_kv_get_int(const FlexKvStore* store, const char* key, long long default_val) {
    const char* val = flex_kv_get_str(store, key);
    if (!val) return default_val;
    char* endptr = NULL;
    long long res = strtoll(val, &endptr, 10);
    return (endptr != val) ? res : default_val;
}

bool flex_kv_get_bool(const FlexKvStore* store, const char* key, bool default_val) {
    const char* val = flex_kv_get_str(store, key);
    if (!val) return default_val;
    
    if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcmp(val, "yes") == 0) {
        return true;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0 || strcmp(val, "no") == 0) {
        return false;
    }
    return default_val;
}

bool flex_kv_remove(FlexKvStore* store, const char* key) {
    if (!store || !key) return false;
    
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use && strcmp(store->pairs[i].key, key) == 0) {
            store->pairs[i].in_use = false;
            store->count--;
            return true;
        }
    }
    return false;
}

bool flex_kv_parse(FlexKvStore* store, const char* text, size_t len) {
    if (!store || !text) return false;
    
    size_t line_start = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i == len || text[i] == '\n') {
            size_t line_len = i - line_start;
            if (line_len > 0) {
                char line[FLEX_KV_MAX_KEY + FLEX_KV_MAX_VAL + 4];
                if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
                memcpy(line, &text[line_start], line_len);
                line[line_len] = '\0';
                flex_str_trim(line);
                
                if (line[0] != '#' && line[0] != '\0') {
                    char* eq = strchr(line, '=');
                    if (eq) {
                        *eq = '\0';
                        char* k = line;
                        char* v = eq + 1;
                        flex_str_trim(k);
                        flex_str_trim(v);
                        flex_kv_set(store, k, v);
                    }
                }
            }
            line_start = i + 1;
        }
    }
    return true;
}

bool flex_kv_contains(const FlexKvStore* store, const char* key) {
    return flex_kv_get_str(store, key) != NULL;
}

void flex_kv_clear(FlexKvStore* store) {
    if (!store) return;
    flex_kv_init(store);
}

double flex_kv_get_float(const FlexKvStore* store, const char* key, double default_val) {
    const char* s = flex_kv_get_str(store, key);
    if (!s) return default_val;
    char* endptr = NULL;
    double val = strtod(s, &endptr);
    if (endptr == s) return default_val;
    return val;
}

bool flex_kv_set_int(FlexKvStore* store, const char* key, long long val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", val);
    return flex_kv_set(store, key, buf);
}

bool flex_kv_set_bool(FlexKvStore* store, const char* key, bool val) {
    return flex_kv_set(store, key, val ? "true" : "false");
}

bool flex_kv_set_float(FlexKvStore* store, const char* key, double val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6f", val);
    return flex_kv_set(store, key, buf);
}

bool flex_kv_copy(FlexKvStore* dest, const FlexKvStore* src) {
    if (!dest || !src) return false;
    memcpy(dest, src, sizeof(FlexKvStore));
    return true;
}

size_t flex_kv_serialize(const FlexKvStore* store, char* out_buf, size_t out_capacity) {
    if (!store || !out_buf || out_capacity == 0) return 0;
    
    size_t written = 0;
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use) {
            int len = snprintf(&out_buf[written], out_capacity - written,
                               "%s=%s\n", store->pairs[i].key, store->pairs[i].value);
            if (len > 0 && written + (size_t)len < out_capacity) {
                written += (size_t)len;
            } else {
                break;
            }
        }
    }
    
    out_buf[written] = '\0';
    return written;
}
