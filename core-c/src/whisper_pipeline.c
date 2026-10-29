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

