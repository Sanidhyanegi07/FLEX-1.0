#include "flex/ui/theme.hpp"
#include "theme_engine.h"
#include <vector>
#include <string>

namespace flex::ui {

QString Theme::getApplicationStylesheet()
{
    static std::string s_cached_stylesheet;
    if (s_cached_stylesheet.empty()) {
        std::vector<char> buffer(16384);
        size_t written = flex_theme_generate_desktop_stylesheet(buffer.data(), buffer.size());
        if (written > 0) {
            s_cached_stylesheet.assign(buffer.data(), written);
        }
    }
    return QString::fromStdString(s_cached_stylesheet);
}

} // namespace flex::ui
