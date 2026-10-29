#ifndef FLEX_STRING_UTIL_H
#define FLEX_STRING_UTIL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * In-place whitespace trimming (strips leading and trailing spaces, tabs, CR, LF).
 */
void flex_str_trim(char* str);

/**
 * Converts src string to lowercase into dst buffer, bounded by dst_cap.
 * Always guarantees null-termination if dst_cap > 0.
 * Returns length of formatted string.
 */
size_t flex_str_tolower(const char* src, char* dst, size_t dst_cap);

/**
 * Case-insensitive string comparison (equivalent to POSIX strcasecmp).
 * Returns negative if s1 < s2, positive if s1 > s2, 0 if s1 == s2.
 */
int flex_str_casecmp(const char* s1, const char* s2);

/**
 * Case-insensitive substring search (equivalent to POSIX strcasestr).
 * Returns pointer to start of first occurrence of needle in haystack, or NULL.
 */
const char* flex_str_casestr(const char* haystack, const char* needle);

/**
 * Checks if str starts with prefix (case-sensitive).
 */
bool flex_str_starts_with(const char* str, const char* prefix);

/**
 * Checks if str ends with suffix (case-sensitive).
 */
bool flex_str_ends_with(const char* str, const char* suffix);

/**
 * Safe bounded string copy guaranteeing null-termination.
 * Returns length of copied string.
 */
size_t flex_str_copy(char* dst, size_t dst_cap, const char* src);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_STRING_UTIL_H */

