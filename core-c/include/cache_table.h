#ifndef FLEX_CACHE_TABLE_H
#define FLEX_CACHE_TABLE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_CACHE_MAX_ENTRIES  64
#define FLEX_CACHE_KEY_LEN      64
#define FLEX_CACHE_VALUE_LEN    256

typedef struct {
    char key[FLEX_CACHE_KEY_LEN];
    char value[FLEX_CACHE_VALUE_LEN];
    unsigned long created_at_sec;
    unsigned long ttl_sec;
    unsigned long last_accessed_seq;
    bool is_valid;
} FlexCacheEntry;

typedef struct {
    FlexCacheEntry entries[FLEX_CACHE_MAX_ENTRIES];
    size_t count;
    unsigned long access_counter;
    size_t hits;
    size_t misses;
    size_t evictions;
} FlexCacheTable;

/**
 * Initialize cache table.
 */
void flex_cache_init(FlexCacheTable* table);

/**
 * Put a key-value pair into cache with a TTL in seconds.
 * Evicts LRU entry when full.
 */
bool flex_cache_put(FlexCacheTable* table,
                    const char* key,
                    const char* value,
                    unsigned long ttl_sec,
                    unsigned long now_sec);

/**
 * Get value by key from cache. Returns true if hit and not expired.
 */
bool flex_cache_get(FlexCacheTable* table,
                    const char* key,
                    char* out_value,
                    size_t out_capacity,
                    unsigned long now_sec);

/**
 * Invalidate a single key or clear expired entries.
 */
bool flex_cache_remove(FlexCacheTable* table, const char* key);

/**
 * Purge expired entries according to current time.
 */
size_t flex_cache_purge_expired(FlexCacheTable* table, unsigned long now_sec);

/**
 * Clear all cache entries.
 */
void flex_cache_clear(FlexCacheTable* table);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_CACHE_TABLE_H */

