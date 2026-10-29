#include "flex/settings_persistence_service.hpp"
#include "kv_store.h"
#include "config_validator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

std::vector<std::string> SettingsPersistenceService::validateSettings(const AppSettings& settings)
{
    std::vector<std::string> errors;
    flex_cfg_result_t res;

    if (flex_validate_endpoint_url(settings.ollamaEndpoint.c_str(), &res) != FLEX_CFG_VALID) {
        errors.push_back(std::string("ollama.endpoint: ") + res.error_detail);
    }

    if (flex_validate_model_name(settings.ollamaModel.c_str(), &res) != FLEX_CFG_VALID) {
        errors.push_back(std::string("ollama.model: ") + res.error_detail);
    }

    if (flex_validate_temperature(settings.ollamaTemperature, &res) != FLEX_CFG_VALID) {
        errors.push_back(std::string("ollama.temperature: ") + res.error_detail);
    }

    if (flex_validate_context_window(settings.ollamaNumCtx, &res) != FLEX_CFG_VALID) {
        errors.push_back(std::string("ollama.num_ctx: ") + res.error_detail);
    }

    if (flex_validate_timeout(settings.ollamaTimeout, &res) != FLEX_CFG_VALID) {
        errors.push_back(std::string("ollama.timeout: ") + res.error_detail);
    }

    if (flex_validate_tts_rate(settings.ttsRate, &res) != FLEX_CFG_VALID) {
        errors.push_back(std::string("tts.rate: ") + res.error_detail);
    }

    if (flex_validate_tts_volume(settings.ttsVolume, &res) != FLEX_CFG_VALID) {
        errors.push_back(std::string("tts.volume: ") + res.error_detail);
    }

    if (!settings.whisperModelPath.empty()) {
        if (flex_validate_whisper_path(settings.whisperModelPath.c_str(), &res) != FLEX_CFG_VALID) {
            errors.push_back(std::string("whisper.model_path: ") + res.error_detail);
        }
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

    result.success = true;

    // 1. Ollama Endpoint
    const char* endpointVal = flex_kv_get_str(&store, "ollama.endpoint");
    if (endpointVal) {
        flex_cfg_result_t cfgRes;
        if (flex_validate_endpoint_url(endpointVal, &cfgRes) == FLEX_CFG_VALID) {
            result.settings.ollamaEndpoint = endpointVal;
        } else {
            result.warnings.push_back(std::string("Invalid ollama.endpoint in config: ") + cfgRes.error_detail);
        }
    }

    // 2. Ollama Model
    const char* modelVal = flex_kv_get_str(&store, "ollama.model");
    if (modelVal) {
        flex_cfg_result_t cfgRes;
        if (flex_validate_model_name(modelVal, &cfgRes) == FLEX_CFG_VALID) {
            result.settings.ollamaModel = modelVal;
        } else {
            result.warnings.push_back(std::string("Invalid ollama.model in config: ") + cfgRes.error_detail);
        }
    }

    // 3. Temperature
    double tempVal = 0.0;
    FlexKvStatus st = flex_kv_parse_float(&store, "ollama.temperature", &tempVal);
    if (st == FLEX_KV_OK) {
        flex_cfg_result_t cfgRes;
        if (flex_validate_temperature(tempVal, &cfgRes) == FLEX_CFG_VALID) {
            result.settings.ollamaTemperature = tempVal;
        } else {
            result.warnings.push_back(std::string("Invalid ollama.temperature: ") + cfgRes.error_detail);
        }
    } else if (st != FLEX_KV_ERR_NOT_FOUND) {
        result.warnings.push_back(std::string("Malformed ollama.temperature: ") + flex_kv_status_str(st));
    }

    // 4. Num Ctx
    long long ctxVal = 0;
    st = flex_kv_parse_int(&store, "ollama.num_ctx", &ctxVal);
    if (st == FLEX_KV_OK) {
        flex_cfg_result_t cfgRes;
        if (flex_validate_context_window(static_cast<int>(ctxVal), &cfgRes) == FLEX_CFG_VALID) {
            result.settings.ollamaNumCtx = static_cast<int>(ctxVal);
        } else {
            result.warnings.push_back(std::string("Invalid ollama.num_ctx: ") + cfgRes.error_detail);
        }
    } else if (st != FLEX_KV_ERR_NOT_FOUND) {
        result.warnings.push_back(std::string("Malformed ollama.num_ctx: ") + flex_kv_status_str(st));
    }

    // 5. Timeout
    long long timeoutVal = 0;
    st = flex_kv_parse_int(&store, "ollama.timeout", &timeoutVal);
    if (st == FLEX_KV_OK) {
        flex_cfg_result_t cfgRes;
        if (flex_validate_timeout(static_cast<int>(timeoutVal), &cfgRes) == FLEX_CFG_VALID) {
            result.settings.ollamaTimeout = static_cast<int>(timeoutVal);
        } else {
            result.warnings.push_back(std::string("Invalid ollama.timeout: ") + cfgRes.error_detail);
        }
    } else if (st != FLEX_KV_ERR_NOT_FOUND) {
        result.warnings.push_back(std::string("Malformed ollama.timeout: ") + flex_kv_status_str(st));
    }

    // 6. Audio input device
    const char* audioDev = flex_kv_get_str(&store, "audio.input_device");
    if (audioDev) {
        result.settings.audioInputDevice = audioDev;
    }

    // 7. TTS Rate
    long long rateVal = 0;
    st = flex_kv_parse_int(&store, "tts.rate", &rateVal);
    if (st == FLEX_KV_OK) {
        flex_cfg_result_t cfgRes;
        if (flex_validate_tts_rate(static_cast<int>(rateVal), &cfgRes) == FLEX_CFG_VALID) {
            result.settings.ttsRate = static_cast<int>(rateVal);
        } else {
            result.warnings.push_back(std::string("Invalid tts.rate: ") + cfgRes.error_detail);
        }
    }

    // 8. TTS Volume
    long long volVal = 0;
    st = flex_kv_parse_int(&store, "tts.volume", &volVal);
    if (st == FLEX_KV_OK) {
        flex_cfg_result_t cfgRes;
        if (flex_validate_tts_volume(static_cast<int>(volVal), &cfgRes) == FLEX_CFG_VALID) {
            result.settings.ttsVolume = static_cast<int>(volVal);
        } else {
            result.warnings.push_back(std::string("Invalid tts.volume: ") + cfgRes.error_detail);
        }
    }

    // 9. Whisper model path
    const char* whisperPath = flex_kv_get_str(&store, "whisper.model_path");
    if (whisperPath && whisperPath[0] != '\0') {
        flex_cfg_result_t cfgRes;
        if (flex_validate_whisper_path(whisperPath, &cfgRes) == FLEX_CFG_VALID) {
            result.settings.whisperModelPath = whisperPath;
        } else {
            result.warnings.push_back(std::string("Invalid whisper.model_path: ") + cfgRes.error_detail);
        }
    }

    return result;
}

SettingsSaveResult SettingsPersistenceService::saveSettings(
    const AppSettings& settings,
    const std::optional<std::string>& customPath)
{
    // Step 1: Pre-validate settings using C core validator
    auto errors = validateSettings(settings);
    if (!errors.empty()) {
        std::string errStr = "Validation failed: ";
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) errStr += "; ";
            errStr += errors[i];
        }
        return {false, errStr};
    }

    // Step 2: Populate C kv_store
    FlexKvStore store;
    flex_kv_init(&store);

    if (!flex_kv_set(&store, "ollama.endpoint", settings.ollamaEndpoint.c_str()) ||
        !flex_kv_set(&store, "ollama.model", settings.ollamaModel.c_str()) ||
        !flex_kv_set_float(&store, "ollama.temperature", settings.ollamaTemperature) ||
        !flex_kv_set_int(&store, "ollama.num_ctx", settings.ollamaNumCtx) ||
        !flex_kv_set_int(&store, "ollama.timeout", settings.ollamaTimeout) ||
        !flex_kv_set(&store, "audio.input_device", settings.audioInputDevice.c_str()) ||
        !flex_kv_set_int(&store, "tts.rate", settings.ttsRate) ||
        !flex_kv_set_int(&store, "tts.volume", settings.ttsVolume)) {
        return {false, "Failed to assemble key-value store pairs (capacity exceeded)"};
    }

    if (!settings.whisperModelPath.empty()) {
        if (!flex_kv_set(&store, "whisper.model_path", settings.whisperModelPath.c_str())) {
            return {false, "Failed to store whisper.model_path"};
        }
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

