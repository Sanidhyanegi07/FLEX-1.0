#include "bloom_filter.h"
#include <string.h>

/**
 * Single-pass triple hash function.
 * Computes FNV-1a, DJB2, and SDBM hashes simultaneously across the string
 * in a single cache-friendly pass.
 */
static void bloom_triple_hash(const char* str, uint32_t* out_h1, uint32_t* out_h2, uint32_t* out_h3) {
    uint32_t h1 = 2166136261u;
    uint32_t h2 = 5381u;
    uint32_t h3 = 0u;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)str[i];
        h1 = (h1 ^ c) * 16777619u;
        h2 = ((h2 << 5) + h2) + c;
        h3 = c + (h3 << 6) + (h3 << 16) - h3;
    }

    *out_h1 = h1 % FLEX_BLOOM_BITS;
    *out_h2 = h2 % FLEX_BLOOM_BITS;
    *out_h3 = h3 % FLEX_BLOOM_BITS;
}

void flex_bloom_init(FlexBloomFilter* filter) {
    if (!filter) return;
    memset(filter->bits, 0, sizeof(filter->bits));
    filter->items_added = 0;
}

void flex_bloom_add(FlexBloomFilter* filter, const char* str) {
    if (!filter || !str || str[0] == '\0') return;
    
    uint32_t h1 = 0, h2 = 0, h3 = 0;
    bloom_triple_hash(str, &h1, &h2, &h3);
    
    filter->bits[h1 / 8] |= (uint8_t)(1 << (h1 % 8));
    filter->bits[h2 / 8] |= (uint8_t)(1 << (h2 % 8));
    filter->bits[h3 / 8] |= (uint8_t)(1 << (h3 % 8));
    
    filter->items_added++;
}

bool flex_bloom_contains(const FlexBloomFilter* filter, const char* str) {
    if (!filter || !str || str[0] == '\0') return false;
    
    uint32_t h1 = 0, h2 = 0, h3 = 0;
    bloom_triple_hash(str, &h1, &h2, &h3);
    
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
    const uint64_t* words = (const uint64_t*)filter->bits;
    const size_t word_count = sizeof(filter->bits) / sizeof(uint64_t);

    for (size_t i = 0; i < word_count; ++i) {
#if defined(__GNUC__) || defined(__clang__)
        count += (size_t)__builtin_popcountll(words[i]);
#else
        uint64_t w = words[i];
        while (w > 0) {
            count += (size_t)(w & 1ULL);
            w >>= 1;
        }
#endif
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
    uint64_t* dw = (uint64_t*)dest->bits;
    const uint64_t* sw = (const uint64_t*)src->bits;
    const size_t word_count = sizeof(dest->bits) / sizeof(uint64_t);

    for (size_t i = 0; i < word_count; ++i) {
        dw[i] |= sw[i];
    }
    dest->items_added += src->items_added;
    return true;
}

bool flex_bloom_intersect(FlexBloomFilter* dest, const FlexBloomFilter* src) {
    if (!dest || !src) return false;
    uint64_t* dw = (uint64_t*)dest->bits;
    const uint64_t* sw = (const uint64_t*)src->bits;
    const size_t word_count = sizeof(dest->bits) / sizeof(uint64_t);

    for (size_t i = 0; i < word_count; ++i) {
        dw[i] &= sw[i];
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

