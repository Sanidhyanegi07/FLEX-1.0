#include "sysinfo_collector.h"
#include <string.h>
#include <stdio.h>

void flex_sysinfo_collect(FlexSystemInfo* info, unsigned long uptime_sec) {
    if (!info) return;
    memset(info, 0, sizeof(FlexSystemInfo));
    
#if defined(_WIN32) || defined(_WIN64)
    strncpy(info->os_name, "Windows", sizeof(info->os_name) - 1);
#elif defined(__APPLE__)
    strncpy(info->os_name, "macOS", sizeof(info->os_name) - 1);
#elif defined(__linux__)
    strncpy(info->os_name, "Linux", sizeof(info->os_name) - 1);
#else
    strncpy(info->os_name, "Generic OS", sizeof(info->os_name) - 1);
#endif

#if defined(__x86_64__) || defined(_M_X64)
    strncpy(info->arch, "x86_64", sizeof(info->arch) - 1);
#elif defined(__aarch64__) || defined(_M_ARM64)
    strncpy(info->arch, "arm64", sizeof(info->arch) - 1);
#elif defined(__i386__) || defined(_M_IX86)
    strncpy(info->arch, "x86_32", sizeof(info->arch) - 1);
#else
    strncpy(info->arch, "unknown", sizeof(info->arch) - 1);
#endif

#if defined(__MINGW64__)
    strncpy(info->compiler, "MinGW-w64 GCC", sizeof(info->compiler) - 1);
#elif defined(__GNUC__)
    strncpy(info->compiler, "GCC", sizeof(info->compiler) - 1);
#elif defined(_MSC_VER)
    strncpy(info->compiler, "MSVC", sizeof(info->compiler) - 1);
#elif defined(__clang__)
    strncpy(info->compiler, "Clang", sizeof(info->compiler) - 1);
#else
    strncpy(info->compiler, "Standard C", sizeof(info->compiler) - 1);
#endif

    info->pointer_bits = sizeof(void*) * 8;
    info->uptime_seconds = uptime_sec;
    info->total_ram_mb = 16384; /* Default standard baseline */
    info->free_ram_mb = 8192;
}

size_t flex_sysinfo_format_uptime(unsigned long uptime_sec, char* out_buf, size_t out_capacity) {
    if (!out_buf || out_capacity == 0) return 0;
    
    unsigned long days = uptime_sec / 86400;
    unsigned long hours = (uptime_sec % 86400) / 3600;
    unsigned long mins = (uptime_sec % 3600) / 60;
    unsigned long secs = uptime_sec % 60;
    
    int len = 0;
    if (days > 0) {
        len = snprintf(out_buf, out_capacity, "%lud %02luh %02lum %02lus", days, hours, mins, secs);
    } else if (hours > 0) {
        len = snprintf(out_buf, out_capacity, "%02luh %02lum %02lus", hours, mins, secs);
    } else {
        len = snprintf(out_buf, out_capacity, "%02lum %02lus", mins, secs);
    }
    
    return len > 0 ? (size_t)len : 0;
}

size_t flex_sysinfo_format_memory(size_t mb, char* out_buf, size_t out_capacity) {
    if (!out_buf || out_capacity == 0) return 0;
    
    int len = 0;
    if (mb >= 1024) {
        double gb = (double)mb / 1024.0;
        len = snprintf(out_buf, out_capacity, "%.1f GB", gb);
    } else {
        len = snprintf(out_buf, out_capacity, "%zu MB", mb);
    }
    return len > 0 ? (size_t)len : 0;
}

size_t flex_sysinfo_format_status(const FlexSystemInfo* info,
                                  const char* app_version,
                                  char* out_buf,
                                  size_t out_capacity) {
    if (!info || !out_buf || out_capacity == 0) return 0;
    
    char uptime_str[32];
    flex_sysinfo_format_uptime(info->uptime_seconds, uptime_str, sizeof(uptime_str));
    
    int len = snprintf(out_buf, out_capacity,
                       "FLEX 1.0 (v%s) System Status:\n"
                       "  OS: %s (%s, %zu-bit)\n"
                       "  Compiler: %s\n"
                       "  Uptime: %s\n"
                       "  Architecture: C11 Core authoritative + Native C++17 Qt 6",
                       app_version ? app_version : "1.0",
                       info->os_name,
                       info->arch,
                       info->pointer_bits,
                       info->compiler,
                       uptime_str);
    
    return len > 0 ? (size_t)len : 0;
}

