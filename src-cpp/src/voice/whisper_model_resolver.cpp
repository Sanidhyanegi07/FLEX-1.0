#include "flex/voice/whisper_model_resolver.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace flex::voice {

QString WhisperModelResolver::defaultModelsDirectory()
{
    // On Windows, %USERPROFILE% is authoritative; fallback to QDir::homePath()
    QString userProfile = qEnvironmentVariable("USERPROFILE").trimmed();
    if (userProfile.isEmpty()) {
        userProfile = QDir::homePath();
    }
    return QDir::cleanPath(QDir(userProfile).filePath(".flex/models/whisper"));
}

QString WhisperModelResolver::defaultModelPath()
{
    return QDir::cleanPath(QDir(defaultModelsDirectory()).filePath(defaultModelFileName()));
}

ModelResolutionResult WhisperModelResolver::resolveModelPath(const QString& customPath)
{
    ModelResolutionResult result;
    QString targetPath = customPath.trimmed();

    if (targetPath.isEmpty()) {
        QString envPath = QString::fromUtf8(qgetenv("FLEX_WHISPER_MODEL_PATH")).trimmed();
        if (!envPath.isEmpty()) {
            targetPath = envPath;
        } else {
            targetPath = defaultModelPath();
        }
    }

    targetPath = QDir::cleanPath(targetPath);
    result.resolvedPath = targetPath;

    QFileInfo info(targetPath);
    if (!info.exists()) {
        result.status = ModelResolutionStatus::Missing;
        result.errorMessage = QString("Whisper speech model not found at: %1. Run scripts/download_whisper_model.ps1 to download the model.")
                                  .arg(targetPath);
        return result;
    }

    if (!info.isReadable()) {
        result.status = ModelResolutionStatus::Unreadable;
        result.errorMessage = QString("Whisper speech model file exists at: %1, but is not readable (check file permissions).")
                                  .arg(targetPath);
        return result;
    }

    // A valid Whisper ggml-tiny.en.bin model file is ~75 MB; require at least 1 MB (1048576 bytes)
    constexpr qint64 kMinValidSizeBytes = 1024 * 1024;
    if (info.size() < kMinValidSizeBytes) {
        result.status = ModelResolutionStatus::TooSmall;
        result.errorMessage = QString("Whisper speech model file at '%1' is incomplete or corrupted (%2 bytes, expected >1MB). Run scripts/download_whisper_model.ps1 to re-download.")
                                  .arg(targetPath)
                                  .arg(info.size());
        return result;
    }

    result.status = ModelResolutionStatus::Found;
    result.resolvedPath = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    result.errorMessage.clear();
    return result;
}

} // namespace flex::voice

