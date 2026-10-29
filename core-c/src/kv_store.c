#include "kv_store.h"
#include "string_util.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

void flex_kv_init(FlexKvStore* store) {
    if (!store) return;
    memset(store, 0, sizeof(FlexKvStore));
}

bool flex_kv_set(FlexKvStore* store, const char* key, const char* value) {
    if (!store || !key || !value) return false;
    
    size_t klen = strlen(key);
    size_t vlen = strlen(value);
    
    /* Strictly validate capacities: never silently truncate */
    if (klen == 0 || klen >= FLEX_KV_MAX_KEY || vlen >= FLEX_KV_MAX_VAL) {
        return false;
    }
    
    /* Update existing pair */
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use && strcmp(store->pairs[i].key, key) == 0) {
            memcpy(store->pairs[i].value, value, vlen + 1);
            return true;
        }
    }
    
    /* Find free slot */
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (!store->pairs[i].in_use) {
            memcpy(store->pairs[i].key, key, klen + 1);
            memcpy(store->pairs[i].value, value, vlen + 1);
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

const char* flex_kv_status_str(FlexKvStatus status) {
    switch (status) {
        case FLEX_KV_OK:
            return "OK";
        case FLEX_KV_ERR_NULL:
            return "Null parameter";
        case FLEX_KV_ERR_NOT_FOUND:
            return "Key not found";
        case FLEX_KV_ERR_INVALID_SYNTAX:
            return "Invalid syntax";
        case FLEX_KV_ERR_OVERFLOW:
            return "Numeric overflow";
        case FLEX_KV_ERR_CAPACITY:
            return "Capacity exceeded";
        default:
            return "Unknown error";
    }
}

FlexKvStatus flex_kv_parse_int(const FlexKvStore* store, const char* key, long long* out_val) {
    if (!store || !key || !out_val) return FLEX_KV_ERR_NULL;
    const char* val = flex_kv_get_str(store, key);
    if (!val) return FLEX_KV_ERR_NOT_FOUND;
    
    while (isspace((unsigned char)*val)) val++;
    if (*val == '\0') return FLEX_KV_ERR_INVALID_SYNTAX;
    
    const char* p = val;
    if (*p == '-' || *p == '+') p++;
    if (!isdigit((unsigned char)*p)) return FLEX_KV_ERR_INVALID_SYNTAX;
    
    errno = 0;
    char* endptr = NULL;
    long long res = strtoll(val, &endptr, 10);
    if (errno == ERANGE) {
        return FLEX_KV_ERR_OVERFLOW;
    }
    
    while (isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') {
        return FLEX_KV_ERR_INVALID_SYNTAX;
    }
    
    *out_val = res;
    return FLEX_KV_OK;
}

FlexKvStatus flex_kv_parse_float(const FlexKvStore* store, const char* key, double* out_val) {
    if (!store || !key || !out_val) return FLEX_KV_ERR_NULL;
    const char* val = flex_kv_get_str(store, key);
    if (!val) return FLEX_KV_ERR_NOT_FOUND;
    
    const char* s = val;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return FLEX_KV_ERR_INVALID_SYNTAX;
    
    /* Reject NaN and Infinity representations */
    if (tolower((unsigned char)s[0]) == 'n' || tolower((unsigned char)s[0]) == 'i') {
        return FLEX_KV_ERR_INVALID_SYNTAX;
    }
    if ((s[0] == '+' || s[0] == '-') && (tolower((unsigned char)s[1]) == 'n' || tolower((unsigned char)s[1]) == 'i')) {
        return FLEX_KV_ERR_INVALID_SYNTAX;
    }
    
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    
    bool has_digits = false;
    double int_part = 0.0;
    while (isdigit((unsigned char)*s)) {
        has_digits = true;
        int_part = int_part * 10.0 + (*s - '0');
        s++;
    }
    
    double frac_part = 0.0;
    double divisor = 1.0;
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) {
            has_digits = true;
            divisor *= 10.0;
            frac_part += (*s - '0') / divisor;
            s++;
        }
    }
    if (!has_digits) return FLEX_KV_ERR_INVALID_SYNTAX;
    
    double result = (int_part + frac_part) * sign;
    
    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '-') { exp_sign = -1; s++; }
        else if (*s == '+') { s++; }
        int exp_val = 0;
        bool has_exp_digits = false;
        while (isdigit((unsigned char)*s)) {
            has_exp_digits = true;
            exp_val = exp_val * 10 + (*s - '0');
            if (exp_val > 308) return FLEX_KV_ERR_OVERFLOW;
            s++;
        }
        if (!has_exp_digits) return FLEX_KV_ERR_INVALID_SYNTAX;
        result *= pow(10.0, exp_sign * exp_val);
    }
    
    while (isspace((unsigned char)*s)) s++;
    if (*s != '\0') return FLEX_KV_ERR_INVALID_SYNTAX;
    
    if (isnan(result) || isinf(result)) {
        return FLEX_KV_ERR_INVALID_SYNTAX;
    }
    
    *out_val = result;
    return FLEX_KV_OK;
}

FlexKvStatus flex_kv_parse_bool(const FlexKvStore* store, const char* key, bool* out_val) {
    if (!store || !key || !out_val) return FLEX_KV_ERR_NULL;
    const char* val = flex_kv_get_str(store, key);
    if (!val) return FLEX_KV_ERR_NOT_FOUND;
    
    if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcmp(val, "yes") == 0 || strcmp(val, "on") == 0) {
        *out_val = true;
        return FLEX_KV_OK;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0 || strcmp(val, "no") == 0 || strcmp(val, "off") == 0) {
        *out_val = false;
        return FLEX_KV_OK;
    }
    return FLEX_KV_ERR_INVALID_SYNTAX;
}

long long flex_kv_get_int(const FlexKvStore* store, const char* key, long long default_val) {
    long long res = 0;
    if (flex_kv_parse_int(store, key, &res) == FLEX_KV_OK) {
        return res;
    }
    return default_val;
}

bool flex_kv_get_bool(const FlexKvStore* store, const char* key, bool default_val) {
    bool res = false;
    if (flex_kv_parse_bool(store, key, &res) == FLEX_KV_OK) {
        return res;
    }
    return default_val;
}

double flex_kv_get_float(const FlexKvStore* store, const char* key, double default_val) {
    double res = 0.0;
    if (flex_kv_parse_float(store, key, &res) == FLEX_KV_OK) {
        return res;
    }
    return default_val;
}

bool flex_kv_remove(FlexKvStore* store, const char* key) {
    if (!store || !key) return false;
    
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use && strcmp(store->pairs[i].key, key) == 0) {
            store->pairs[i].in_use = false;
            store->pairs[i].key[0] = '\0';
            store->pairs[i].value[0] = '\0';
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
                if (line_len >= FLEX_KV_MAX_KEY + FLEX_KV_MAX_VAL + 4) {
                    return false; /* Oversized line: reject to avoid truncation */
                }
                char line[FLEX_KV_MAX_KEY + FLEX_KV_MAX_VAL + 4];
                memcpy(line, &text[line_start], line_len);
                line[line_len] = '\0';
                flex_str_trim(line);
                
                if (line[0] != '#' && line[0] != '\0') {
                    char* eq = strchr(line, '=');
                    if (!eq) {
                        return false; /* Malformed non-comment line without '=' */
                    }
                    *eq = '\0';
                    char* k = line;
                    char* v = eq + 1;
                    flex_str_trim(k);
                    flex_str_trim(v);
                    if (!flex_kv_set(store, k, v)) {
                        return false; /* Key/val capacity overflow or store full */
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

bool flex_kv_set_int(FlexKvStore* store, const char* key, long long val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", val);
    return flex_kv_set(store, key, buf);
}

bool flex_kv_set_bool(FlexKvStore* store, const char* key, bool val) {
    return flex_kv_set(store, key, val ? "true" : "false");
}

bool flex_kv_set_float(FlexKvStore* store, const char* key, double val) {
    if (isnan(val) || isinf(val)) return false;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", val);
    /* Enforce ASCII decimal dot across all locales */
    for (char* p = buf; *p; ++p) {
        if (*p == ',') *p = '.';
    }
    return flex_kv_set(store, key, buf);
}

bool flex_kv_copy(FlexKvStore* dest, const FlexKvStore* src) {
    if (!dest || !src) return false;
    memcpy(dest, src, sizeof(FlexKvStore));
    return true;
}

size_t flex_kv_serialize(const FlexKvStore* store, char* out_buf, size_t out_capacity) {
    if (!store || !out_buf || out_capacity == 0) return 0;
    
    /* Calculate exact required size to guarantee atomic write and prevent partial truncation */
    size_t required = 0;
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use) {
            required += strlen(store->pairs[i].key) + 1 + strlen(store->pairs[i].value) + 1;
        }
    }
    required += 1; /* trailing null byte */
    
    if (required > out_capacity) {
        out_buf[0] = '\0';
        return 0;
    }
    
    size_t written = 0;
    for (size_t i = 0; i < FLEX_KV_MAX_PAIRS; ++i) {
        if (store->pairs[i].in_use) {
            int len = snprintf(&out_buf[written], out_capacity - written,
                               "%s=%s\n", store->pairs[i].key, store->pairs[i].value);
            if (len > 0) {
                written += (size_t)len;
            }
        }
    }
    
    out_buf[written] = '\0';
    return written;
}
