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

#ifdef __cplusplus
}
#endif

#endif /* FLEX_WHISPER_PIPELINE_H */

