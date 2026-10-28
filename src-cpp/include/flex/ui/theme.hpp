#ifndef FLEX_UI_THEME_HPP
#define FLEX_UI_THEME_HPP

#include <QColor>
#include <QString>

namespace flex::ui {

/**
 * Space-inspired dark theme palette matching the FLEX 1.0 celestial design system.
 */
struct Theme {
    // Backgrounds
    static inline const QString BgRoot           = "#0A0B10"; // Deep space obsidian
    static inline const QString BgPanel          = "#10121C"; // Spacecraft interior deck
    static inline const QString BgSidebar        = "#0F111A"; // Slate obsidian sidebar
    static inline const QString BgSidebarActive  = "#1E1A33"; // Cosmic violet highlight
    static inline const QString BgSidebarHover   = "#171826";
    static inline const QString BgChat           = "#0C0E17"; // Event horizon backdrop
    static inline const QString BgInput          = "#131522"; // Dark prompt pill deck
    static inline const QString BgCard           = "#141726"; // System card background
    static inline const QString BgUserBubble     = "#181528"; // Subtle cosmic violet bubble
    static inline const QString BgAssistantBubble= "#121422"; // Subtle cosmic obsidian bubble

    // Borders
    static inline const QString Border           = "#1E2235"; // Subtle outline
    static inline const QString BorderFocus      = "#7C3AED"; // Neon violet glow
    static inline const QString BorderHover      = "#2E3650";

    // Text & Accents
    static inline const QString TextPrimary      = "#F8FAFC"; // Bright star white
    static inline const QString TextMuted        = "#94A3B8"; // Slate nebula gray
    static inline const QString TextAccentCyan   = "#38BDF8"; // Cosmic Cyan
    static inline const QString TextAccentViolet = "#A78BFA"; // Radiant Lavender
    static inline const QString TextUser         = "#C084FC"; // Pulsar Lavender
    static inline const QString AccentPurple     = "#7C3AED"; // Deep Nebula Violet
    static inline const QString AccentPurpleHover= "#6D28D9";

    // Status Indicators
    static inline const QString StatusSuccess    = "#34D399"; // Emerald Orbit Green
    static inline const QString StatusWarning    = "#FBBF24"; // Solar Flare Amber
    static inline const QString StatusError      = "#F87171"; // Red Alert Flare
    static inline const QString StatusOffline    = "#64748B"; // Quenched Slate

    // QColor equivalents
    static inline QColor colorBgRoot()           { return QColor(BgRoot); }
    static inline QColor colorBgPanel()          { return QColor(BgPanel); }
    static inline QColor colorBgChat()           { return QColor(BgChat); }
    static inline QColor colorAccentCyan()       { return QColor(TextAccentCyan); }
    static inline QColor colorAccentViolet()     { return QColor(TextAccentViolet); }
    static inline QColor colorAccentPurple()     { return QColor(AccentPurple); }
    static inline QColor colorStatusSuccess()    { return QColor(StatusSuccess); }
    static inline QColor colorStatusError()      { return QColor(StatusError); }

    /**
     * Comprehensive application-wide Qt stylesheet.
     */
    static QString getApplicationStylesheet();
};

} // namespace flex::ui

#endif // FLEX_UI_THEME_HPP

