#ifndef FLEX_WHISPER_PIPELINE_H
#define FLEX_WHISPER_PIPELINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_WHISPER_MODEL_TINY = 0,
    FLEX_WHISPER_MODEL_BASE = 1,
    FLEX_WHISPER_MODEL_SMALL = 2,
    FLEX_WHISPER_MODEL_MEDIUM = 3,
    FLEX_WHISPER_MODEL_LARGE = 4,
    FLEX_WHISPER_MODEL_UNKNOWN = 5
} flex_whisper_model_type_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    size_t sample_count;
    float duration_seconds;
    float peak_amplitude;
    bool has_nans_or_infs;
    bool is_valid_for_inference;
    char error_detail[128];
} flex_whisper_audio_spec_t;

typedef struct {
    int64_t start_time_ms;
    int64_t end_time_ms;
    char text[256];
    float confidence;
} flex_whisper_segment_t;

/**
 * Validates audio sample array before passing to whisper.cpp.
 */
bool flex_whisper_validate_audio_input(
    const float* samples,
    size_t sample_count,
    uint32_t sample_rate,
    flex_whisper_audio_spec_t* out_spec
);

/**
 * Formats segment start and end timestamps into "[MM:SS.mmm --> MM:SS.mmm]".
 */
size_t flex_whisper_format_timestamp_range(
    int64_t start_ms,
    int64_t end_ms,
    char* out_buf,
    size_t buf_cap
);

/**
 * Cleans whisper transcript: removes hallucinated tokens like "[BLANK_AUDIO]",
 * normalizes whitespace, and trims leading/trailing spaces.
 */
size_t flex_whisper_clean_transcript(
    const char* raw_text,
    char* out_cleaned,
    size_t out_cap
);

/**
 * Classifies model type from filename or path.
 */
flex_whisper_model_type_t flex_whisper_classify_model(const char* model_path);

/**
 * Returns estimated RAM memory requirement in megabytes for the model type.
 */
uint32_t flex_whisper_model_ram_mb(flex_whisper_model_type_t model_type);

typedef struct {
    char effective_language[16];
    bool is_multilingual_required;
    bool fallback_applied;
    char warning_message[128];
} flex_whisper_lang_config_t;

/**
 * Checks whether a Whisper model filename or path indicates multilingual support.
 * Returns false if the path contains ".en." or ends with ".en.bin" (English-only).
 * Returns true for standard multilingual models or unknown.
 */
bool flex_whisper_is_model_multilingual_by_name(const char* model_path);

/**
 * Resolves requested transcription language against model capabilities.
 * If model is English-only and language is "hi" or "auto", cleanly falls back to "en"
 * and records an informative warning message.
 */
bool flex_whisper_resolve_language(
    const char* requested_language,
    bool model_is_multilingual,
    flex_whisper_lang_config_t* out_config
);

/**
 * Builds the default technical and contextual vocabulary prompt for Whisper inference.
 * If custom_terms is provided, combines it with the base technical glossary:
 * "FLEX, C11, C++, CMake, MinGW-w64, GCC, Qt, SQLite, Ollama, Qwen, Whisper"
 * Returns number of bytes written to out_buf.
 */
size_t flex_whisper_build_default_prompt(
    const char* custom_terms,
    char* out_buf,
    size_t buf_cap
);

typedef enum {
    FLEX_WHISPER_CAP_UNKNOWN = 0,
    FLEX_WHISPER_CAP_ENGLISH_ONLY = 1,
    FLEX_WHISPER_CAP_MULTILINGUAL = 2
} flex_whisper_capability_t;

typedef struct {
    bool is_valid_format;
    uint32_t magic;
    int32_t n_vocab;
    int32_t n_audio_ctx;
    int32_t n_audio_state;
    int64_t file_size_bytes;
    flex_whisper_capability_t capability;
    char error_detail[128];
} flex_whisper_model_header_t;

/**
 * Reads and validates the GGML Whisper model header (first 16 bytes)
 * without loading tensors or allocating model weights into RAM.
 *
 * Verifies GGML magic (0x67676d6c / "ggml") and inspects vocabulary size:
 * - n_vocab >= 51865: Multilingual model
 * - n_vocab >= 50000: English-only model (standard 51864)
 * - Otherwise: Unknown / invalid format.
 *
 * Returns true if the file exists, is readable, >= 1MB, and has valid GGML magic.
 */
bool flex_whisper_inspect_header(
    const char* file_path,
    flex_whisper_model_header_t* out_header
);

/**
 * Formats a user-friendly display name for a model file based on its filename,
 * classified model type, and verified capability.
 */
size_t flex_whisper_format_display_name(
    const char* filename,
    flex_whisper_model_type_t model_type,
    flex_whisper_capability_t capability,
    char* out_name,
    size_t cap
);

/**
 * Returns a static string representation of a capability enum.
 */
const char* flex_whisper_capability_to_string(flex_whisper_capability_t cap);

/**
 * Validates a candidate model path for user selection.
 * Empty or NULL paths are valid (designating auto-resolved default model).
 * Non-empty paths must exist, be readable, >= 1MB, and possess a valid GGML header.
 */
bool flex_whisper_validate_model_selection(
    const char* model_path,
    char* out_diagnostic,
    size_t diag_cap
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_WHISPER_PIPELINE_H */

