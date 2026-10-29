#include "cache_table.h"
#include <string.h>

void flex_cache_init(FlexCacheTable* table) {
    if (!table) return;
    memset(table, 0, sizeof(FlexCacheTable));
}

bool flex_cache_put(FlexCacheTable* table,
                    const char* key,
                    const char* value,
                    unsigned long ttl_sec,
                    unsigned long now_sec) {
    if (!table || !key || !value) return false;
    
    table->access_counter++;
    
    /* Check if key already exists */
    for (size_t i = 0; i < FLEX_CACHE_MAX_ENTRIES; ++i) {
        if (table->entries[i].is_valid && strcmp(table->entries[i].key, key) == 0) {
            strncpy(table->entries[i].value, value, FLEX_CACHE_VALUE_LEN - 1);
            table->entries[i].value[FLEX_CACHE_VALUE_LEN - 1] = '\0';
            table->entries[i].created_at_sec = now_sec;
            table->entries[i].ttl_sec = ttl_sec;
            table->entries[i].last_accessed_seq = table->access_counter;
            return true;
        }
    }
    
    /* Find free slot or evict LRU */
    size_t victim_idx = 0;
    bool found_free = false;
    unsigned long oldest_seq = 0xFFFFFFFFUL;
    
    for (size_t i = 0; i < FLEX_CACHE_MAX_ENTRIES; ++i) {
        if (!table->entries[i].is_valid) {
            victim_idx = i;
            found_free = true;
            break;
        }
        if (table->entries[i].last_accessed_seq < oldest_seq) {
            oldest_seq = table->entries[i].last_accessed_seq;
            victim_idx = i;
        }
    }
    
    if (!found_free) {
        table->evictions++;
    } else {
        table->count++;
    }
    
    FlexCacheEntry* e = &table->entries[victim_idx];
    strncpy(e->key, key, FLEX_CACHE_KEY_LEN - 1);
    e->key[FLEX_CACHE_KEY_LEN - 1] = '\0';
    strncpy(e->value, value, FLEX_CACHE_VALUE_LEN - 1);
    e->value[FLEX_CACHE_VALUE_LEN - 1] = '\0';
    e->created_at_sec = now_sec;
    e->ttl_sec = ttl_sec;
    e->last_accessed_seq = table->access_counter;
    e->is_valid = true;
    
    return true;
}

bool flex_cache_get(FlexCacheTable* table,
                    const char* key,
                    char* out_value,
                    size_t out_capacity,
                    unsigned long now_sec) {
    if (!table || !key) return false;
    
    for (size_t i = 0; i < FLEX_CACHE_MAX_ENTRIES; ++i) {
        if (table->entries[i].is_valid && strcmp(table->entries[i].key, key) == 0) {
            /* Check expiry */
            if (table->entries[i].ttl_sec > 0 &&
                (now_sec - table->entries[i].created_at_sec) > table->entries[i].ttl_sec) {
                table->entries[i].is_valid = false;
                table->count--;
                table->misses++;
                return false;
            }
            
            table->access_counter++;
            table->entries[i].last_accessed_seq = table->access_counter;
            table->hits++;
            
            if (out_value && out_capacity > 0) {
                strncpy(out_value, table->entries[i].value, out_capacity - 1);
                out_value[out_capacity - 1] = '\0';
            }
            return true;
        }
    }
    
    table->misses++;
    return false;
}

bool flex_cache_remove(FlexCacheTable* table, const char* key) {
    if (!table || !key) return false;
    
    for (size_t i = 0; i < FLEX_CACHE_MAX_ENTRIES; ++i) {
        if (table->entries[i].is_valid && strcmp(table->entries[i].key, key) == 0) {
            table->entries[i].is_valid = false;
            if (table->count > 0) table->count--;
            return true;
        }
    }
    return false;
}

size_t flex_cache_purge_expired(FlexCacheTable* table, unsigned long now_sec) {
    if (!table) return 0;
    
    size_t purged = 0;
    for (size_t i = 0; i < FLEX_CACHE_MAX_ENTRIES; ++i) {
        if (table->entries[i].is_valid && table->entries[i].ttl_sec > 0) {
            if ((now_sec - table->entries[i].created_at_sec) > table->entries[i].ttl_sec) {
                table->entries[i].is_valid = false;
                if (table->count > 0) table->count--;
                purged++;
            }
        }
    }
    return purged;
}

void flex_cache_clear(FlexCacheTable* table) {
    if (!table) return;
    for (size_t i = 0; i < FLEX_CACHE_MAX_ENTRIES; ++i) {
        table->entries[i].is_valid = false;
    }
    table->count = 0;
}

