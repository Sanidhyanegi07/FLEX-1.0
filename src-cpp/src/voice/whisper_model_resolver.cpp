#include "flex/voice/whisper_model_resolver.hpp"
#include "whisper_pipeline.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSet>

#include <algorithm>

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

DiscoveredModelInfo WhisperModelResolver::inspectModelFile(
    const QString& filePath,
    const QString& currentlySelectedPath
) {
    DiscoveredModelInfo modelInfo;
    QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        modelInfo.validity = ModelValidityStatus::Missing;
        modelInfo.diagnosticMessage = QStringLiteral("File path is empty.");
        return modelInfo;
    }

    QFileInfo fileInfo(trimmedPath);
    modelInfo.fileName = fileInfo.fileName();
    modelInfo.absolutePath = fileInfo.canonicalFilePath().isEmpty()
                                 ? QDir::cleanPath(fileInfo.absoluteFilePath())
                                 : QDir::cleanPath(fileInfo.canonicalFilePath());
    modelInfo.isDefaultModel = (modelInfo.fileName.compare(defaultModelFileName(), Qt::CaseInsensitive) == 0);

    // Determine selection state
    if (!currentlySelectedPath.trimmed().isEmpty()) {
        QString selClean = QDir::cleanPath(currentlySelectedPath.trimmed());
        QFileInfo selInfo(selClean);
        QString selCanonical = selInfo.canonicalFilePath().isEmpty()
                                   ? selInfo.absoluteFilePath()
                                   : selInfo.canonicalFilePath();
        selCanonical = QDir::cleanPath(selCanonical);

        if (modelInfo.absolutePath.compare(selCanonical, Qt::CaseInsensitive) == 0 ||
            modelInfo.fileName.compare(currentlySelectedPath.trimmed(), Qt::CaseInsensitive) == 0) {
            modelInfo.isSelectedModel = true;
        }
    } else {
        // When no current path is explicitly requested, default model is considered selected
        if (modelInfo.isDefaultModel) {
            modelInfo.isSelectedModel = true;
        }
    }

    if (!fileInfo.exists()) {
        modelInfo.validity = ModelValidityStatus::Missing;
        modelInfo.diagnosticMessage = QString("File does not exist: %1").arg(trimmedPath);
        modelInfo.displayName = modelInfo.fileName;
        return modelInfo;
    }

    modelInfo.fileSizeBytes = fileInfo.size();

    if (!fileInfo.isReadable()) {
        modelInfo.validity = ModelValidityStatus::Unreadable;
        modelInfo.diagnosticMessage = QString("File is not readable (check permissions): %1").arg(trimmedPath);
        modelInfo.displayName = modelInfo.fileName;
        return modelInfo;
    }

    constexpr qint64 kMinValidSizeBytes = 1024 * 1024;
    if (modelInfo.fileSizeBytes < kMinValidSizeBytes) {
        modelInfo.validity = ModelValidityStatus::TooSmall;
        modelInfo.diagnosticMessage = QString("File size (%1 bytes) is below minimum 1MB threshold.")
                                          .arg(modelInfo.fileSizeBytes);
        modelInfo.displayName = modelInfo.fileName;
        return modelInfo;
    }

    // Inspect header without loading model weights into RAM
    flex_whisper_model_header_t hdr;
    QByteArray localPathBytes = fileInfo.absoluteFilePath().toLocal8Bit();
    bool inspected = flex_whisper_inspect_header(localPathBytes.constData(), &hdr);

    if (!inspected || !hdr.is_valid_format) {
        modelInfo.validity = ModelValidityStatus::InvalidFormat;
        modelInfo.capability = WhisperModelCapability::Unknown;
        modelInfo.diagnosticMessage = QString::fromUtf8(hdr.error_detail);
        if (modelInfo.diagnosticMessage.isEmpty()) {
            modelInfo.diagnosticMessage = QStringLiteral("Invalid GGML header or unsupported format.");
        }
        modelInfo.displayName = modelInfo.fileName;
        return modelInfo;
    }

    modelInfo.validity = ModelValidityStatus::Valid;
    switch (hdr.capability) {
        case FLEX_WHISPER_CAP_MULTILINGUAL:
            modelInfo.capability = WhisperModelCapability::Multilingual;
            break;
        case FLEX_WHISPER_CAP_ENGLISH_ONLY:
            modelInfo.capability = WhisperModelCapability::EnglishOnly;
            break;
        default:
            modelInfo.capability = WhisperModelCapability::Unknown;
            break;
    }

    flex_whisper_model_type_t modelType = flex_whisper_classify_model(localPathBytes.constData());
    char dispBuf[128] = {0};
    flex_whisper_format_display_name(
        modelInfo.fileName.toUtf8().constData(),
        modelType,
        hdr.capability,
        dispBuf,
        sizeof(dispBuf)
    );

    modelInfo.displayName = QString::fromUtf8(dispBuf);
    if (modelInfo.displayName.isEmpty()) {
        modelInfo.displayName = modelInfo.fileName;
    }

    return modelInfo;
}

std::vector<DiscoveredModelInfo> WhisperModelResolver::discoverInstalledModels(
    const QString& customDirectory,
    const QString& currentlySelectedPath
) {
    std::vector<DiscoveredModelInfo> results;
    QString targetDir = customDirectory.trimmed().isEmpty()
                            ? defaultModelsDirectory()
                            : customDirectory.trimmed();

    QDir dir(targetDir);
    if (!dir.exists()) {
        return results;
    }

    QStringList filters;
    filters << QStringLiteral("*.bin");
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::Readable | QDir::NoSymLinks);
    dir.setSorting(QDir::Name | QDir::IgnoreCase);

    QFileInfoList entries = dir.entryInfoList();
    QSet<QString> seenCanonicalPaths;

    for (const QFileInfo& entry : entries) {
        QString canonPath = entry.canonicalFilePath().isEmpty()
                                ? QDir::cleanPath(entry.absoluteFilePath())
                                : QDir::cleanPath(entry.canonicalFilePath());
        if (seenCanonicalPaths.contains(canonPath)) {
            continue;
        }
        seenCanonicalPaths.insert(canonPath);

        DiscoveredModelInfo info = inspectModelFile(entry.absoluteFilePath(), currentlySelectedPath);
        results.push_back(info);
    }

    // Ensure stable alphabetical ordering by filename
    std::stable_sort(results.begin(), results.end(), [](const DiscoveredModelInfo& a, const DiscoveredModelInfo& b) {
        return a.fileName.compare(b.fileName, Qt::CaseInsensitive) < 0;
    });

    return results;
}

QString WhisperModelResolver::whisperModelCapabilityToString(WhisperModelCapability cap)
{
    switch (cap) {
        case WhisperModelCapability::EnglishOnly:
            return QStringLiteral("English-only");
        case WhisperModelCapability::Multilingual:
            return QStringLiteral("Multilingual");
        case WhisperModelCapability::Unknown:
        default:
            return QStringLiteral("Unknown");
    }
}

QString WhisperModelResolver::modelValidityStatusToString(ModelValidityStatus status)
{
    switch (status) {
        case ModelValidityStatus::Valid:
            return QStringLiteral("Valid");
        case ModelValidityStatus::Missing:
            return QStringLiteral("Missing");
        case ModelValidityStatus::Unreadable:
            return QStringLiteral("Unreadable");
        case ModelValidityStatus::TooSmall:
            return QStringLiteral("TooSmall");
        case ModelValidityStatus::InvalidFormat:
            return QStringLiteral("InvalidFormat");
        default:
            return QStringLiteral("Unknown");
    }
}

} // namespace flex::voice
