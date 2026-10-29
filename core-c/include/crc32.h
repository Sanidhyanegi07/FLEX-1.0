#ifndef FLEX_CRC32_H
#define FLEX_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compute IEEE 802.3 32-bit Cyclic Redundancy Check (CRC32) over a buffer.
 */
uint32_t flex_crc32(const void* data, size_t length);

/**
 * Update an existing CRC32 checksum with additional data chunk.
 */
uint32_t flex_crc32_update(uint32_t crc, const void* data, size_t length);

/**
 * Format a CRC32 checksum into an 8-character hex string (e.g. "A1B2C3D4").
 */
void flex_crc32_to_hex(uint32_t crc, char* out_hex, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_CRC32_H */

