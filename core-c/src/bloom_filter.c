#include "bloom_filter.h"
#include <string.h>

static uint32_t hash_fnv1a(const char* str) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        h ^= (unsigned char)str[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t hash_djb2(const char* str) {
    uint32_t h = 5381u;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        h = ((h << 5) + h) + (unsigned char)str[i];
    }
    return h;
}

static uint32_t hash_sdbm(const char* str) {
    uint32_t h = 0;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        h = (unsigned char)str[i] + (h << 6) + (h << 16) - h;
    }
    return h;
}

void flex_bloom_init(FlexBloomFilter* filter) {
    if (!filter) return;
    memset(filter->bits, 0, sizeof(filter->bits));
    filter->items_added = 0;
}

void flex_bloom_add(FlexBloomFilter* filter, const char* str) {
    if (!filter || !str || str[0] == '\0') return;
    
    uint32_t h1 = hash_fnv1a(str) % FLEX_BLOOM_BITS;
    uint32_t h2 = hash_djb2(str)  % FLEX_BLOOM_BITS;
    uint32_t h3 = hash_sdbm(str)  % FLEX_BLOOM_BITS;
    
    filter->bits[h1 / 8] |= (uint8_t)(1 << (h1 % 8));
    filter->bits[h2 / 8] |= (uint8_t)(1 << (h2 % 8));
    filter->bits[h3 / 8] |= (uint8_t)(1 << (h3 % 8));
    
    filter->items_added++;
}

bool flex_bloom_contains(const FlexBloomFilter* filter, const char* str) {
    if (!filter || !str || str[0] == '\0') return false;
    
    uint32_t h1 = hash_fnv1a(str) % FLEX_BLOOM_BITS;
    uint32_t h2 = hash_djb2(str)  % FLEX_BLOOM_BITS;
    uint32_t h3 = hash_sdbm(str)  % FLEX_BLOOM_BITS;
    
    if (!(filter->bits[h1 / 8] & (1 << (h1 % 8)))) return false;
    if (!(filter->bits[h2 / 8] & (1 << (h2 % 8)))) return false;
    if (!(filter->bits[h3 / 8] & (1 << (h3 % 8)))) return false;
    
    return true;
}

void flex_bloom_clear(FlexBloomFilter* filter) {
    if (!filter) return;
    memset(filter->bits, 0, sizeof(filter->bits));
    filter->items_added = 0;
}

size_t flex_bloom_count_bits(const FlexBloomFilter* filter) {
    if (!filter) return 0;
    size_t count = 0;
    for (size_t i = 0; i < sizeof(filter->bits); ++i) {
        uint8_t byte = filter->bits[i];
        while (byte > 0) {
            count += (byte & 1u);
            byte >>= 1;
        }
    }
    return count;
}

float flex_bloom_density(const FlexBloomFilter* filter) {
    if (!filter) return 0.0f;
    size_t set_bits = flex_bloom_count_bits(filter);
    return (float)set_bits / (float)FLEX_BLOOM_BITS;
}

bool flex_bloom_union(FlexBloomFilter* dest, const FlexBloomFilter* src) {
    if (!dest || !src) return false;
    for (size_t i = 0; i < sizeof(dest->bits); ++i) {
        dest->bits[i] |= src->bits[i];
    }
    dest->items_added += src->items_added;
    return true;
}

bool flex_bloom_intersect(FlexBloomFilter* dest, const FlexBloomFilter* src) {
    if (!dest || !src) return false;
    for (size_t i = 0; i < sizeof(dest->bits); ++i) {
        dest->bits[i] &= src->bits[i];
    }
    if (dest->items_added > src->items_added) {
        dest->items_added = src->items_added;
    }
    return true;
}

float flex_bloom_estimate_fp(const FlexBloomFilter* filter) {
    if (!filter) return 0.0f;
    float p = flex_bloom_density(filter);
    /* 3 hash functions: p^3 */
    return p * p * p;
}

