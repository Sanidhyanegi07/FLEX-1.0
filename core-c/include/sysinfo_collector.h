#ifndef FLEX_SYSINFO_COLLECTOR_H
#define FLEX_SYSINFO_COLLECTOR_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char os_name[32];
    char arch[16];
    char compiler[32];
    size_t pointer_bits;
    unsigned long uptime_seconds;
    size_t total_ram_mb;
    size_t free_ram_mb;
} FlexSystemInfo;

/**
 * Collect platform and build environment metadata into FlexSystemInfo.
 */
void flex_sysinfo_collect(FlexSystemInfo* info, unsigned long uptime_sec);

/**
 * Format uptime seconds into human-readable string (e.g. "1d 04h 23m 15s" or "02m 45s").
 */
size_t flex_sysinfo_format_uptime(unsigned long uptime_sec, char* out_buf, size_t out_capacity);

/**
 * Format RAM bytes/megabytes into human-readable string (e.g. "16.0 GB" or "512 MB").
 */
size_t flex_sysinfo_format_memory(size_t mb, char* out_buf, size_t out_capacity);

/**
 * Generate full diagnostic system summary string for /status command.
 */
size_t flex_sysinfo_format_status(const FlexSystemInfo* info,
                                  const char* app_version,
                                  char* out_buf,
                                  size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_SYSINFO_COLLECTOR_H */

