#include "color_palette.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool flex_palette_color_from_hex(const char* hex_str, FlexColorRgba* out_color) {
    if (!hex_str || !out_color) return false;
    
    if (hex_str[0] == '#') hex_str++;
    size_t len = strlen(hex_str);
    
    if (len == 6) {
        int r1 = hex_char_val(hex_str[0]);
        int r2 = hex_char_val(hex_str[1]);
        int g1 = hex_char_val(hex_str[2]);
        int g2 = hex_char_val(hex_str[3]);
        int b1 = hex_char_val(hex_str[4]);
        int b2 = hex_char_val(hex_str[5]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
        
        out_color->r = (unsigned char)((r1 << 4) | r2);
        out_color->g = (unsigned char)((g1 << 4) | g2);
        out_color->b = (unsigned char)((b1 << 4) | b2);
        out_color->a = 255;
        return true;
    } else if (len == 8) {
        int r1 = hex_char_val(hex_str[0]);
        int r2 = hex_char_val(hex_str[1]);
        int g1 = hex_char_val(hex_str[2]);
        int g2 = hex_char_val(hex_str[3]);
        int b1 = hex_char_val(hex_str[4]);
        int b2 = hex_char_val(hex_str[5]);
        int a1 = hex_char_val(hex_str[6]);
        int a2 = hex_char_val(hex_str[7]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0 || a1 < 0 || a2 < 0) return false;
        
        out_color->r = (unsigned char)((r1 << 4) | r2);
        out_color->g = (unsigned char)((g1 << 4) | g2);
        out_color->b = (unsigned char)((b1 << 4) | b2);
        out_color->a = (unsigned char)((a1 << 4) | a2);
        return true;
    }
    return false;
}

size_t flex_palette_color_to_hex(const FlexColorRgba* color, bool include_alpha, char* out_buf, size_t out_capacity) {
    if (!color || !out_buf || out_capacity == 0) return 0;
    
    int len = 0;
    if (include_alpha) {
        len = snprintf(out_buf, out_capacity, "#%02x%02x%02x%02x", color->r, color->g, color->b, color->a);
    } else {
        len = snprintf(out_buf, out_capacity, "#%02x%02x%02x", color->r, color->g, color->b);
    }
    return len > 0 ? (size_t)len : 0;
}

FlexColorHsva flex_palette_rgba_to_hsva(const FlexColorRgba* rgba) {
    FlexColorHsva hsva = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!rgba) return hsva;
    
    float r = rgba->r / 255.0f;
    float g = rgba->g / 255.0f;
    float b = rgba->b / 255.0f;
    hsva.a = rgba->a / 255.0f;
    
    float max_c = fmaxf(r, fmaxf(g, b));
    float min_c = fminf(r, fminf(g, b));
    float delta = max_c - min_c;
    
    hsva.v = max_c;
    
    if (max_c < 1e-6f) {
        hsva.s = 0.0f;
        hsva.h = 0.0f;
        return hsva;
    }
    
    hsva.s = delta / max_c;
    
    if (delta < 1e-6f) {
        hsva.h = 0.0f;
        return hsva;
    }
    
    if (fabsf(max_c - r) < 1e-6f) {
        hsva.h = 60.0f * fmodf(((g - b) / delta), 6.0f);
    } else if (fabsf(max_c - g) < 1e-6f) {
        hsva.h = 60.0f * (((b - r) / delta) + 2.0f);
    } else {
        hsva.h = 60.0f * (((r - g) / delta) + 4.0f);
    }
    
    if (hsva.h < 0.0f) {
        hsva.h += 360.0f;
    }
    
    return hsva;
}

FlexColorRgba flex_palette_hsva_to_rgba(const FlexColorHsva* hsva) {
    FlexColorRgba rgba = {0, 0, 0, 255};
    if (!hsva) return rgba;
    
    rgba.a = (unsigned char)roundf(fmaxf(0.0f, fminf(1.0f, hsva->a)) * 255.0f);
    
    float c = hsva->v * hsva->s;
    float h_prime = fmodf(hsva->h / 60.0f, 6.0f);
    if (h_prime < 0.0f) h_prime += 6.0f;
    float x = c * (1.0f - fabsf(fmodf(h_prime, 2.0f) - 1.0f));
    float m = hsva->v - c;
    
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h_prime < 1.0f) { r = c; g = x; b = 0; }
    else if (h_prime < 2.0f) { r = x; g = c; b = 0; }
    else if (h_prime < 3.0f) { r = 0; g = c; b = x; }
    else if (h_prime < 4.0f) { r = 0; g = x; b = c; }
    else if (h_prime < 5.0f) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    
    rgba.r = (unsigned char)roundf((r + m) * 255.0f);
    rgba.g = (unsigned char)roundf((g + m) * 255.0f);
    rgba.b = (unsigned char)roundf((b + m) * 255.0f);
    return rgba;
}

FlexColorRgba flex_palette_blend(const FlexColorRgba* fg, const FlexColorRgba* bg) {
    FlexColorRgba out = {0, 0, 0, 255};
    if (!fg || !bg) return out;
    
    float alpha_fg = fg->a / 255.0f;
    float alpha_bg = bg->a / 255.0f;
    float alpha_out = alpha_fg + alpha_bg * (1.0f - alpha_fg);
    
    if (alpha_out < 1e-6f) {
        out.a = 0;
        return out;
    }
    
    float r = (fg->r * alpha_fg + bg->r * alpha_bg * (1.0f - alpha_fg)) / alpha_out;
    float g = (fg->g * alpha_fg + bg->g * alpha_bg * (1.0f - alpha_fg)) / alpha_out;
    float b = (fg->b * alpha_fg + bg->b * alpha_bg * (1.0f - alpha_fg)) / alpha_out;
    
    out.r = (unsigned char)roundf(r);
    out.g = (unsigned char)roundf(g);
    out.b = (unsigned char)roundf(b);
    out.a = (unsigned char)roundf(alpha_out * 255.0f);
    return out;
}

static float srgb_to_linear(float c) {
    if (c <= 0.03928f) {
        return c / 12.92f;
    }
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

float flex_palette_luminance(const FlexColorRgba* color) {
    if (!color) return 0.0f;
    float r = srgb_to_linear(color->r / 255.0f);
    float g = srgb_to_linear(color->g / 255.0f);
    float b = srgb_to_linear(color->b / 255.0f);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float flex_palette_contrast_ratio(const FlexColorRgba* c1, const FlexColorRgba* c2) {
    if (!c1 || !c2) return 1.0f;
    float l1 = flex_palette_luminance(c1);
    float l2 = flex_palette_luminance(c2);
    
    float lighter = fmaxf(l1, l2);
    float darker = fminf(l1, l2);
    
    return (lighter + 0.05f) / (darker + 0.05f);
}
