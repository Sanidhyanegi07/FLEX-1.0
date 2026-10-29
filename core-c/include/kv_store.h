#ifndef FLEX_KV_STORE_H
#define FLEX_KV_STORE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_KV_MAX_PAIRS  64
#define FLEX_KV_MAX_KEY    48
#define FLEX_KV_MAX_VAL    128

typedef enum {
    FLEX_KV_OK = 0,
    FLEX_KV_ERR_NULL = 1,
    FLEX_KV_ERR_NOT_FOUND = 2,
    FLEX_KV_ERR_INVALID_SYNTAX = 3,
    FLEX_KV_ERR_OVERFLOW = 4,
    FLEX_KV_ERR_CAPACITY = 5
} FlexKvStatus;

typedef struct {
    char key[FLEX_KV_MAX_KEY];
    char value[FLEX_KV_MAX_VAL];
    bool in_use;
} FlexKvPair;

typedef struct {
    FlexKvPair pairs[FLEX_KV_MAX_PAIRS];
    size_t count;
} FlexKvStore;

/**
 * Initialize key-value store.
 */
void flex_kv_init(FlexKvStore* store);

/**
 * Parse key-value config string (e.g. "model=qwen2.5-coder:3b\nport=11434\n").
 */
bool flex_kv_parse(FlexKvStore* store, const char* text, size_t len);

/**
 * Set or update a key-value pair.
 */
bool flex_kv_set(FlexKvStore* store, const char* key, const char* value);

/**
 * Get string value for a key. Returns NULL if not found.
 */
const char* flex_kv_get_str(const FlexKvStore* store, const char* key);

/**
 * Get integer value for a key with fallback default.
 */
long long flex_kv_get_int(const FlexKvStore* store, const char* key, long long default_val);

/**
 * Get boolean value for a key ("true", "1", "yes" -> true).
 */
bool flex_kv_get_bool(const FlexKvStore* store, const char* key, bool default_val);

/**
 * Remove a key from store.
 */
bool flex_kv_remove(FlexKvStore* store, const char* key);

/**
 * Check if store contains a key.
 */
bool flex_kv_contains(const FlexKvStore* store, const char* key);

/**
 * Clear all key-value pairs from store.
 */
void flex_kv_clear(FlexKvStore* store);

/**
 * Get floating-point value for a key with fallback default.
 */
double flex_kv_get_float(const FlexKvStore* store, const char* key, double default_val);

/**
 * Set integer value for a key.
 */
bool flex_kv_set_int(FlexKvStore* store, const char* key, long long val);

/**
 * Set boolean value for a key.
 */
bool flex_kv_set_bool(FlexKvStore* store, const char* key, bool val);

/**
 * Set floating-point value for a key.
 */
bool flex_kv_set_float(FlexKvStore* store, const char* key, double val);

/**
 * Copy all pairs from src to dest.
 */
bool flex_kv_copy(FlexKvStore* dest, const FlexKvStore* src);

/**
 * Serialize store contents to "key=value\n" format.
 */
size_t flex_kv_serialize(const FlexKvStore* store, char* out_buf, size_t out_capacity);

/**
 * Converts status code to human-readable string.
 */
const char* flex_kv_status_str(FlexKvStatus status);

/**
 * Strict integer parser distinguishing missing key, syntax error, and overflow.
 */
FlexKvStatus flex_kv_parse_int(const FlexKvStore* store, const char* key, long long* out_val);

/**
 * Strict float parser rejecting NaN/infinity and locale variations.
 */
FlexKvStatus flex_kv_parse_float(const FlexKvStore* store, const char* key, double* out_val);

/**
 * Strict boolean parser ("true", "1", "yes", "on" / "false", "0", "no", "off").
 */
FlexKvStatus flex_kv_parse_bool(const FlexKvStore* store, const char* key, bool* out_val);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_KV_STORE_H */
