#include "json_validator.h"
#include "ai_stream_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    const char* src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    int depth;
} JsonParser;

static void skip_ws(JsonParser* p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == '\n') {
            p->line++;
            p->col = 1;
            p->pos++;
        } else if (isspace((unsigned char)c)) {
            p->col++;
            p->pos++;
        } else {
            break;
        }
    }
}

static char peek_char(JsonParser* p) {
    skip_ws(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static char next_char(JsonParser* p) {
    skip_ws(p);
    if (p->pos >= p->len) return '\0';
    p->col++;
    return p->src[p->pos++];
}

static bool parse_value(JsonParser* p, FlexJsonError* err);

static bool parse_string(JsonParser* p, FlexJsonError* err) {
    if (p->pos >= p->len || p->src[p->pos] != '"') {
        if (err) {
            err->status = FLEX_JSON_ERR_UNEXPECTED_CHAR;
            err->error_offset = p->pos;
            err->line = p->line;
            err->column = p->col;
            err->message = "Expected opening quote for string";
        }
        return false;
    }
    p->pos++;
    p->col++;
    
    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        p->col++;
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (p->pos >= p->len) break;
            p->pos++;
            p->col++;
        }
    }
    
    if (err) {
        err->status = FLEX_JSON_ERR_UNTERMINATED_STRING;
        err->error_offset = p->pos;
        err->line = p->line;
        err->column = p->col;
        err->message = "Unterminated string literal";
    }
    return false;
}

static bool parse_number(JsonParser* p, FlexJsonError* err) {
    size_t start = p->pos;
    if (p->pos < p->len && (p->src[p->pos] == '-' || p->src[p->pos] == '+')) {
        p->pos++;
        p->col++;
    }
    
    bool has_digits = false;
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) {
        has_digits = true;
        p->pos++;
        p->col++;
    }
    
    if (!has_digits) {
        if (err) {
            err->status = FLEX_JSON_ERR_INVALID_NUMBER;
            err->error_offset = start;
            err->line = p->line;
            err->column = p->col;
            err->message = "Invalid number format";
        }
        return false;
    }
    
    if (p->pos < p->len && p->src[p->pos] == '.') {
        p->pos++;
        p->col++;
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) {
            p->pos++;
            p->col++;
        }
    }
    
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        p->pos++;
        p->col++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) {
            p->pos++;
            p->col++;
        }
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) {
            p->pos++;
            p->col++;
        }
    }
    
    return true;
}

static bool parse_object(JsonParser* p, FlexJsonError* err) {
    if (p->depth >= FLEX_JSON_MAX_DEPTH) {
        if (err) {
            err->status = FLEX_JSON_ERR_MAX_DEPTH_EXCEEDED;
            err->error_offset = p->pos;
            err->line = p->line;
            err->column = p->col;
            err->message = "Exceeded maximum nested JSON depth";
        }
        return false;
    }
    p->depth++;
    next_char(p); /* consume '{' */
    
    skip_ws(p);
    if (peek_char(p) == '}') {
        next_char(p);
        p->depth--;
        return true;
    }
    
    while (p->pos < p->len) {
        skip_ws(p);
        if (peek_char(p) != '"') {
            if (err) {
                err->status = FLEX_JSON_ERR_UNEXPECTED_CHAR;
                err->error_offset = p->pos;
                err->line = p->line;
                err->column = p->col;
                err->message = "Expected string object key";
            }
            return false;
        }
        if (!parse_string(p, err)) return false;
        
        skip_ws(p);
        if (peek_char(p) != ':') {
            if (err) {
                err->status = FLEX_JSON_ERR_EXPECTED_COLON;
                err->error_offset = p->pos;
                err->line = p->line;
                err->column = p->col;
                err->message = "Expected ':' after key";
            }
            return false;
        }
        next_char(p); /* consume ':' */
        
        if (!parse_value(p, err)) return false;
        
        skip_ws(p);
        char c = peek_char(p);
        if (c == '}') {
            next_char(p);
            p->depth--;
            return true;
        }
        if (c == ',') {
            next_char(p);
        } else {
            if (err) {
                err->status = FLEX_JSON_ERR_EXPECTED_COMMA;
                err->error_offset = p->pos;
                err->line = p->line;
                err->column = p->col;
                err->message = "Expected ',' or '}' in object";
            }
            return false;
        }
    }
    
    if (err) {
        err->status = FLEX_JSON_ERR_UNTERMINATED_OBJECT;
        err->error_offset = p->pos;
        err->line = p->line;
        err->column = p->col;
        err->message = "Unterminated JSON object";
    }
    return false;
}

static bool parse_array(JsonParser* p, FlexJsonError* err) {
    if (p->depth >= FLEX_JSON_MAX_DEPTH) {
        if (err) {
            err->status = FLEX_JSON_ERR_MAX_DEPTH_EXCEEDED;
            err->error_offset = p->pos;
            err->line = p->line;
            err->column = p->col;
            err->message = "Exceeded maximum nested JSON depth";
        }
        return false;
    }
    p->depth++;
    next_char(p); /* consume '[' */
    
    skip_ws(p);
    if (peek_char(p) == ']') {
        next_char(p);
        p->depth--;
        return true;
    }
    
    while (p->pos < p->len) {
        if (!parse_value(p, err)) return false;
        
        skip_ws(p);
        char c = peek_char(p);
        if (c == ']') {
            next_char(p);
            p->depth--;
            return true;
        }
        if (c == ',') {
            next_char(p);
        } else {
            if (err) {
                err->status = FLEX_JSON_ERR_EXPECTED_COMMA;
                err->error_offset = p->pos;
                err->line = p->line;
                err->column = p->col;
                err->message = "Expected ',' or ']' in array";
            }
            return false;
        }
    }
    
    if (err) {
        err->status = FLEX_JSON_ERR_UNTERMINATED_ARRAY;
        err->error_offset = p->pos;
        err->line = p->line;
        err->column = p->col;
        err->message = "Unterminated JSON array";
    }
    return false;
}

static bool parse_value(JsonParser* p, FlexJsonError* err) {
    skip_ws(p);
    char c = peek_char(p);
    if (c == '{') return parse_object(p, err);
    if (c == '[') return parse_array(p, err);
    if (c == '"') return parse_string(p, err);
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(p, err);
    
    /* Literals: true, false, null */
    if (p->pos + 4 <= p->len && strncmp(&p->src[p->pos], "true", 4) == 0) {
        p->pos += 4;
        p->col += 4;
        return true;
    }
    if (p->pos + 5 <= p->len && strncmp(&p->src[p->pos], "false", 5) == 0) {
        p->pos += 5;
        p->col += 5;
        return true;
    }
    if (p->pos + 4 <= p->len && strncmp(&p->src[p->pos], "null", 4) == 0) {
        p->pos += 4;
        p->col += 4;
        return true;
    }
    
    if (err) {
        err->status = FLEX_JSON_ERR_INVALID_LITERAL;
        err->error_offset = p->pos;
        err->line = p->line;
        err->column = p->col;
        err->message = "Invalid JSON literal value";
    }
    return false;
}

bool flex_json_validate(const char* json_str, size_t len, FlexJsonError* out_error) {
    if (out_error) {
        memset(out_error, 0, sizeof(FlexJsonError));
    }
    if (!json_str) {
        if (out_error) {
            out_error->status = FLEX_JSON_ERR_NULL_INPUT;
            out_error->message = "Input pointer is null";
        }
        return false;
    }
    if (len == 0) {
        if (out_error) {
            out_error->status = FLEX_JSON_ERR_EMPTY_INPUT;
            out_error->message = "Input length is zero";
        }
        return false;
    }
    
    JsonParser p;
    p.src = json_str;
    p.len = len;
    p.pos = 0;
    p.line = 1;
    p.col = 1;
    p.depth = 0;
    
    if (!parse_value(&p, out_error)) {
        return false;
    }
    
    skip_ws(&p);
    if (p.pos < p.len) {
        if (out_error) {
            out_error->status = FLEX_JSON_ERR_UNEXPECTED_CHAR;
            out_error->error_offset = p.pos;
            out_error->line = p.line;
            out_error->column = p.col;
            out_error->message = "Trailing characters after JSON value";
        }
        return false;
    }
    
    return true;
}

FlexJsonValueType flex_json_root_type(const char* json_str, size_t len) {
    if (!json_str || len == 0) return FLEX_JSON_VAL_UNKNOWN;
    
    size_t i = 0;
    while (i < len && isspace((unsigned char)json_str[i])) i++;
    if (i >= len) return FLEX_JSON_VAL_UNKNOWN;
    
    char c = json_str[i];
    if (c == '{') return FLEX_JSON_VAL_OBJECT;
    if (c == '[') return FLEX_JSON_VAL_ARRAY;
    if (c == '"') return FLEX_JSON_VAL_STRING;
    if (c == '-' || isdigit((unsigned char)c)) return FLEX_JSON_VAL_NUMBER;
    if (i + 4 <= len && strncmp(&json_str[i], "true", 4) == 0) return FLEX_JSON_VAL_BOOL;
    if (i + 5 <= len && strncmp(&json_str[i], "false", 5) == 0) return FLEX_JSON_VAL_BOOL;
    if (i + 4 <= len && strncmp(&json_str[i], "null", 4) == 0) return FLEX_JSON_VAL_NULL;
    return FLEX_JSON_VAL_UNKNOWN;
}

bool flex_json_get_string(const char* json_str,
                          size_t len,
                          const char* key,
                          char* out_buf,
                          size_t out_buf_len) {
    if (!json_str || !key || !out_buf || out_buf_len == 0 || len == 0) return false;
    
    char needle[FLEX_JSON_MAX_KEY_LEN + 4];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t needle_len = strlen(needle);
    
    const char* p = json_str;
    const char* end = json_str + len;
    
    while ((p = strstr(p, needle)) != NULL && p < end) {
        const char* colon = p + needle_len;
        while (colon < end && isspace((unsigned char)*colon)) colon++;
        if (colon < end && *colon == ':') {
            colon++;
            while (colon < end && isspace((unsigned char)*colon)) colon++;
            if (colon < end && *colon == '"') {
                const char* val_start = colon + 1;
                const char* q = val_start;
                while (q < end && *q != '"') {
                    if (*q == '\\' && q + 1 < end) q++;
                    q++;
                }
                if (q < end && *q == '"') {
                    flex_json_unescape_string(val_start, (size_t)(q - val_start), out_buf, out_buf_len);
                    return true;
                }
            }
        }
        p += needle_len;
    }
    return false;
}

bool flex_json_get_int(const char* json_str,
                       size_t len,
                       const char* key,
                       long long* out_val) {
    if (!json_str || !key || !out_val || len == 0) return false;
    
    char needle[FLEX_JSON_MAX_KEY_LEN + 4];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t needle_len = strlen(needle);
    
    const char* p = json_str;
    const char* end = json_str + len;
    
    while ((p = strstr(p, needle)) != NULL && p < end) {
        const char* colon = p + needle_len;
        while (colon < end && isspace((unsigned char)*colon)) colon++;
        if (colon < end && *colon == ':') {
            colon++;
            while (colon < end && isspace((unsigned char)*colon)) colon++;
            if (colon < end && (isdigit((unsigned char)*colon) || *colon == '-')) {
                char* endptr = NULL;
                long long val = strtoll(colon, &endptr, 10);
                if (endptr != colon) {
                    *out_val = val;
                    return true;
                }
            }
        }
        p += needle_len;
    }
    return false;
}

bool flex_json_get_bool(const char* json_str,
                        size_t len,
                        const char* key,
                        bool* out_val) {
    if (!json_str || !key || !out_val || len == 0) return false;
    
    char needle[FLEX_JSON_MAX_KEY_LEN + 4];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t needle_len = strlen(needle);
    
    const char* p = json_str;
    const char* end = json_str + len;
    
    while ((p = strstr(p, needle)) != NULL && p < end) {
        const char* colon = p + needle_len;
        while (colon < end && isspace((unsigned char)*colon)) colon++;
        if (colon < end && *colon == ':') {
            colon++;
            while (colon < end && isspace((unsigned char)*colon)) colon++;
            if (colon + 4 <= end && strncmp(colon, "true", 4) == 0) {
                *out_val = true;
                return true;
            }
            if (colon + 5 <= end && strncmp(colon, "false", 5) == 0) {
                *out_val = false;
                return true;
            }
        }
        p += needle_len;
    }
    return false;
}
