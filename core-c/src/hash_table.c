#include "hash_table.h"
#include <string.h>

void flex_ht_init(FlexHashTable* ht) {
    if (!ht) return;
    memset(ht, 0, sizeof(FlexHashTable));
}

uint32_t flex_ht_hash_fnv1a(const char* key) {
    if (!key) return 0;
    
    uint32_t hash = 2166136261u;
    for (size_t i = 0; key[i] != '\0'; ++i) {
        hash ^= (unsigned char)key[i];
        hash *= 16777619u;
    }
    return hash;
}

bool flex_ht_put(FlexHashTable* ht, const char* key, const char* value) {
    if (!ht || !key || !value) return false;
    
    uint32_t hash = flex_ht_hash_fnv1a(key);
    size_t first_tombstone = FLEX_HT_CAPACITY;
    
    for (size_t i = 0; i < FLEX_HT_CAPACITY; ++i) {
        size_t idx = (hash + i + (i * i)) % FLEX_HT_CAPACITY;
        
        if (ht->entries[idx].state == FLEX_HT_SLOT_OCCUPIED) {
            if (ht->entries[idx].hash == hash && strcmp(ht->entries[idx].key, key) == 0) {
                /* Update existing value */
                strncpy(ht->entries[idx].value, value, FLEX_HT_VAL_LEN - 1);
                ht->entries[idx].value[FLEX_HT_VAL_LEN - 1] = '\0';
                return true;
            }
            ht->collisions++;
        } else if (ht->entries[idx].state == FLEX_HT_SLOT_TOMBSTONE) {
            if (first_tombstone == FLEX_HT_CAPACITY) {
                first_tombstone = idx;
            }
        } else {
            /* Empty slot */
            size_t insert_idx = (first_tombstone != FLEX_HT_CAPACITY) ? first_tombstone : idx;
            FlexHtEntry* e = &ht->entries[insert_idx];
            strncpy(e->key, key, FLEX_HT_KEY_LEN - 1);
            e->key[FLEX_HT_KEY_LEN - 1] = '\0';
            strncpy(e->value, value, FLEX_HT_VAL_LEN - 1);
            e->value[FLEX_HT_VAL_LEN - 1] = '\0';
            e->hash = hash;
            e->state = FLEX_HT_SLOT_OCCUPIED;
            ht->count++;
            return true;
        }
    }
    
    if (first_tombstone != FLEX_HT_CAPACITY) {
        FlexHtEntry* e = &ht->entries[first_tombstone];
        strncpy(e->key, key, FLEX_HT_KEY_LEN - 1);
        e->key[FLEX_HT_KEY_LEN - 1] = '\0';
        strncpy(e->value, value, FLEX_HT_VAL_LEN - 1);
        e->value[FLEX_HT_VAL_LEN - 1] = '\0';
        e->hash = hash;
        e->state = FLEX_HT_SLOT_OCCUPIED;
        ht->count++;
        return true;
    }
    
    return false; /* Table full */
}

const char* flex_ht_get(const FlexHashTable* ht, const char* key) {
    if (!ht || !key) return NULL;
    
    uint32_t hash = flex_ht_hash_fnv1a(key);
    
    for (size_t i = 0; i < FLEX_HT_CAPACITY; ++i) {
        size_t idx = (hash + i + (i * i)) % FLEX_HT_CAPACITY;
        
        if (ht->entries[idx].state == FLEX_HT_SLOT_OCCUPIED) {
            if (ht->entries[idx].hash == hash && strcmp(ht->entries[idx].key, key) == 0) {
                return ht->entries[idx].value;
            }
        } else if (ht->entries[idx].state == FLEX_HT_SLOT_EMPTY) {
            return NULL; /* Stop on empty */
        }
    }
    return NULL;
}

bool flex_ht_contains(const FlexHashTable* ht, const char* key) {
    return flex_ht_get(ht, key) != NULL;
}

bool flex_ht_remove(FlexHashTable* ht, const char* key) {
    if (!ht || !key) return false;
    
    uint32_t hash = flex_ht_hash_fnv1a(key);
    
    for (size_t i = 0; i < FLEX_HT_CAPACITY; ++i) {
        size_t idx = (hash + i + (i * i)) % FLEX_HT_CAPACITY;
        
        if (ht->entries[idx].state == FLEX_HT_SLOT_OCCUPIED) {
            if (ht->entries[idx].hash == hash && strcmp(ht->entries[idx].key, key) == 0) {
                ht->entries[idx].state = FLEX_HT_SLOT_TOMBSTONE;
                ht->count--;
                return true;
            }
        } else if (ht->entries[idx].state == FLEX_HT_SLOT_EMPTY) {
            return false;
        }
    }
    return false;
}

void flex_ht_clear(FlexHashTable* ht) {
    if (!ht) return;
    for (size_t i = 0; i < FLEX_HT_CAPACITY; ++i) {
        ht->entries[i].state = FLEX_HT_SLOT_EMPTY;
    }
    ht->count = 0;
    ht->collisions = 0;
}

