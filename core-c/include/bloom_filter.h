#ifndef FLEX_BLOOM_FILTER_H
#define FLEX_BLOOM_FILTER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_BLOOM_BITS 512
#define FLEX_BLOOM_BYTES (FLEX_BLOOM_BITS / 8)

typedef struct {
    uint8_t bits[FLEX_BLOOM_BYTES];
    size_t items_added;
} FlexBloomFilter;

/**
 * Initialize a Bloom filter with all bits cleared.
 */
void flex_bloom_init(FlexBloomFilter* filter);

/**
 * Add a term/token to the Bloom filter.
 */
void flex_bloom_add(FlexBloomFilter* filter, const char* str);

/**
 * Check if a term might be in the set.
 * Returns true if possibly present, false if definitely not present.
 */
bool flex_bloom_contains(const FlexBloomFilter* filter, const char* str);

/**
 * Clear all bits in the Bloom filter.
 */
void flex_bloom_clear(FlexBloomFilter* filter);

/**
 * Count the number of bits set to 1 in the filter.
 */
size_t flex_bloom_count_bits(const FlexBloomFilter* filter);

/**
 * Calculate the bit saturation density [0.0, 1.0].
 */
float flex_bloom_density(const FlexBloomFilter* filter);

/**
 * Union two filters: dest = dest OR src.
 */
bool flex_bloom_union(FlexBloomFilter* dest, const FlexBloomFilter* src);

/**
 * Intersect two filters: dest = dest AND src.
 */
bool flex_bloom_intersect(FlexBloomFilter* dest, const FlexBloomFilter* src);

/**
 * Estimate the current false-positive probability based on bit density.
 */
float flex_bloom_estimate_fp(const FlexBloomFilter* filter);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_BLOOM_FILTER_H */
