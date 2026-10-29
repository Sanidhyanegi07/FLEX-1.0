#include "theme_engine.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

void flex_theme_palette_init_default(flex_theme_palette_t* pal) {
    if (!pal) return;
    pal->bg_deep = 0x0A0D14;
    pal->bg_surface = 0x121620;
    pal->bg_elevated = 0x1A1F2C;
    pal->border_color = 0x2A3245;
    pal->accent_cyan = 0x00D2FF;
    pal->accent_blue = 0x3B82F6;
    pal->text_primary = 0xF3F4F6;
    pal->text_secondary = 0x9CA3AF;
    pal->status_success = 0x10B981;
    pal->status_warning = 0xF59E0B;
    pal->status_error = 0xEF4444;
}

uint32_t flex_color_blend(uint32_t bg_rgb, uint32_t fg_rgb, float alpha) {
    if (alpha <= 0.0f) return bg_rgb;
    if (alpha >= 1.0f) return fg_rgb;

    float r_bg = (float)((bg_rgb >> 16) & 0xFF);
    float g_bg = (float)((bg_rgb >> 8) & 0xFF);
    float b_bg = (float)(bg_rgb & 0xFF);

    float r_fg = (float)((fg_rgb >> 16) & 0xFF);
    float g_fg = (float)((fg_rgb >> 8) & 0xFF);
    float b_fg = (float)(fg_rgb & 0xFF);

    uint32_t r = (uint32_t)(r_bg + (r_fg - r_bg) * alpha);
    uint32_t g = (uint32_t)(g_bg + (g_fg - g_bg) * alpha);
    uint32_t b = (uint32_t)(b_bg + (b_fg - b_bg) * alpha);

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    return (r << 16) | (g << 8) | b;
}

float flex_color_luminance(uint32_t rgb) {
    float r = (float)((rgb >> 16) & 0xFF) / 255.0f;
    float g = (float)((rgb >> 8) & 0xFF) / 255.0f;
    float b = (float)(rgb & 0xFF) / 255.0f;

    r = (r <= 0.03928f) ? (r / 12.92f) : powf((r + 0.055f) / 1.055f, 2.4f);
    g = (g <= 0.03928f) ? (g / 12.92f) : powf((g + 0.055f) / 1.055f, 2.4f);
    b = (b <= 0.03928f) ? (b / 12.92f) : powf((b + 0.055f) / 1.055f, 2.4f);

    return (0.2126f * r) + (0.7152f * g) + (0.0722f * b);
}

float flex_color_contrast_ratio(uint32_t rgb1, uint32_t rgb2) {
    float l1 = flex_color_luminance(rgb1);
    float l2 = flex_color_luminance(rgb2);

    float lighter = (l1 > l2) ? l1 : l2;
    float darker = (l1 > l2) ? l2 : l1;

    return (lighter + 0.05f) / (darker + 0.05f);
}

void flex_color_to_hex(uint32_t rgb, char* out_hex, size_t out_cap) {
    if (!out_hex || out_cap < 8) return;
    snprintf(out_hex, out_cap, "#%06X", rgb & 0xFFFFFF);
}

size_t flex_theme_generate_stylesheet(
    const flex_theme_palette_t* pal,
    char* out_css,
    size_t css_cap
) {
    if (!out_css || css_cap < 512) return 0;

    flex_theme_palette_t default_pal;
    if (!pal) {
        flex_theme_palette_init_default(&default_pal);
        pal = &default_pal;
    }

    char hex_deep[8], hex_surface[8], hex_elevated[8], hex_border[8];
    char hex_cyan[8], hex_blue[8], hex_text_pri[8], hex_text_sec[8];
    char hex_success[8], hex_warning[8], hex_error[8];

    flex_color_to_hex(pal->bg_deep, hex_deep, sizeof(hex_deep));
    flex_color_to_hex(pal->bg_surface, hex_surface, sizeof(hex_surface));
    flex_color_to_hex(pal->bg_elevated, hex_elevated, sizeof(hex_elevated));
    flex_color_to_hex(pal->border_color, hex_border, sizeof(hex_border));
    flex_color_to_hex(pal->accent_cyan, hex_cyan, sizeof(hex_cyan));
    flex_color_to_hex(pal->accent_blue, hex_blue, sizeof(hex_blue));
    flex_color_to_hex(pal->text_primary, hex_text_pri, sizeof(hex_text_pri));
    flex_color_to_hex(pal->text_secondary, hex_text_sec, sizeof(hex_text_sec));
    flex_color_to_hex(pal->status_success, hex_success, sizeof(hex_success));
    flex_color_to_hex(pal->status_warning, hex_warning, sizeof(hex_warning));
    flex_color_to_hex(pal->status_error, hex_error, sizeof(hex_error));

    int written = snprintf(
        out_css, css_cap,
        "QMainWindow { background-color: %s; color: %s; font-family: 'Segoe UI', system-ui, sans-serif; }\n"
        "QWidget { background-color: transparent; color: %s; }\n"
        "QWidget#sidebar, QWidget#navPanel { background-color: %s; border-right: 1px solid %s; }\n"
        "QWidget#headerPanel { background-color: %s; border-bottom: 1px solid %s; }\n"
        "QWidget#rightSidebar { background-color: %s; border-left: 1px solid %s; }\n"
        "QScrollArea { border: none; background: transparent; }\n"
        "QScrollBar:vertical { width: 8px; background: %s; margin: 0; border-radius: 4px; }\n"
        "QScrollBar::handle:vertical { background: %s; min-height: 24px; border-radius: 4px; }\n"
        "QScrollBar::handle:vertical:hover { background: %s; }\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; background: none; }\n"
        "QLineEdit, QTextEdit, QPlainTextEdit { background-color: %s; color: %s; border: 1px solid %s; border-radius: 8px; padding: 8px; selection-background-color: %s; selection-color: #FFFFFF; }\n"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border: 1px solid %s; }\n"
        "QPushButton { background-color: %s; color: %s; border: 1px solid %s; border-radius: 6px; padding: 8px 16px; font-weight: 600; }\n"
        "QPushButton:hover { background-color: %s; border-color: %s; color: #FFFFFF; }\n"
        "QPushButton:pressed { background-color: %s; }\n"
        "QPushButton:disabled { background-color: %s; color: %s; border-color: %s; }\n"
        "QPushButton#sendButton, QPushButton#primaryButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %s, stop:1 %s); color: #000000; border: none; }\n"
        "QPushButton#sendButton:hover, QPushButton#primaryButton:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #1CE0FF, stop:1 #5B95F8); }\n"
        "QListView, QTreeView { background-color: %s; border: 1px solid %s; border-radius: 6px; color: %s; }\n"
        "QListView::item:hover, QTreeView::item:hover { background-color: %s; }\n"
        "QListView::item:selected, QTreeView::item:selected { background-color: %s; color: #FFFFFF; }\n"
        "QSplitter::handle { background-color: %s; width: 1px; height: 1px; }\n"
        "QLabel#badgeSuccess { background-color: %s; color: #000000; border-radius: 4px; padding: 2px 6px; font-size: 11px; font-weight: bold; }\n"
        "QLabel#badgeWarning { background-color: %s; color: #000000; border-radius: 4px; padding: 2px 6px; font-size: 11px; font-weight: bold; }\n"
        "QLabel#badgeError { background-color: %s; color: #FFFFFF; border-radius: 4px; padding: 2px 6px; font-size: 11px; font-weight: bold; }\n",
        hex_deep, hex_text_pri,
        hex_text_pri,
        hex_surface, hex_border,
        hex_surface, hex_border,
        hex_surface, hex_border,
        hex_deep,
        hex_border,
        hex_text_sec,
        hex_elevated, hex_text_pri, hex_border, hex_blue,
        hex_cyan,
        hex_elevated, hex_text_pri, hex_border,
        hex_border, hex_cyan,
        hex_deep,
        hex_deep, hex_border, hex_deep,
        hex_cyan, hex_blue,
        hex_surface, hex_border, hex_text_pri,
        hex_elevated,
        hex_border,
        hex_border,
        hex_success,
        hex_warning,
        hex_error
    );

    if (written < 0 || (size_t)written >= css_cap) {
        out_css[css_cap - 1] = '\0';
        return css_cap - 1;
    }
    return (size_t)written;
}

size_t flex_theme_generate_desktop_stylesheet(char* out_css, size_t css_cap) {
    if (!out_css || css_cap < 4096) return 0;

    static const char* const S_PART1 =
        "/* Global Application Stylesheet — FLEX 1.0 Celestial Dark */\n"
        "QMainWindow, QWidget#CentralWidget {\n"
        "    background-color: #0A0B10;\n"
        "    color: #F8FAFC;\n"
        "    font-family: \"Segoe UI\", system-ui, -apple-system, sans-serif;\n"
        "    font-size: 13px;\n"
        "}\n\n"
        "/* Top Telemetry Header Bar */\n"
        "QFrame#TopHeaderFrame {\n"
        "    background-color: #10121C;\n"
        "    border-bottom: 1px solid #1E2235;\n"
        "    padding: 0px;\n"
        "}\n"
        "QLabel#TopBrandTitle, QLabel#AppBrandTitle {\n"
        "    color: #38BDF8;\n"
        "    font-size: 16px;\n"
        "    font-weight: 700;\n"
        "    letter-spacing: 0.8px;\n"
        "}\n"
        "QLabel#TopBrandSubtitle, QLabel#AppBrandSubtitle {\n"
        "    color: #94A3B8;\n"
        "    font-size: 9px;\n"
        "    font-weight: 700;\n"
        "    letter-spacing: 0.5px;\n"
        "}\n"
        "QLabel#StatusBadge, QFrame#TelemetryCard {\n"
        "    background-color: #141726;\n"
        "    color: #94A3B8;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 4px;\n"
        "    padding: 4px 10px;\n"
        "    font-family: \"Consolas\", monospace;\n"
        "    font-size: 10px;\n"
        "    font-weight: 700;\n"
        "}\n"
        "QPushButton#SidebarToggleBtn {\n"
        "    background-color: #141726;\n"
        "    color: #94A3B8;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 4px;\n"
        "    padding: 4px 8px;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "}\n"
        "QPushButton#SidebarToggleBtn:hover {\n"
        "    background-color: #171826;\n"
        "    color: #F8FAFC;\n"
        "    border-color: #7C3AED;\n"
        "}\n\n"
        "/* Left Navigation Sidebar */\n"
        "QWidget#SidebarWidget, QWidget#LeftSidebarWidget {\n"
        "    background-color: #0F111A;\n"
        "    border-right: 1px solid #1E2235;\n"
        "}\n"
        "QLabel#BrandLogoBox {\n"
        "    background-color: #7C3AED;\n"
        "    color: #FFFFFF;\n"
        "    font-size: 11px;\n"
        "    font-weight: 800;\n"
        "    border-radius: 4px;\n"
        "    padding: 2px 6px;\n"
        "}\n"
        "QPushButton#NavButton {\n"
        "    background-color: transparent;\n"
        "    color: #94A3B8;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 8px 14px;\n"
        "    font-size: 12px;\n"
        "    font-weight: 500;\n"
        "    text-align: left;\n"
        "}\n"
        "QPushButton#NavButton:hover {\n"
        "    background-color: #171826;\n"
        "    color: #F8FAFC;\n"
        "}\n"
        "QPushButton#NavButton[active=\"true\"] {\n"
        "    background-color: #1E1A33;\n"
        "    color: #F8FAFC;\n"
        "    font-weight: 600;\n"
        "    border-left: 3px solid #7C3AED;\n"
        "}\n"
        "QLabel#RecentChatsHeader {\n"
        "    color: #64748B;\n"
        "    font-size: 9px;\n"
        "    font-weight: 700;\n"
        "    letter-spacing: 0.5px;\n"
        "    padding: 6px 14px 2px 14px;\n"
        "}\n"
        "QFrame#LocalProfileCard {\n"
        "    background-color: #141726;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 6px;\n"
        "    padding: 8px 10px;\n"
        "}\n\n"
        "/* Right Information Sidebar */\n"
        "QWidget#RightSidebarWidget {\n"
        "    background-color: #0F111A;\n"
        "    border-left: 1px solid #1E2235;\n"
        "}\n"
        "QFrame#UserProfileCard {\n"
        "    background-color: #141726;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 8px;\n"
        "    padding: 12px;\n"
        "}\n"
        "QLabel#SectionHeaderLabel {\n"
        "    color: #64748B;\n"
        "    font-size: 9px;\n"
        "    font-weight: 700;\n"
        "    letter-spacing: 0.6px;\n"
        "}\n"
        "QLabel#StatusPill {\n"
        "    background-color: #141726;\n"
        "    color: #94A3B8;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 4px;\n"
        "    padding: 5px 8px;\n"
        "    font-family: \"Consolas\", monospace;\n"
        "    font-size: 9px;\n"
        "}\n"
        "QPushButton#QuickActionBtn {\n"
        "    background-color: #0F111A;\n"
        "    color: #94A3B8;\n"
        "    border: none;\n"
        "    border-radius: 4px;\n"
        "    padding: 6px 10px;\n"
        "    font-size: 11px;\n"
        "    text-align: left;\n"
        "}\n"
        "QPushButton#QuickActionBtn:hover {\n"
        "    background-color: #1E1A33;\n"
        "    color: #F8FAFC;\n"
        "}\n"
        "QLabel#TaglineLabel {\n"
        "    color: #475569;\n"
        "    font-size: 10px;\n"
        "    font-style: italic;\n"
        "}\n";

    static const char* const S_PART2 =
        "/* Buttons */\n"
        "QPushButton {\n"
        "    background-color: #141726;\n"
        "    color: #F8FAFC;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 6px;\n"
        "    padding: 6px 12px;\n"
        "    font-size: 12px;\n"
        "    font-weight: 500;\n"
        "}\n"
        "QPushButton:hover {\n"
        "    background-color: #171826;\n"
        "    border-color: #2E3650;\n"
        "}\n"
        "QPushButton:pressed {\n"
        "    background-color: #5B21B6;\n"
        "}\n"
        "QPushButton:disabled {\n"
        "    background-color: #1A1D2B;\n"
        "    color: #475569;\n"
        "    border-color: #1E2235;\n"
        "}\n"
        "QPushButton#PrimaryButton {\n"
        "    background-color: #7C3AED;\n"
        "    color: #FFFFFF;\n"
        "    border: 1px solid #7C3AED;\n"
        "    font-weight: 600;\n"
        "}\n"
        "QPushButton#PrimaryButton:hover {\n"
        "    background-color: #6D28D9;\n"
        "    border-color: #6D28D9;\n"
        "}\n"
        "QPushButton#PrimaryButton:disabled {\n"
        "    background-color: #2D1F4D;\n"
        "    color: #64748B;\n"
        "    border-color: #2D1F4D;\n"
        "}\n"
        "QPushButton#NewChatButton {\n"
        "    background-color: #1E1A33;\n"
        "    color: #A78BFA;\n"
        "    border: 1px solid #7C3AED;\n"
        "    border-radius: 6px;\n"
        "    padding: 7px 12px;\n"
        "    font-size: 12px;\n"
        "    font-weight: 600;\n"
        "    text-align: center;\n"
        "}\n"
        "QPushButton#NewChatButton:hover {\n"
        "    background-color: #2A234A;\n"
        "    border-color: #A78BFA;\n"
        "    color: #FFFFFF;\n"
        "}\n\n"
        "/* Hero Prompt Suggestion Chips */\n"
        "QPushButton#PromptSuggestionChip {\n"
        "    background-color: #141726;\n"
        "    color: #94A3B8;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 8px;\n"
        "    padding: 10px 14px;\n"
        "    font-size: 12px;\n"
        "    text-align: left;\n"
        "}\n"
        "QPushButton#PromptSuggestionChip:hover {\n"
        "    background-color: #1E1A33;\n"
        "    border-color: #7C3AED;\n"
        "    color: #F8FAFC;\n"
        "}\n\n"
        "/* Line Edits & Search */\n"
        "QLineEdit {\n"
        "    background-color: #131522;\n"
        "    color: #F8FAFC;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 6px;\n"
        "    padding: 6px 10px;\n"
        "    font-size: 12px;\n"
        "}\n"
        "QLineEdit:focus {\n"
        "    border-color: #7C3AED;\n"
        "}\n"
        "QPlainTextEdit, QTextEdit {\n"
        "    background-color: #131522;\n"
        "    color: #F8FAFC;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 8px;\n"
        "    padding: 8px 12px;\n"
        "    font-size: 13px;\n"
        "    selection-background-color: #7C3AED;\n"
        "    selection-color: #FFFFFF;\n"
        "}\n"
        "QPlainTextEdit:focus, QTextEdit:focus {\n"
        "    border-color: #7C3AED;\n"
        "}\n";

    static const char* const S_PART3 =
        "/* Conversation List */\n"
        "QListWidget {\n"
        "    background-color: transparent;\n"
        "    border: none;\n"
        "    outline: none;\n"
        "    padding: 2px;\n"
        "}\n"
        "QListWidget::item {\n"
        "    background-color: transparent;\n"
        "    color: #94A3B8;\n"
        "    border-radius: 6px;\n"
        "    padding: 7px 10px;\n"
        "    margin-bottom: 2px;\n"
        "}\n"
        "QListWidget::item:hover {\n"
        "    background-color: #171826;\n"
        "    color: #F8FAFC;\n"
        "}\n"
        "QListWidget::item:selected {\n"
        "    background-color: #1E1A33;\n"
        "    color: #FFFFFF;\n"
        "    border-left: 3px solid #7C3AED;\n"
        "}\n\n"
        "/* Tree Views & Projects Explorer */\n"
        "QTreeView {\n"
        "    background-color: #121124;\n"
        "    color: #E2E8F0;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 6px;\n"
        "    padding: 4px;\n"
        "    outline: none;\n"
        "}\n"
        "QTreeView::item {\n"
        "    padding: 4px;\n"
        "    border-radius: 4px;\n"
        "}\n"
        "QTreeView::item:hover {\n"
        "    background-color: #171826;\n"
        "}\n"
        "QTreeView::item:selected {\n"
        "    background-color: #2C1B5E;\n"
        "    color: #A78BFA;\n"
        "}\n"
        "QHeaderView::section {\n"
        "    background-color: #10121C;\n"
        "    color: #94A3B8;\n"
        "    border: none;\n"
        "    border-bottom: 1px solid #1E2235;\n"
        "    padding: 4px 8px;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "}\n\n"
        "/* Scroll Areas & Scrollbars */\n"
        "QScrollArea {\n"
        "    background-color: transparent;\n"
        "    border: none;\n"
        "}\n"
        "QScrollBar:vertical {\n"
        "    background-color: transparent;\n"
        "    width: 7px;\n"
        "    margin: 0;\n"
        "}\n"
        "QScrollBar::handle:vertical {\n"
        "    background-color: #1E2338;\n"
        "    border-radius: 3px;\n"
        "    min-height: 20px;\n"
        "}\n"
        "QScrollBar::handle:vertical:hover {\n"
        "    background-color: #333C5E;\n"
        "}\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {\n"
        "    height: 0;\n"
        "}\n"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {\n"
        "    background: none;\n"
        "}\n"
        "QScrollBar:horizontal {\n"
        "    background-color: transparent;\n"
        "    height: 7px;\n"
        "    margin: 0;\n"
        "}\n"
        "QScrollBar::handle:horizontal {\n"
        "    background-color: #1E2338;\n"
        "    border-radius: 3px;\n"
        "    min-width: 20px;\n"
        "}\n"
        "QScrollBar::handle:horizontal:hover {\n"
        "    background-color: #333C5E;\n"
        "}\n"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {\n"
        "    width: 0;\n"
        "}\n"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {\n"
        "    background: none;\n"
        "}\n\n"
        "/* Splitter & Dividers */\n"
        "QSplitter::handle {\n"
        "    background-color: #1E2235;\n"
        "}\n"
        "QSplitter::handle:horizontal {\n"
        "    width: 1px;\n"
        "}\n\n"
        "/* Tooltips */\n"
        "QToolTip {\n"
        "    background-color: #141726;\n"
        "    color: #F8FAFC;\n"
        "    border: 1px solid #1E2235;\n"
        "    border-radius: 4px;\n"
        "    padding: 4px 8px;\n"
        "    font-size: 11px;\n"
        "}\n";

    size_t len1 = strlen(S_PART1);
    size_t len2 = strlen(S_PART2);
    size_t len3 = strlen(S_PART3);
    size_t total = len1 + len2 + len3;
    if (total >= css_cap) return 0;

    memcpy(out_css, S_PART1, len1);
    memcpy(out_css + len1, S_PART2, len2);
    memcpy(out_css + len1 + len2, S_PART3, len3);
    out_css[total] = '\0';
    return total;
}

