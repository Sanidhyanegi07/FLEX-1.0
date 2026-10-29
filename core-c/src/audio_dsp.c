#include "audio_dsp.h"
#include <string.h>
#include <math.h>

void flex_audio_ring_init(
    flex_audio_ring_buffer_t* ring,
    float* storage,
    size_t capacity_samples
) {
    if (!ring) return;
    ring->buffer = storage;
    ring->capacity_samples = capacity_samples;
    ring->write_pos = 0;
    ring->read_pos = 0;
    ring->available_samples = 0;
}

size_t flex_audio_ring_write(
    flex_audio_ring_buffer_t* ring,
    const float* samples,
    size_t count
) {
    if (!ring || !ring->buffer || !samples || count == 0) return 0;

    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        ring->buffer[ring->write_pos] = samples[i];
        ring->write_pos = (ring->write_pos + 1) % ring->capacity_samples;

        if (ring->available_samples < ring->capacity_samples) {
            ring->available_samples++;
        } else {
            /* Overflow: advance read pointer */
            ring->read_pos = (ring->read_pos + 1) % ring->capacity_samples;
        }
        written++;
    }

    return written;
}

size_t flex_audio_ring_read(
    flex_audio_ring_buffer_t* ring,
    float* dest,
    size_t count
) {
    if (!ring || !ring->buffer || !dest || count == 0) return 0;

    size_t to_read = (count < ring->available_samples) ? count : ring->available_samples;
    for (size_t i = 0; i < to_read; ++i) {
        dest[i] = ring->buffer[ring->read_pos];
        ring->read_pos = (ring->read_pos + 1) % ring->capacity_samples;
    }

    ring->available_samples -= to_read;
    return to_read;
}

void flex_pcm16_to_float(
    const int16_t* in_pcm,
    float* out_float,
    size_t sample_count
) {
    if (!in_pcm || !out_float) return;
    const float scale = 1.0f / 32768.0f;

    /* Vectorizable clamping conversion using standard math intrinsics */
    for (size_t i = 0; i < sample_count; ++i) {
        float val = (float)in_pcm[i] * scale;
        val = fminf(fmaxf(val, -1.0f), 1.0f);
        out_float[i] = val;
    }
}

void flex_pcm24_to_float(
    const uint8_t* in_bytes,
    float* out_float,
    size_t sample_count
) {
    if (!in_bytes || !out_float) return;
    const float scale = 1.0f / 8388608.0f;

    for (size_t i = 0; i < sample_count; ++i) {
        size_t byte_idx = i * 3;
        int32_t val32 = (int32_t)(in_bytes[byte_idx] |
                                 (in_bytes[byte_idx + 1] << 8) |
                                 ((int8_t)in_bytes[byte_idx + 2] << 16));
        float val = (float)val32 * scale;
        val = fminf(fmaxf(val, -1.0f), 1.0f);
        out_float[i] = val;
    }
}

void flex_audio_downmix_to_mono(
    const float* in_interleaved,
    float* out_mono,
    size_t frame_count,
    uint32_t channel_count
) {
    if (!in_interleaved || !out_mono || frame_count == 0 || channel_count == 0) return;

    if (channel_count == 1) {
        memcpy(out_mono, in_interleaved, frame_count * sizeof(float));
        return;
    }

    float inv_channels = 1.0f / (float)channel_count;
    for (size_t i = 0; i < frame_count; ++i) {
        float sum = 0.0f;
        for (uint32_t c = 0; c < channel_count; ++c) {
            sum += in_interleaved[i * channel_count + c];
        }
        out_mono[i] = sum * inv_channels;
    }
}

size_t flex_audio_resample_linear(
    const float* in_samples,
    size_t in_count,
    uint32_t in_rate,
    float* out_samples,
    size_t out_capacity,
    uint32_t out_rate
) {
    if (!in_samples || !out_samples || in_count == 0 || in_rate == 0 || out_rate == 0 || out_capacity == 0) {
        return 0;
    }

    if (in_rate == out_rate) {
        size_t count = (in_count < out_capacity) ? in_count : out_capacity;
        memcpy(out_samples, in_samples, count * sizeof(float));
        return count;
    }

    double ratio = (double)in_rate / (double)out_rate;
    size_t estimated_out = (size_t)ceil((double)in_count / ratio);
    size_t out_count = (estimated_out < out_capacity) ? estimated_out : out_capacity;

    /* Compute safe boundary where idx1 is guaranteed < in_count to avoid inner branches */
    size_t safe_count = 0;
    if (in_count > 1) {
        double max_in_pos = (double)(in_count - 2);
        safe_count = (size_t)(max_in_pos / ratio);
        if (safe_count > out_count) safe_count = out_count;
    }

    /* Fast branchless inner loop */
    for (size_t i = 0; i < safe_count; ++i) {
        double in_pos = (double)i * ratio;
        size_t idx0 = (size_t)in_pos;
        double frac = in_pos - (double)idx0;
        out_samples[i] = (float)((1.0 - frac) * in_samples[idx0] + frac * in_samples[idx0 + 1]);
    }

    /* Boundary tail handling */
    for (size_t i = safe_count; i < out_count; ++i) {
        double in_pos = (double)i * ratio;
        size_t idx0 = (size_t)in_pos;
        size_t idx1 = idx0 + 1;
        double frac = in_pos - (double)idx0;

        if (idx1 < in_count) {
            out_samples[i] = (float)((1.0 - frac) * in_samples[idx0] + frac * in_samples[idx1]);
        } else if (idx0 < in_count) {
            out_samples[i] = in_samples[idx0];
        } else {
            out_samples[i] = 0.0f;
        }
    }

    return out_count;
}

void flex_audio_calculate_rms(
    const float* samples,
    size_t count,
    float* out_rms,
    float* out_peak
) {
    if (!samples || count == 0) {
        if (out_rms) *out_rms = 0.0f;
        if (out_peak) *out_peak = 0.0f;
        return;
    }

    /* 4-way parallel accumulators for auto-vectorization without extra fabsf on squaring */
    double sum0 = 0.0, sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;
    float peak = 0.0f;
    size_t i = 0;

    while (i + 4 <= count) {
        float v0 = samples[i];
        float v1 = samples[i + 1];
        float v2 = samples[i + 2];
        float v3 = samples[i + 3];

        sum0 += (double)v0 * (double)v0;
        sum1 += (double)v1 * (double)v1;
        sum2 += (double)v2 * (double)v2;
        sum3 += (double)v3 * (double)v3;

        float a0 = fabsf(v0);
        float a1 = fabsf(v1);
        float a2 = fabsf(v2);
        float a3 = fabsf(v3);
        if (a0 > peak) peak = a0;
        if (a1 > peak) peak = a1;
        if (a2 > peak) peak = a2;
        if (a3 > peak) peak = a3;

        i += 4;
    }

    double sum_sq = sum0 + sum1 + sum2 + sum3;
    while (i < count) {
        float val = samples[i];
        float a = fabsf(val);
        if (a > peak) peak = a;
        sum_sq += (double)val * (double)val;
        i++;
    }

    if (out_rms) {
        *out_rms = (float)sqrt(sum_sq / (double)count);
    }
    if (out_peak) {
        *out_peak = peak;
    }
}

float flex_audio_rms_to_db(float rms) {
    if (rms <= 0.00001f) return -100.0f;
    float db = 20.0f * log10f(rms);
    if (db > 0.0f) db = 0.0f;
    return db;
}

void flex_vad_init(
    flex_vad_detector_t* vad,
    float initial_noise_floor,
    float speech_threshold_ratio
) {
    if (!vad) return;
    vad->noise_floor_rms = (initial_noise_floor > 0.0001f) ? initial_noise_floor : 0.005f;
    vad->speech_threshold_rms = vad->noise_floor_rms * ((speech_threshold_ratio > 1.0f) ? speech_threshold_ratio : 3.0f);
    vad->hangover_frames = 10;
    vad->current_hangover = 0;
    vad->is_speech_active = false;
}

bool flex_vad_process_frame(
    flex_vad_detector_t* vad,
    const float* frame_samples,
    size_t frame_count
) {
    if (!vad || !frame_samples || frame_count == 0) return false;

    float rms = 0.0f;
    flex_audio_calculate_rms(frame_samples, frame_count, &rms, NULL);

    if (rms > vad->speech_threshold_rms) {
        vad->is_speech_active = true;
        vad->current_hangover = vad->hangover_frames;
    } else {
        if (vad->current_hangover > 0) {
            vad->current_hangover--;
            vad->is_speech_active = true;
        } else {
            vad->is_speech_active = false;
            /* Adapt noise floor slowly during silence */
            vad->noise_floor_rms = (vad->noise_floor_rms * 0.95f) + (rms * 0.05f);
            vad->speech_threshold_rms = vad->noise_floor_rms * 3.0f;
        }
    }

    return vad->is_speech_active;
}

