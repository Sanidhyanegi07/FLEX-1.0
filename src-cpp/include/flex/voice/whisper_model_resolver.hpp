#ifndef FLEX_VOICE_WHISPER_MODEL_RESOLVER_HPP
#define FLEX_VOICE_WHISPER_MODEL_RESOLVER_HPP

#include <QString>
#include <vector>

namespace flex::voice {

/**
 * Model resolution status categories.
 */
enum class ModelResolutionStatus {
    Found = 0,
    Missing,
    Unreadable,
    TooSmall
};

/**
 * Detailed result of canonical Whisper model path resolution.
 */
struct ModelResolutionResult {
    ModelResolutionStatus status{ModelResolutionStatus::Missing};
    QString resolvedPath;
    QString errorMessage;

    [[nodiscard]] bool isFound() const noexcept {
        return status == ModelResolutionStatus::Found;
    }
};

/**
 * Model validity status for discovered model files.
 */
enum class ModelValidityStatus {
    Valid = 0,
    Missing,
    Unreadable,
    TooSmall,
    InvalidFormat
};

/**
 * Whisper model speech capability classification.
 */
enum class WhisperModelCapability {
    Unknown = 0,
    EnglishOnly,
    Multilingual
};

/**
 * Metadata and capability representation of an installed or inspected Whisper model.
 */
struct DiscoveredModelInfo {
    QString displayName;
    QString fileName;
    QString absolutePath;
    qint64 fileSizeBytes{0};
    ModelValidityStatus validity{ModelValidityStatus::Missing};
    WhisperModelCapability capability{WhisperModelCapability::Unknown};
    bool isDefaultModel{false};
    bool isSelectedModel{false};
    QString diagnosticMessage;

    [[nodiscard]] bool isValid() const noexcept {
        return validity == ModelValidityStatus::Valid;
    }

    [[nodiscard]] bool isMultilingual() const noexcept {
        return capability == WhisperModelCapability::Multilingual;
    }

    [[nodiscard]] bool isEnglishOnly() const noexcept {
        return capability == WhisperModelCapability::EnglishOnly;
    }
};

/**
 * Canonical model path resolver and local model discovery for FLEX 1.0 speech recognition.
 *
 * Guarantees:
 * - Uses the documented %USERPROFILE%\.flex\models\whisper\ on Windows.
 * - Discovers installed models without loading tensors or model weights into RAM.
 * - Does not alter active settings, active language, or transcription state during discovery.
 * - Qt-clean path normalization and stable alphabetical ordering.
 * - Accurately classifies English-only vs Multilingual models using binary header inspection.
 * - Never downloads models automatically.
 */
class WhisperModelResolver {
public:
    static QString defaultModelFileName() noexcept { return QStringLiteral("ggml-tiny.en.bin"); }
    static QString defaultModelsDirectory();
    static QString defaultModelPath();
    static ModelResolutionResult resolveModelPath(const QString& customPath = QString());

    /**
     * Inspects a specific model file path and returns its metadata, validity, and capability.
     */
    static DiscoveredModelInfo inspectModelFile(
        const QString& filePath,
        const QString& currentlySelectedPath = QString()
    );

    /**
     * Discovers all installed Whisper models in the target directory (defaults to defaultModelsDirectory()),
     * sorted stably in alphabetical order, with deduplicated canonical paths.
     */
    static std::vector<DiscoveredModelInfo> discoverInstalledModels(
        const QString& customDirectory = QString(),
        const QString& currentlySelectedPath = QString()
    );

    static QString whisperModelCapabilityToString(WhisperModelCapability cap);
    static QString modelValidityStatusToString(ModelValidityStatus status);
};

} // namespace flex::voice

#endif // FLEX_VOICE_WHISPER_MODEL_RESOLVER_HPP
