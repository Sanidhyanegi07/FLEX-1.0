#include "flex/ui/theme.hpp"

namespace flex::ui {

QString Theme::getApplicationStylesheet()
{
    return QString(R"(
/* Global Application Stylesheet — FLEX 1.0 Celestial Dark */

QMainWindow, QWidget#CentralWidget {
    background-color: %1;
    color: %2;
    font-family: "Segoe UI", system-ui, -apple-system, sans-serif;
    font-size: 13px;
}

/* Sidebar Container */
QWidget#SidebarWidget {
    background-color: %3;
    border-right: 1px solid %4;
}

/* Header & Brand */
QLabel#AppBrandTitle {
    color: %2;
    font-size: 17px;
    font-weight: 700;
    letter-spacing: 1px;
}

QLabel#AppBrandSubtitle {
    color: %5;
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 0.5px;
}

/* Buttons */
QPushButton {
    background-color: %6;
    color: %2;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 7px 14px;
    font-size: 13px;
    font-weight: 500;
}

QPushButton:hover {
    background-color: %7;
    border-color: %8;
}

QPushButton:pressed {
    background-color: #5B21B6;
}

QPushButton:disabled {
    background-color: #1A1D2B;
    color: #475569;
    border-color: #1E2235;
}

QPushButton#PrimaryButton {
    background-color: %9;
    color: #FFFFFF;
    border: 1px solid %9;
    font-weight: 600;
}

QPushButton#PrimaryButton:hover {
    background-color: %10;
    border-color: %10;
}

QPushButton#PrimaryButton:disabled {
    background-color: #2D1F4D;
    color: #64748B;
    border-color: #2D1F4D;
}

/* New Chat Button */
QPushButton#NewChatButton {
    background-color: #1E1A33;
    color: %11;
    border: 1px solid #7C3AED;
    border-radius: 6px;
    padding: 8px 14px;
    font-weight: 600;
    text-align: center;
}

QPushButton#NewChatButton:hover {
    background-color: #2A234A;
    border-color: #A78BFA;
    color: #FFFFFF;
}

/* Line Edits & Search */
QLineEdit {
    background-color: %12;
    color: %2;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}

QLineEdit:focus {
    border-color: %8;
}

/* Plain Text & Text Edit */
QPlainTextEdit, QTextEdit {
    background-color: %12;
    color: %2;
    border: 1px solid %4;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
    selection-background-color: #7C3AED;
    selection-color: #FFFFFF;
}

QPlainTextEdit:focus, QTextEdit:focus {
    border-color: %8;
}

/* Conversation List */
QListWidget {
    background-color: transparent;
    border: none;
    outline: none;
    padding: 4px;
}

QListWidget::item {
    background-color: transparent;
    color: %5;
    border-radius: 6px;
    padding: 8px 10px;
    margin-bottom: 3px;
}

QListWidget::item:hover {
    background-color: %13;
    color: %2;
}

QListWidget::item:selected {
    background-color: %14;
    color: #FFFFFF;
    border-left: 3px solid %9;
}

/* Scroll Areas */
QScrollArea {
    background-color: transparent;
    border: none;
}

QScrollBar:vertical {
    background-color: transparent;
    width: 7px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background-color: #1E2338;
    border-radius: 3px;
    min-height: 20px;
}

QScrollBar::handle:vertical:hover {
    background-color: #333C5E;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}

QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: none;
}

/* Telemetry & Status Badges */
QFrame#TelemetryCard {
    background-color: %15;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 6px;
}

QLabel#TelemetryLabel {
    color: %5;
    font-size: 11px;
}

/* Splitter */
QSplitter::handle {
    background-color: %4;
}

QSplitter::handle:horizontal {
    width: 1px;
}
)")
    .arg(BgRoot)             // %1
    .arg(TextPrimary)        // %2
    .arg(BgSidebar)         // %3
    .arg(Border)            // %4
    .arg(TextMuted)         // %5
    .arg(BgCard)            // %6
    .arg(BgSidebarHover)    // %7
    .arg(BorderFocus)       // %8
    .arg(AccentPurple)      // %9
    .arg(AccentPurpleHover) // %10
    .arg(TextAccentViolet)  // %11
    .arg(BgInput)           // %12
    .arg(BgSidebarHover)    // %13
    .arg(BgSidebarActive)   // %14
    .arg(BgPanel);          // %15
}

} // namespace flex::ui

