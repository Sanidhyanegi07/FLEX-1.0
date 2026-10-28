#ifndef FLEX_VOICE_WHISPER_MODEL_RESOLVER_HPP
#define FLEX_VOICE_WHISPER_MODEL_RESOLVER_HPP

#include <QString>

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
 * Canonical model path resolver for FLEX 1.0 speech recognition.
 *
 * Guarantees:
 * - Uses the documented %USERPROFILE%\.flex\models\whisper\ggml-tiny.en.bin on Windows.
 * - Does not depend on current working directory.
 * - Qt-clean path normalization.
 * - Clearly distinguishes missing files, unreadable files, and undersized/corrupted files.
 * - Never downloads models automatically.
 */
class WhisperModelResolver {
public:
    static QString defaultModelFileName() noexcept { return QStringLiteral("ggml-tiny.en.bin"); }
    static QString defaultModelsDirectory();
    static QString defaultModelPath();
    static ModelResolutionResult resolveModelPath(const QString& customPath = QString());
};

} // namespace flex::voice

#endif // FLEX_VOICE_WHISPER_MODEL_RESOLVER_HPP

