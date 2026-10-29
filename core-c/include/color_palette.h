#ifndef FLEX_COLOR_PALETTE_H
#define FLEX_COLOR_PALETTE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} FlexColorRgba;

typedef struct {
    float h; /* 0.0 to 360.0 degrees */
    float s; /* 0.0 to 1.0 */
    float v; /* 0.0 to 1.0 */
    float a; /* 0.0 to 1.0 */
} FlexColorHsva;

/**
 * Parse hex color string (e.g. "#131722", "#818cf8", or "#38bdf880").
 */
bool flex_palette_color_from_hex(const char* hex_str, FlexColorRgba* out_color);

/**
 * Format RGBA color to hex string (e.g. "#131722" or "#818cf8ff").
 */
size_t flex_palette_color_to_hex(const FlexColorRgba* color, bool include_alpha, char* out_buf, size_t out_capacity);

/**
 * Convert RGBA color to HSVA.
 */
FlexColorHsva flex_palette_rgba_to_hsva(const FlexColorRgba* rgba);

/**
 * Convert HSVA color to RGBA.
 */
FlexColorRgba flex_palette_hsva_to_rgba(const FlexColorHsva* hsva);

/**
 * Alpha blend foreground over background color.
 */
FlexColorRgba flex_palette_blend(const FlexColorRgba* fg, const FlexColorRgba* bg);

/**
 * Compute relative luminance according to WCAG 2.1 standard.
 */
float flex_palette_luminance(const FlexColorRgba* color);

/**
 * Calculate WCAG contrast ratio between two colors (range: 1.0 to 21.0).
 */
float flex_palette_contrast_ratio(const FlexColorRgba* c1, const FlexColorRgba* c2);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_COLOR_PALETTE_H */
