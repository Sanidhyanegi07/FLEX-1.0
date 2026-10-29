#ifndef FLEX_THEME_ENGINE_H
#define FLEX_THEME_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t bg_deep;          /* Default: 0x0A0D14 */
    uint32_t bg_surface;       /* Default: 0x121620 */
    uint32_t bg_elevated;      /* Default: 0x1A1F2C */
    uint32_t border_color;     /* Default: 0x2A3245 */
    uint32_t accent_cyan;      /* Default: 0x00D2FF */
    uint32_t accent_blue;      /* Default: 0x3B82F6 */
    uint32_t text_primary;     /* Default: 0xF3F4F6 */
    uint32_t text_secondary;   /* Default: 0x9CA3AF */
    uint32_t status_success;   /* Default: 0x10B981 */
    uint32_t status_warning;   /* Default: 0xF59E0B */
    uint32_t status_error;     /* Default: 0xEF4444 */
} flex_theme_palette_t;

/**
 * Initializes palette with default Space/Obsidian theme colors.
 */
void flex_theme_palette_init_default(flex_theme_palette_t* pal);

/**
 * Blends an RGB color with alpha into an existing background color.
 */
uint32_t flex_color_blend(uint32_t bg_rgb, uint32_t fg_rgb, float alpha);

/**
 * Calculates luminance of an RGB color (0.0 to 1.0).
 */
float flex_color_luminance(uint32_t rgb);

/**
 * Calculates WCAG contrast ratio between two colors (1.0 to 21.0).
 */
float flex_color_contrast_ratio(uint32_t rgb1, uint32_t rgb2);

/**
 * Formats an RGB uint32 into a hex color string ("#RRGGBB").
 */
void flex_color_to_hex(uint32_t rgb, char* out_hex, size_t out_cap);

/**
 * Generates the complete Qt stylesheet CSS string into out_buf.
 */
size_t flex_theme_generate_stylesheet(
    const flex_theme_palette_t* pal,
    char* out_css,
    size_t css_cap
);

/**
 * Generates the full FLEX 1.0 Celestial Dark desktop Qt stylesheet into out_css.
 * Returns bytes written.
 */
size_t flex_theme_generate_desktop_stylesheet(char* out_css, size_t css_cap);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_THEME_ENGINE_H */

