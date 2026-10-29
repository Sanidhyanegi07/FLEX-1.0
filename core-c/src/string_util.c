#include "string_util.h"
#include <string.h>
#include <ctype.h>

void flex_str_trim(char* str) {
    if (!str || str[0] == '\0') return;

    /* Trim trailing whitespace */
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }

    /* Locate first non-whitespace character */
    size_t start = 0;
    while (str[start] != '\0' && isspace((unsigned char)str[start])) {
        start++;
    }

    /* Shift string left if leading spaces exist */
    if (start > 0) {
        memmove(str, &str[start], len - start + 1);
    }
}

size_t flex_str_tolower(const char* src, char* dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    size_t i = 0;
    while (src[i] != '\0' && i + 1 < dst_cap) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
    return i;
}

int flex_str_casecmp(const char* s1, const char* s2) {
    if (s1 == s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    while (*s1 != '\0' && *s2 != '\0') {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

const char* flex_str_casestr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (needle[0] == '\0') return haystack;

    size_t needle_len = strlen(needle);
    int first = tolower((unsigned char)needle[0]);

    for (const char* h = haystack; *h != '\0'; ++h) {
        if (tolower((unsigned char)*h) == first) {
            size_t j = 1;
            while (j < needle_len && h[j] != '\0' &&
                   tolower((unsigned char)h[j]) == tolower((unsigned char)needle[j])) {
                j++;
            }
            if (j == needle_len) {
                return h;
            }
        }
    }
    return NULL;
}

bool flex_str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    size_t plen = strlen(prefix);
    return strncmp(str, prefix, plen) == 0;
}

bool flex_str_ends_with(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t slen = strlen(str);
    size_t suflen = strlen(suffix);
    if (suflen > slen) return false;
    return strcmp(&str[slen - suflen], suffix) == 0;
}

size_t flex_str_copy(char* dst, size_t dst_cap, const char* src) {
    if (!dst || dst_cap == 0) return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    size_t i = 0;
    while (src[i] != '\0' && i + 1 < dst_cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

