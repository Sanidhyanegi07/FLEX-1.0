#ifndef FLEX_REGEX_MATCHER_H
#define FLEX_REGEX_MATCHER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Match a string against a glob pattern containing '*' (any sequence) and '?' (any single char).
 * Case insensitive if case_sensitive is false.
 */
bool flex_regex_glob_match(const char* pattern, const char* str, bool case_sensitive);

/**
 * Check if a string matches a strict IPv4 address pattern (e.g. "127.0.0.1").
 */
bool flex_regex_is_ipv4(const char* str);

/**
 * Substitute template placeholders like "{{model}}" or "{{name}}" in template_str.
 * Resolves keys using getter callback function.
 */
typedef const char* (*flex_regex_var_resolver_fn)(const char* var_name, void* user_data);

size_t flex_regex_substitute_vars(const char* template_str,
                                  flex_regex_var_resolver_fn resolver,
                                  void* user_data,
                                  char* out_buf,
                                  size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_REGEX_MATCHER_H */

