#include "whisper_pipeline.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>

bool flex_whisper_validate_audio_input(
    const float* samples,
    size_t sample_count,
    uint32_t sample_rate,
    flex_whisper_audio_spec_t* out_spec
) {
    if (out_spec) {
        memset(out_spec, 0, sizeof(flex_whisper_audio_spec_t));
    }

    if (!samples || sample_count == 0) {
        if (out_spec) {
            snprintf(out_spec->error_detail, sizeof(out_spec->error_detail), "Audio buffer is empty or NULL");
        }
        return false;
    }

    float peak = 0.0f;
    bool bad_samples = false;

    for (size_t i = 0; i < sample_count; ++i) {
        float s = samples[i];
        if (isnan(s) || isinf(s)) {
            bad_samples = true;
            break;
        }
        float abs_s = fabsf(s);
        if (abs_s > peak) peak = abs_s;
    }

    float dur = (sample_rate > 0) ? ((float)sample_count / (float)sample_rate) : 0.0f;

    if (out_spec) {
        out_spec->sample_rate = sample_rate;
        out_spec->channels = 1;
        out_spec->sample_count = sample_count;
        out_spec->duration_seconds = dur;
        out_spec->peak_amplitude = peak;
        out_spec->has_nans_or_infs = bad_samples;
    }

    if (bad_samples) {
        if (out_spec) {
            snprintf(out_spec->error_detail, sizeof(out_spec->error_detail), "Audio samples contain NaN or infinite values");
        }
        return false;
    }

    if (sample_rate != 16000) {
        if (out_spec) {
            snprintf(out_spec->error_detail, sizeof(out_spec->error_detail), "Whisper requires 16000 Hz, got %u Hz", sample_rate);
        }
        return false;
    }

    if (dur > 30.5f) {
        if (out_spec) {
            snprintf(out_spec->error_detail, sizeof(out_spec->error_detail), "Audio duration (%.2fs) exceeds 30.0s safety cap", dur);
        }
        return false;
    }

    if (out_spec) {
        out_spec->is_valid_for_inference = true;
    }
    return true;
}

size_t flex_whisper_format_timestamp_range(
    int64_t start_ms,
    int64_t end_ms,
    char* out_buf,
    size_t buf_cap
) {
    if (!out_buf || buf_cap < 32) return 0;

    int start_m = (int)(start_ms / 60000);
    int start_s = (int)((start_ms % 60000) / 1000);
    int start_ms_rem = (int)(start_ms % 1000);

    int end_m = (int)(end_ms / 60000);
    int end_s = (int)((end_ms % 60000) / 1000);
    int end_ms_rem = (int)(end_ms % 1000);

    int written = snprintf(
        out_buf, buf_cap,
        "[%02d:%02d.%03d --> %02d:%02d.%03d]",
        start_m, start_s, start_ms_rem,
        end_m, end_s, end_ms_rem
    );

    if (written < 0 || (size_t)written >= buf_cap) {
        out_buf[buf_cap - 1] = '\0';
        return buf_cap - 1;
    }
    return (size_t)written;
}

size_t flex_whisper_clean_transcript(
    const char* raw_text,
    char* out_cleaned,
    size_t out_cap
) {
    if (!raw_text || !out_cleaned || out_cap < 2) return 0;

    static const char* const S_FILTER_TOKENS[] = {
        "[BLANK_AUDIO]",
        "(blank audio)",
        "[Applause]",
        "[Music]",
        "[music]",
        "(music)",
        NULL
    };

    /* Skip leading whitespace */
    while (*raw_text && isspace((unsigned char)*raw_text)) raw_text++;

    char temp[1024];
    snprintf(temp, sizeof(temp), "%.1023s", raw_text);

    /* Strip filter tokens */
    for (size_t k = 0; S_FILTER_TOKENS[k] != NULL; ++k) {
        char* match;
        size_t tok_len = strlen(S_FILTER_TOKENS[k]);
        while ((match = strstr(temp, S_FILTER_TOKENS[k])) != NULL) {
            memmove(match, match + tok_len, strlen(match + tok_len) + 1);
        }
    }

    /* Normalize whitespace */
    size_t out_idx = 0;
    bool in_space = false;

    for (size_t i = 0; temp[i] && out_idx + 1 < out_cap; ++i) {
        char c = temp[i];
        if (isspace((unsigned char)c)) {
            if (!in_space && out_idx > 0) {
                out_cleaned[out_idx++] = ' ';
                in_space = true;
            }
        } else {
            out_cleaned[out_idx++] = c;
            in_space = false;
        }
    }

    /* Trim trailing space */
    while (out_idx > 0 && out_cleaned[out_idx - 1] == ' ') {
        out_idx--;
    }

    out_cleaned[out_idx] = '\0';
    return out_idx;
}

flex_whisper_model_type_t flex_whisper_classify_model(const char* model_path) {
    if (!model_path) return FLEX_WHISPER_MODEL_UNKNOWN;

    if (strstr(model_path, "tiny")) return FLEX_WHISPER_MODEL_TINY;
    if (strstr(model_path, "base")) return FLEX_WHISPER_MODEL_BASE;
    if (strstr(model_path, "small")) return FLEX_WHISPER_MODEL_SMALL;
    if (strstr(model_path, "medium")) return FLEX_WHISPER_MODEL_MEDIUM;
    if (strstr(model_path, "large")) return FLEX_WHISPER_MODEL_LARGE;

    return FLEX_WHISPER_MODEL_UNKNOWN;
}

uint32_t flex_whisper_model_ram_mb(flex_whisper_model_type_t model_type) {
    switch (model_type) {
        case FLEX_WHISPER_MODEL_TINY: return 75;
        case FLEX_WHISPER_MODEL_BASE: return 142;
        case FLEX_WHISPER_MODEL_SMALL: return 466;
        case FLEX_WHISPER_MODEL_MEDIUM: return 1460;
        case FLEX_WHISPER_MODEL_LARGE: return 2950;
        default: return 100;
    }
}

bool flex_whisper_is_model_multilingual_by_name(const char* model_path) {
    if (!model_path || model_path[0] == '\0') {
        return true;
    }

    char lower[256];
    size_t len = strlen(model_path);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; ++i) {
        lower[i] = (char)tolower((unsigned char)model_path[i]);
    }
    lower[len] = '\0';

    if (strstr(lower, ".en.") != NULL ||
        strstr(lower, "-en.") != NULL ||
        strstr(lower, ".en.bin") != NULL ||
        strstr(lower, "tiny.en") != NULL ||
        strstr(lower, "base.en") != NULL ||
        strstr(lower, "small.en") != NULL ||
        strstr(lower, "medium.en") != NULL) {
        return false;
    }

    return true;
}

bool flex_whisper_resolve_language(
    const char* requested_language,
    bool model_is_multilingual,
    flex_whisper_lang_config_t* out_config
) {
    if (!out_config) return false;
    memset(out_config, 0, sizeof(flex_whisper_lang_config_t));

    const char* req = (requested_language && requested_language[0] != '\0')
                      ? requested_language : "en";

    char lower[16];
    size_t len = strlen(req);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; ++i) {
        lower[i] = (char)tolower((unsigned char)req[i]);
    }
    lower[len] = '\0';

    if (strcmp(lower, "hi") == 0 || strcmp(lower, "auto") == 0) {
        out_config->is_multilingual_required = true;
    } else {
        out_config->is_multilingual_required = false;
    }

    if (!model_is_multilingual && out_config->is_multilingual_required) {
        strncpy(out_config->effective_language, "en", sizeof(out_config->effective_language) - 1);
        out_config->fallback_applied = true;
        snprintf(out_config->warning_message, sizeof(out_config->warning_message),
                 "Model is English-only; requested language '%s' fell back to 'en'.", lower);
    } else {
        strncpy(out_config->effective_language, lower, sizeof(out_config->effective_language) - 1);
        out_config->fallback_applied = false;
        out_config->warning_message[0] = '\0';
    }

    return true;
}

size_t flex_whisper_build_default_prompt(
    const char* custom_terms,
    char* out_buf,
    size_t buf_cap
) {
    if (!out_buf || buf_cap < 8) return 0;

    static const char BASE_GLOSSARY[] =
        "FLEX, C11, C++, CMake, MinGW-w64, GCC, Qt, SQLite, Ollama, Qwen, Whisper";

    int written = 0;
    if (custom_terms && custom_terms[0] != '\0') {
        written = snprintf(out_buf, buf_cap, "%s, %s", custom_terms, BASE_GLOSSARY);
    } else {
        written = snprintf(out_buf, buf_cap, "%s", BASE_GLOSSARY);
    }

    if (written < 0 || (size_t)written >= buf_cap) {
        out_buf[buf_cap - 1] = '\0';
        return buf_cap - 1;
    }
    return (size_t)written;
}


#define FLEX_GGML_FILE_MAGIC 0x67676d6c /* "ggml" in little-endian ASCII */

bool flex_whisper_inspect_header(
    const char* file_path,
    flex_whisper_model_header_t* out_header
) {
    if (out_header) {
        memset(out_header, 0, sizeof(flex_whisper_model_header_t));
        out_header->capability = FLEX_WHISPER_CAP_UNKNOWN;
    }

    if (!file_path || file_path[0] == '\0') {
        if (out_header) {
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "Model file path is empty or NULL");
        }
        return false;
    }

    FILE* f = fopen(file_path, "rb");
    if (!f) {
        if (out_header) {
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "Cannot open model file for reading (missing or inaccessible)");
        }
        return false;
    }

    /* Measure file size safely */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (out_header) {
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "Failed to determine file size");
        }
        return false;
    }

    long long size = (long long)ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (out_header) {
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "Failed to rewind model file");
        }
        return false;
    }

    if (out_header) {
        out_header->file_size_bytes = size;
    }

    /* Valid Whisper models are >= 1 MB */
    if (size < 1024 * 1024) {
        fclose(f);
        if (out_header) {
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "File size (%lld bytes) is below minimum 1MB threshold", size);
        }
        return false;
    }

    /* Read first 16 bytes: magic (4), n_vocab (4), n_audio_ctx (4), n_audio_state (4) */
    uint32_t header_buf[4];
    size_t read_bytes = fread(header_buf, sizeof(uint32_t), 4, f);
    fclose(f);

    if (read_bytes < 4) {
        if (out_header) {
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "Truncated header (could not read 16 bytes)");
        }
        return false;
    }

    uint32_t magic = header_buf[0];
    int32_t n_vocab = (int32_t)header_buf[1];
    int32_t n_audio_ctx = (int32_t)header_buf[2];
    int32_t n_audio_state = (int32_t)header_buf[3];

    if (out_header) {
        out_header->magic = magic;
        out_header->n_vocab = n_vocab;
        out_header->n_audio_ctx = n_audio_ctx;
        out_header->n_audio_state = n_audio_state;
    }

    if (magic != FLEX_GGML_FILE_MAGIC) {
        if (out_header) {
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "Invalid GGML magic (0x%08X, expected 0x67676D6C 'ggml')", (unsigned int)magic);
        }
        return false;
    }

    /* Classify capability from vocabulary */
    if (n_vocab >= 51865) {
        if (out_header) {
            out_header->capability = FLEX_WHISPER_CAP_MULTILINGUAL;
            out_header->is_valid_format = true;
            out_header->error_detail[0] = '\0';
        }
        return true;
    } else if (n_vocab >= 50000) {
        if (out_header) {
            out_header->capability = FLEX_WHISPER_CAP_ENGLISH_ONLY;
            out_header->is_valid_format = true;
            out_header->error_detail[0] = '\0';
        }
        return true;
    } else {
        if (out_header) {
            out_header->capability = FLEX_WHISPER_CAP_UNKNOWN;
            out_header->is_valid_format = false;
            snprintf(out_header->error_detail, sizeof(out_header->error_detail),
                     "Unrecognized vocabulary size (%d)", n_vocab);
        }
        return false;
    }
}

size_t flex_whisper_format_display_name(
    const char* filename,
    flex_whisper_model_type_t model_type,
    flex_whisper_capability_t capability,
    char* out_name,
    size_t cap
) {
    if (!out_name || cap < 4) return 0;

    const char* type_str = "Whisper";
    switch (model_type) {
        case FLEX_WHISPER_MODEL_TINY:   type_str = "Tiny"; break;
        case FLEX_WHISPER_MODEL_BASE:   type_str = "Base"; break;
        case FLEX_WHISPER_MODEL_SMALL:  type_str = "Small"; break;
        case FLEX_WHISPER_MODEL_MEDIUM: type_str = "Medium"; break;
        case FLEX_WHISPER_MODEL_LARGE:  type_str = "Large"; break;
        default:                        type_str = "Model"; break;
    }

    const char* cap_str = "";
    switch (capability) {
        case FLEX_WHISPER_CAP_ENGLISH_ONLY: cap_str = " (English)"; break;
        case FLEX_WHISPER_CAP_MULTILINGUAL: cap_str = " (Multilingual)"; break;
        default:                            cap_str = ""; break;
    }

    int written = 0;
    if (filename && filename[0] != '\0') {
        written = snprintf(out_name, cap, "%s%s [%s]", type_str, cap_str, filename);
    } else {
        written = snprintf(out_name, cap, "%s%s", type_str, cap_str);
    }

    if (written < 0 || (size_t)written >= cap) {
        out_name[cap - 1] = '\0';
        return cap - 1;
    }
    return (size_t)written;
}

const char* flex_whisper_capability_to_string(flex_whisper_capability_t cap) {
    switch (cap) {
        case FLEX_WHISPER_CAP_ENGLISH_ONLY: return "English-only";
        case FLEX_WHISPER_CAP_MULTILINGUAL: return "Multilingual";
        case FLEX_WHISPER_CAP_UNKNOWN:
        default:                            return "Unknown";
    }
}

bool flex_whisper_validate_model_selection(
    const char* model_path,
    char* out_diagnostic,
    size_t diag_cap
) {
    if (out_diagnostic && diag_cap > 0) {
        out_diagnostic[0] = '\0';
    }

    if (!model_path || model_path[0] == '\0') {
        /* Empty path represents auto-resolved default model preference */
        return true;
    }

    flex_whisper_model_header_t hdr;
    bool ok = flex_whisper_inspect_header(model_path, &hdr);
    if (!ok || !hdr.is_valid_format) {
        if (out_diagnostic && diag_cap > 0) {
            snprintf(out_diagnostic, diag_cap, "%s",
                     hdr.error_detail[0] ? hdr.error_detail : "Invalid or missing model file");
        }
        return false;
    }

    return true;
}
