#include "regex_matcher.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static bool char_match(char p, char c, bool case_sensitive) {
    if (p == '?') return c != '\0';
    if (!case_sensitive) {
        return tolower((unsigned char)p) == tolower((unsigned char)c);
    }
    return p == c;
}

bool flex_regex_glob_match(const char* pattern, const char* str, bool case_sensitive) {
    if (!pattern || !str) return false;
    
    const char* p = pattern;
    const char* s = str;
    const char* star_p = NULL;
    const char* star_s = NULL;
    
    while (*s != '\0') {
        if (*p == '*') {
            star_p = p++;
            star_s = s;
        } else if (char_match(*p, *s, case_sensitive)) {
            p++;
            s++;
        } else if (star_p) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return false;
        }
    }
    
    while (*p == '*') p++;
    return *p == '\0';
}

bool flex_regex_is_ipv4(const char* str) {
    if (!str || str[0] == '\0') return false;
    
    int octets = 0;
    const char* p = str;
    
    while (*p != '\0') {
        if (!isdigit((unsigned char)*p)) return false;
        
        char* endptr = NULL;
        long val = strtol(p, &endptr, 10);
        if (endptr == p || val < 0 || val > 255) return false;
        
        /* Disallow leading zeros for multi-digit numbers (e.g. "01") */
        if (endptr - p > 1 && *p == '0') return false;
        
        octets++;
        p = endptr;
        
        if (*p == '.') {
            p++;
            if (*p == '\0') return false; /* Trailing dot */
        } else if (*p != '\0') {
            return false;
        }
    }
    
    return octets == 4;
}

size_t flex_regex_substitute_vars(const char* template_str,
                                  flex_regex_var_resolver_fn resolver,
                                  void* user_data,
                                  char* out_buf,
                                  size_t out_capacity) {
    if (!template_str || !out_buf || out_capacity == 0) return 0;
    
    size_t written = 0;
    const char* p = template_str;
    
    while (*p != '\0' && written + 1 < out_capacity) {
        if (p[0] == '{' && p[1] == '{') {
            const char* var_start = p + 2;
            const char* var_end = strstr(var_start, "}}");
            if (var_end) {
                char var_name[64];
                size_t v_len = (size_t)(var_end - var_start);
                if (v_len < sizeof(var_name)) {
                    memcpy(var_name, var_start, v_len);
                    var_name[v_len] = '\0';
                    
                    const char* replacement = resolver ? resolver(var_name, user_data) : NULL;
                    if (replacement) {
                        size_t r_len = strlen(replacement);
                        if (written + r_len < out_capacity) {
                            memcpy(&out_buf[written], replacement, r_len);
                            written += r_len;
                        }
                    }
                    p = var_end + 2;
                    continue;
                }
            }
        }
        out_buf[written++] = *p++;
    }
    
    out_buf[written] = '\0';
    return written;
}

