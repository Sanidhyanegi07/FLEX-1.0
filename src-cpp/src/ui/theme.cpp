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

/* ========================================================================= */
/* Top Telemetry Header Bar                                                  */
/* ========================================================================= */

QFrame#TopHeaderFrame {
    background-color: %3;
    border-bottom: 1px solid %4;
    padding: 0px;
}

QLabel#TopBrandTitle, QLabel#AppBrandTitle {
    color: %5;
    font-size: 16px;
    font-weight: 700;
    letter-spacing: 0.8px;
}

QLabel#TopBrandSubtitle, QLabel#AppBrandSubtitle {
    color: %6;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.5px;
}

QLabel#StatusBadge, QFrame#TelemetryCard {
    background-color: %7;
    color: %6;
    border: 1px solid %4;
    border-radius: 4px;
    padding: 4px 10px;
    font-family: "Consolas", monospace;
    font-size: 10px;
    font-weight: 700;
}

QPushButton#SidebarToggleBtn {
    background-color: %7;
    color: %6;
    border: 1px solid %4;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 11px;
    font-weight: 600;
}

QPushButton#SidebarToggleBtn:hover {
    background-color: %8;
    color: %2;
    border-color: %9;
}

/* ========================================================================= */
/* Left Navigation Sidebar                                                   */
/* ========================================================================= */

QWidget#SidebarWidget, QWidget#LeftSidebarWidget {
    background-color: %10;
    border-right: 1px solid %4;
}

QLabel#BrandLogoBox {
    background-color: %11;
    color: #FFFFFF;
    font-size: 11px;
    font-weight: 800;
    border-radius: 4px;
    padding: 2px 6px;
}

QPushButton#NavButton {
    background-color: transparent;
    color: %6;
    border: none;
    border-radius: 6px;
    padding: 8px 14px;
    font-size: 12px;
    font-weight: 500;
    text-align: left;
}

QPushButton#NavButton:hover {
    background-color: %12;
    color: %2;
}

QPushButton#NavButton[active="true"] {
    background-color: %13;
    color: %2;
    font-weight: 600;
    border-left: 3px solid %11;
}

QLabel#RecentChatsHeader {
    color: #64748B;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.5px;
    padding: 6px 14px 2px 14px;
}

QFrame#LocalProfileCard {
    background-color: %7;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 8px 10px;
}

/* ========================================================================= */
/* Right Information Sidebar                                                 */
/* ========================================================================= */

QWidget#RightSidebarWidget {
    background-color: %10;
    border-left: 1px solid %4;
}

QFrame#UserProfileCard {
    background-color: %7;
    border: 1px solid %4;
    border-radius: 8px;
    padding: 12px;
}

QLabel#SectionHeaderLabel {
    color: #64748B;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.6px;
}

QLabel#StatusPill {
    background-color: %7;
    color: %6;
    border: 1px solid %4;
    border-radius: 4px;
    padding: 5px 8px;
    font-family: "Consolas", monospace;
    font-size: 9px;
}

QPushButton#QuickActionBtn {
    background-color: %10;
    color: %6;
    border: none;
    border-radius: 4px;
    padding: 6px 10px;
    font-size: 11px;
    text-align: left;
}

QPushButton#QuickActionBtn:hover {
    background-color: %13;
    color: %2;
}

QLabel#TaglineLabel {
    color: #475569;
    font-size: 10px;
    font-style: italic;
}

/* ========================================================================= */
/* Buttons                                                                   */
/* ========================================================================= */

QPushButton {
    background-color: %7;
    color: %2;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 6px 12px;
    font-size: 12px;
    font-weight: 500;
}

QPushButton:hover {
    background-color: %12;
    border-color: %14;
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
    background-color: %11;
    color: #FFFFFF;
    border: 1px solid %11;
    font-weight: 600;
}

QPushButton#PrimaryButton:hover {
    background-color: %15;
    border-color: %15;
}

QPushButton#PrimaryButton:disabled {
    background-color: #2D1F4D;
    color: #64748B;
    border-color: #2D1F4D;
}

QPushButton#NewChatButton {
    background-color: %13;
    color: %16;
    border: 1px solid %11;
    border-radius: 6px;
    padding: 7px 12px;
    font-size: 12px;
    font-weight: 600;
    text-align: center;
}

QPushButton#NewChatButton:hover {
    background-color: #2A234A;
    border-color: #A78BFA;
    color: #FFFFFF;
}

/* ========================================================================= */
/* Hero Prompt Suggestion Chips                                              */
/* ========================================================================= */

QPushButton#PromptSuggestionChip {
    background-color: %7;
    color: %6;
    border: 1px solid %4;
    border-radius: 8px;
    padding: 10px 14px;
    font-size: 12px;
    text-align: left;
}

QPushButton#PromptSuggestionChip:hover {
    background-color: %13;
    border-color: %11;
    color: %2;
}

/* ========================================================================= */
/* Line Edits & Search                                                       */
/* ========================================================================= */

QLineEdit {
    background-color: %17;
    color: %2;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}

QLineEdit:focus {
    border-color: %9;
}

/* Plain Text & Text Edit */
QPlainTextEdit, QTextEdit {
    background-color: %17;
    color: %2;
    border: 1px solid %4;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
    selection-background-color: #7C3AED;
    selection-color: #FFFFFF;
}

QPlainTextEdit:focus, QTextEdit:focus {
    border-color: %9;
}

/* ========================================================================= */
/* Conversation List                                                         */
/* ========================================================================= */

QListWidget {
    background-color: transparent;
    border: none;
    outline: none;
    padding: 2px;
}

QListWidget::item {
    background-color: transparent;
    color: %6;
    border-radius: 6px;
    padding: 7px 10px;
    margin-bottom: 2px;
}

QListWidget::item:hover {
    background-color: %12;
    color: %2;
}

QListWidget::item:selected {
    background-color: %13;
    color: #FFFFFF;
    border-left: 3px solid %11;
}

/* ========================================================================= */
/* Tree Views & Projects Explorer                                            */
/* ========================================================================= */

QTreeView {
    background-color: #121124;
    color: #E2E8F0;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 4px;
    outline: none;
}

QTreeView::item {
    padding: 4px;
    border-radius: 4px;
}

QTreeView::item:hover {
    background-color: %12;
}

QTreeView::item:selected {
    background-color: #2C1B5E;
    color: #A78BFA;
}

QHeaderView::section {
    background-color: %3;
    color: %6;
    border: none;
    border-bottom: 1px solid %4;
    padding: 4px 8px;
    font-size: 11px;
    font-weight: 600;
}

/* ========================================================================= */
/* Scroll Areas & Scrollbars                                                 */
/* ========================================================================= */

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

QScrollBar:horizontal {
    background-color: transparent;
    height: 7px;
    margin: 0;
}

QScrollBar::handle:horizontal {
    background-color: #1E2338;
    border-radius: 3px;
    min-width: 20px;
}

QScrollBar::handle:horizontal:hover {
    background-color: #333C5E;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}

QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: none;
}

/* ========================================================================= */
/* Splitter & Dividers                                                       */
/* ========================================================================= */

QSplitter::handle {
    background-color: %4;
}

QSplitter::handle:horizontal {
    width: 1px;
}

/* ========================================================================= */
/* Tooltips                                                                  */
/* ========================================================================= */

QToolTip {
    background-color: %7;
    color: %2;
    border: 1px solid %4;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 11px;
}
)")
    .arg(BgRoot)             // %1
    .arg(TextPrimary)        // %2
    .arg(BgPanel)           // %3
    .arg(Border)            // %4
    .arg(TextAccentCyan)    // %5
    .arg(TextMuted)         // %6
    .arg(BgCard)            // %7
    .arg(BgSidebarHover)    // %8
    .arg(BorderFocus)       // %9
    .arg(BgSidebar)         // %10
    .arg(AccentPurple)      // %11
    .arg(BgSidebarHover)    // %12
    .arg(BgSidebarActive)   // %13
    .arg(BorderHover)       // %14
    .arg(AccentPurpleHover) // %15
    .arg(TextAccentViolet)  // %16
    .arg(BgInput);          // %17
}

} // namespace flex::ui

