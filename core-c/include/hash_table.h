#ifndef FLEX_HASH_TABLE_H
#define FLEX_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_HT_CAPACITY 64
#define FLEX_HT_KEY_LEN  48
#define FLEX_HT_VAL_LEN  128

typedef enum {
    FLEX_HT_SLOT_EMPTY = 0,
    FLEX_HT_SLOT_OCCUPIED,
    FLEX_HT_SLOT_TOMBSTONE
} FlexHtSlotState;

typedef struct {
    char key[FLEX_HT_KEY_LEN];
    char value[FLEX_HT_VAL_LEN];
    uint32_t hash;
    FlexHtSlotState state;
} FlexHtEntry;

typedef struct {
    FlexHtEntry entries[FLEX_HT_CAPACITY];
    size_t count;
    size_t collisions;
} FlexHashTable;

/**
 * Initialize hash table.
 */
void flex_ht_init(FlexHashTable* ht);

/**
 * Compute 32-bit FNV-1a hash of a string.
 */
uint32_t flex_ht_hash_fnv1a(const char* key);

/**
 * Insert or update key-value pair in hash table.
 * Returns true if inserted/updated, false if table full.
 */
bool flex_ht_put(FlexHashTable* ht, const char* key, const char* value);

/**
 * Look up key in hash table. Returns pointer to value or NULL if not found.
 */
const char* flex_ht_get(const FlexHashTable* ht, const char* key);

/**
 * Check if key exists in hash table.
 */
bool flex_ht_contains(const FlexHashTable* ht, const char* key);

/**
 * Remove key from hash table. Returns true if removed, false if not found.
 */
bool flex_ht_remove(FlexHashTable* ht, const char* key);

/**
 * Clear all entries from hash table.
 */
void flex_ht_clear(FlexHashTable* ht);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_HASH_TABLE_H */

