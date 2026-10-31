#ifndef FLEX_SETTINGS_PERSISTENCE_SERVICE_HPP
#define FLEX_SETTINGS_PERSISTENCE_SERVICE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flex {

/**
 * Representation of persistent user configuration and hardware preferences.
 */
struct AppSettings {
    // Inference connection parameters
    std::string ollamaEndpoint{"http://127.0.0.1:11434"};
    std::string ollamaModel{"qwen2.5-coder:3b"};
    double ollamaTemperature{0.2};
    int ollamaNumCtx{4096};
    int ollamaTimeout{30};

    // Voice & Hardware preferences
    std::string audioInputDevice{""}; // Empty string indicates system default
    int ttsRate{0};                   // SAPI rate: -10 to +10, default 0
    int ttsVolume{100};               // SAPI volume: 0 to 100, default 100
    std::string whisperModelPath{""}; // Empty string indicates auto-resolved path
    std::string whisperLanguage{"en"}; // "en", "hi", "auto"
    std::string whisperPrompt{""};    // Optional technical/contextual prompt
};

/**
 * Result of loading settings from disk.
 */
struct SettingsLoadResult {
    bool success{false};
    AppSettings settings;
    bool fileExisted{false};
    std::string errorMessage;
    std::vector<std::string> warnings;
};

/**
 * Result of saving settings to disk.
 */
struct SettingsSaveResult {
    bool success{false};
    std::string errorMessage;
};

/**
 * Service managing persistent INI configuration backed by the C11 kv_store module.
 *
 * Guarantees:
 * - Default path: %LOCALAPPDATA%\FLEX\config.ini
 * - Environment override: FLEX_CONFIG_PATH
 * - Validates schema before applying
 * - Atomic write through temporary file and safe rename
 * - Zero credentials or secrets stored
 */
class SettingsPersistenceService {
public:
    static std::string defaultConfigurationPath();
    static SettingsLoadResult loadSettings(const std::optional<std::string>& customPath = std::nullopt);
    static SettingsSaveResult saveSettings(const AppSettings& settings, const std::optional<std::string>& customPath = std::nullopt);
    static std::vector<std::string> validateSettings(const AppSettings& settings);
};

} // namespace flex

#endif // FLEX_SETTINGS_PERSISTENCE_SERVICE_HPP

