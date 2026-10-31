#include "flex/settings_persistence_service.hpp"
#include "kv_store.h"
#include "config_validator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstring>

namespace flex {

std::string SettingsPersistenceService::defaultConfigurationPath()
{
    QByteArray envOverride = qgetenv("FLEX_CONFIG_PATH");
    if (!envOverride.isEmpty() && !envOverride.trimmed().isEmpty()) {
        return QDir::cleanPath(QString::fromLocal8Bit(envOverride.trimmed())).toStdString();
    }

    QByteArray localAppData = qgetenv("LOCALAPPDATA");
    QString baseDir;
    if (!localAppData.isEmpty() && !localAppData.trimmed().isEmpty()) {
        baseDir = QDir(QString::fromLocal8Bit(localAppData.trimmed())).filePath("FLEX");
    } else {
        baseDir = QDir(QDir::homePath()).filePath(".flex");
    }

    return QDir::cleanPath(QDir(baseDir).filePath("config.ini")).toStdString();
}

static void appSettingsToCConfig(const AppSettings& src, FlexAppConfig* dst)
{
    std::memset(dst, 0, sizeof(FlexAppConfig));
    std::strncpy(dst->endpoint, src.ollamaEndpoint.c_str(), sizeof(dst->endpoint) - 1);
    std::strncpy(dst->model, src.ollamaModel.c_str(), sizeof(dst->model) - 1);
    dst->temperature = src.ollamaTemperature;
    dst->num_ctx = src.ollamaNumCtx;
    dst->timeout_seconds = src.ollamaTimeout;
    std::strncpy(dst->audio_input_device, src.audioInputDevice.c_str(), sizeof(dst->audio_input_device) - 1);
    dst->tts_rate = src.ttsRate;
    dst->tts_volume = src.ttsVolume;
    std::strncpy(dst->whisper_model_path, src.whisperModelPath.c_str(), sizeof(dst->whisper_model_path) - 1);
    std::strncpy(dst->whisper_language, src.whisperLanguage.empty() ? "en" : src.whisperLanguage.c_str(), sizeof(dst->whisper_language) - 1);
    std::strncpy(dst->whisper_prompt, src.whisperPrompt.c_str(), sizeof(dst->whisper_prompt) - 1);
}

static void cConfigToAppSettings(const FlexAppConfig& src, AppSettings* dst)
{
    dst->ollamaEndpoint = src.endpoint;
    dst->ollamaModel = src.model;
    dst->ollamaTemperature = src.temperature;
    dst->ollamaNumCtx = src.num_ctx;
    dst->ollamaTimeout = src.timeout_seconds;
    dst->audioInputDevice = src.audio_input_device;
    dst->ttsRate = src.tts_rate;
    dst->ttsVolume = src.tts_volume;
    dst->whisperModelPath = src.whisper_model_path;
    dst->whisperLanguage = (src.whisper_language[0] != '\0') ? src.whisper_language : "en";
    dst->whisperPrompt = src.whisper_prompt;
}

std::vector<std::string> SettingsPersistenceService::validateSettings(const AppSettings& settings)
{
    FlexAppConfig cConfig;
    appSettingsToCConfig(settings, &cConfig);

    FlexAppConfigValidation val;
    flex_validate_app_config(&cConfig, &val);

    std::vector<std::string> errors;
    for (int i = 0; i < val.error_count; ++i) {
        errors.push_back(val.errors[i]);
    }
    return errors;
}

SettingsLoadResult SettingsPersistenceService::loadSettings(const std::optional<std::string>& customPath)
{
    SettingsLoadResult result;
    AppSettings defaults;
    result.settings = defaults;

    QString targetPath = QString::fromStdString(customPath.value_or(defaultConfigurationPath()));
    QFileInfo fileInfo(targetPath);

    if (!fileInfo.exists()) {
        result.success = true;
        result.fileExisted = false;
        return result;
    }

    result.fileExisted = true;

    QFile file(targetPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.success = false;
        result.errorMessage = "Failed to open configuration file for reading: " + targetPath.toStdString();
        return result;
    }

    QByteArray rawBytes = file.readAll();
    file.close();

    FlexKvStore store;
    flex_kv_init(&store);
    if (!flex_kv_parse(&store, rawBytes.constData(), static_cast<size_t>(rawBytes.size()))) {
        result.success = false;
        result.errorMessage = "Configuration file syntax is malformed or corrupted: " + targetPath.toStdString();
        return result;
    }

    FlexAppConfig cConfig;
    FlexAppConfigValidation valRes;
    flex_app_config_from_kv(&store, &cConfig, &valRes);

    result.success = true;
    cConfigToAppSettings(cConfig, &result.settings);

    for (int i = 0; i < valRes.error_count; ++i) {
        result.warnings.push_back(valRes.errors[i]);
    }

    return result;
}

SettingsSaveResult SettingsPersistenceService::saveSettings(
    const AppSettings& settings,
    const std::optional<std::string>& customPath)
{
    // Step 1: Pre-validate settings using C core validator
    FlexAppConfig cConfig;
    appSettingsToCConfig(settings, &cConfig);

    FlexAppConfigValidation validation;
    if (!flex_validate_app_config(&cConfig, &validation)) {
        std::string errStr = "Validation failed: ";
        for (int i = 0; i < validation.error_count; ++i) {
            if (i > 0) errStr += "; ";
            errStr += validation.errors[i];
        }
        return {false, errStr};
    }

    // Step 2: Populate C kv_store via C core schema mapper
    FlexKvStore store;
    if (!flex_app_config_to_kv(&cConfig, &store)) {
        return {false, "Failed to assemble key-value store pairs (capacity exceeded)"};
    }

    // Step 3: Serialize to buffer
    char buffer[4096];
    size_t written = flex_kv_serialize(&store, buffer, sizeof(buffer));
    if (written == 0) {
        return {false, "Serialization buffer capacity exceeded; save aborted to prevent truncated file"};
    }

    // Step 4: Ensure target directory exists
    QString targetPath = QString::fromStdString(customPath.value_or(defaultConfigurationPath()));
    QFileInfo targetInfo(targetPath);
    QDir targetDir = targetInfo.dir();

    if (!targetDir.exists()) {
        if (!targetDir.mkpath(".")) {
            return {false, "Failed to create directory: " + targetDir.absolutePath().toStdString()};
        }
    }

    // Step 5: Atomic write through temp file and replacement
    QString tempPath = targetPath + ".tmp." + QString::number(QCoreApplication::applicationPid()) + "_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return {false, "Failed to open temporary settings file: " + tempPath.toStdString()};
    }

    qint64 bytesWritten = tempFile.write(buffer, static_cast<qint64>(written));
    tempFile.flush();
    tempFile.close();

    if (bytesWritten != static_cast<qint64>(written)) {
        QFile::remove(tempPath);
        return {false, "Incomplete write to temporary settings file: " + tempPath.toStdString()};
    }

    // Atomic replacement handling on Windows
    if (QFile::exists(targetPath)) {
        QString backupPath = targetPath + ".bak";
        if (QFile::exists(backupPath)) {
            QFile::remove(backupPath);
        }
        if (!QFile::rename(targetPath, backupPath)) {
            QFile::remove(tempPath);
            return {false, "Failed to backup existing configuration prior to replacement"};
        }
        if (!QFile::rename(tempPath, targetPath)) {
            // Restore from backup on failure
            QFile::rename(backupPath, targetPath);
            QFile::remove(tempPath);
            return {false, "Failed to replace configuration with new temporary file"};
        }
        QFile::remove(backupPath);
    } else {
        if (!QFile::rename(tempPath, targetPath)) {
            QFile::remove(tempPath);
            return {false, "Failed to rename temporary file to destination: " + targetPath.toStdString()};
        }
    }

    return {true, ""};
}

} // namespace flex
