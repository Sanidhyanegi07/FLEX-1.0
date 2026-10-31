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

        if (isnan(v0) || isinf(v0)) v0 = 0.0f;
        if (isnan(v1) || isinf(v1)) v1 = 0.0f;
        if (isnan(v2) || isinf(v2)) v2 = 0.0f;
        if (isnan(v3) || isinf(v3)) v3 = 0.0f;

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
        if (isnan(val) || isinf(val)) val = 0.0f;
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void flex_audio_analyze_stereo(
    const float* in_interleaved,
    size_t frame_count,
    flex_stereo_analysis_t* out_analysis
) {
    if (!out_analysis) return;
    memset(out_analysis, 0, sizeof(flex_stereo_analysis_t));
    out_analysis->downmix_mode = 0;
    out_analysis->correlation = 1.0f;

    if (!in_interleaved || frame_count == 0) {
        return;
    }

    double sum_sq0 = 0.0, sum_sq1 = 0.0, sum_prod = 0.0;
    float peak0 = 0.0f, peak1 = 0.0f;

    for (size_t i = 0; i < frame_count; ++i) {
        float l = in_interleaved[i * 2];
        float r = in_interleaved[i * 2 + 1];

        if (isnan(l) || isinf(l)) l = 0.0f;
        if (isnan(r) || isinf(r)) r = 0.0f;

        float abs_l = fabsf(l);
        float abs_r = fabsf(r);
        if (abs_l > peak0) peak0 = abs_l;
        if (abs_r > peak1) peak1 = abs_r;

        sum_sq0 += (double)l * (double)l;
        sum_sq1 += (double)r * (double)r;
        sum_prod += (double)l * (double)r;
    }

    float rms0 = (float)sqrt(sum_sq0 / (double)frame_count);
    float rms1 = (float)sqrt(sum_sq1 / (double)frame_count);

    double denom = sqrt(sum_sq0 * sum_sq1);
    float corr = (denom > 1e-12) ? (float)(sum_prod / denom) : 1.0f;

    if (corr > 1.0f) corr = 1.0f;
    if (corr < -1.0f) corr = -1.0f;

    out_analysis->ch0_rms = rms0;
    out_analysis->ch1_rms = rms1;
    out_analysis->ch0_peak = peak0;
    out_analysis->ch1_peak = peak1;
    out_analysis->correlation = corr;

    /* Decision logic:
     * Mode 0 = balanced sum: (ch0 + ch1) * 0.5f
     * Mode 1 = ch0 only (ch1 dead, or anti-phase where ch0 >= ch1 or tie)
     * Mode 2 = ch1 only (ch0 dead, or anti-phase where ch1 > ch0)
     */

    /* Case 1: Both channels silent / near-zero (< -80 dBFS) */
    if (rms0 < 0.0001f && rms1 < 0.0001f) {
        out_analysis->downmix_mode = 0; /* balanced average of silence */
        return;
    }

    /* Case 2: Dead-channel detection with relative energy criteria:
     * Active channel must have meaningful energy (>= 0.001f, -60 dBFS).
     * Dead channel is either below electronic noise floor (< 0.0002f, -74 dBFS)
     * OR has >= 20 dB lower energy (ratio >= 10.0f) while below ambient room noise (< 0.002f).
     */
    bool ch1_dead = (rms0 >= 0.001f) &&
                    ((rms1 < 0.0002f && rms0 >= (5.0f * rms1)) ||
                     (rms1 < 0.002f && rms0 >= (10.0f * rms1)));

    bool ch0_dead = (rms1 >= 0.001f) &&
                    ((rms0 < 0.0002f && rms1 >= (5.0f * rms0)) ||
                     (rms0 < 0.002f && rms1 >= (10.0f * rms0)));

    if (ch0_dead && !ch1_dead) {
        out_analysis->downmix_mode = 2; /* ch1 only */
        return;
    }
    if (ch1_dead && !ch0_dead) {
        out_analysis->downmix_mode = 1; /* ch0 only */
        return;
    }

    /* Case 3: Anti-phase stereo cancellation avoidance (correlation < -0.5f).
     * To prevent destructive phase cancellation, select the stronger channel.
     * If one channel has > 5% higher RMS energy, select that channel.
     * If channel levels are approximately equal (tie within 5%), deterministically
     * select Channel 0 (primary / Left channel).
     * Note: Comparing RMS selects the channel with higher received amplitude;
     * true acoustic SNR cannot be determined without a calibrated noise model.
     */
    if (corr < -0.5f) {
        if (rms1 > rms0 * 1.05f) {
            out_analysis->downmix_mode = 2; /* ch1 stronger */
        } else {
            out_analysis->downmix_mode = 1; /* ch0 stronger or deterministic tie */
        }
        return;
    }

    /* Case 4: Normal stereo with both channels active and reasonable phase alignment */
    out_analysis->downmix_mode = 0; /* balanced average */
}

void flex_audio_downmix_to_mono_safe(
    const float* in_interleaved,
    float* out_mono,
    size_t frame_count,
    uint32_t channel_count,
    const flex_stereo_analysis_t* analysis_hint
) {
    if (!in_interleaved || !out_mono || frame_count == 0 || channel_count == 0) return;

    if (channel_count == 1) {
        for (size_t i = 0; i < frame_count; ++i) {
            float val = in_interleaved[i];
            if (isnan(val) || isinf(val)) val = 0.0f;
            out_mono[i] = val;
        }
        return;
    }

    if (channel_count == 2) {
        int mode = analysis_hint ? analysis_hint->downmix_mode : 0;
        if (mode == 1) {
            /* Left channel only */
            for (size_t i = 0; i < frame_count; ++i) {
                float val = in_interleaved[i * 2];
                if (isnan(val) || isinf(val)) val = 0.0f;
                out_mono[i] = val;
            }
            return;
        } else if (mode == 2) {
            /* Right channel only */
            for (size_t i = 0; i < frame_count; ++i) {
                float val = in_interleaved[i * 2 + 1];
                if (isnan(val) || isinf(val)) val = 0.0f;
                out_mono[i] = val;
            }
            return;
        } else {
            /* Balanced sum */
            for (size_t i = 0; i < frame_count; ++i) {
                float l = in_interleaved[i * 2];
                float r = in_interleaved[i * 2 + 1];
                if (isnan(l) || isinf(l)) l = 0.0f;
                if (isnan(r) || isinf(r)) r = 0.0f;
                out_mono[i] = (l + r) * 0.5f;
            }
            return;
        }
    }

    /* > 2 channels: uniform average */
    float inv_ch = 1.0f / (float)channel_count;
    for (size_t i = 0; i < frame_count; ++i) {
        float sum = 0.0f;
        for (uint32_t c = 0; c < channel_count; ++c) {
            float val = in_interleaved[i * channel_count + c];
            if (isnan(val) || isinf(val)) val = 0.0f;
            sum += val;
        }
        out_mono[i] = sum * inv_ch;
    }
}

static inline double flex_sinc_pi(double x) {
    if (fabs(x) < 1e-9) return 1.0;
    double px = M_PI * x;
    return sin(px) / px;
}

static inline double flex_blackman_win(double x, double w) {
    if (fabs(x) >= w) return 0.0;
    double u = (x + w) / (2.0 * w);
    return 0.42 - 0.50 * cos(2.0 * M_PI * u) + 0.08 * cos(4.0 * M_PI * u);
}

size_t flex_audio_resample_bandlimited(
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

    double step = (double)in_rate / (double)out_rate;
    size_t estimated_out = (size_t)ceil((double)in_count / step);
    size_t out_count = (estimated_out < out_capacity) ? estimated_out : out_capacity;

    /* Determine anti-aliasing cutoff frequency:
     * When downsampling (in_rate > out_rate):
     * The Nyquist frequency of the output is out_rate / 2.
     * Normalized to the input sample rate, this is (out_rate / in_rate) * 0.5.
     * To provide transition band margin and deep stopband rejection (>40 dB)
     * at and above the Nyquist limit, we set cutoff factor to 0.45 * (out_rate / in_rate).
     * For 48 kHz -> 16 kHz: cutoff = (16000/48000) * 0.45 = 0.150 (7.2 kHz).
     * For 44.1 kHz -> 16 kHz: cutoff = (16000/44100) * 0.45 = 0.1632 (7.2 kHz).
     */
    double cutoff;
    double filter_radius;
    if (in_rate > out_rate) {
        cutoff = ((double)out_rate / (double)in_rate) * 0.45;
        filter_radius = 12.0 / (2.0 * cutoff);
        if (filter_radius < 12.0) filter_radius = 12.0;
        if (filter_radius > 64.0) filter_radius = 64.0;
    } else {
        cutoff = 0.45;
        filter_radius = 12.0;
    }

    for (size_t i = 0; i < out_count; ++i) {
        double center_t = (double)i * step;
        ptrdiff_t n_start = (ptrdiff_t)ceil(center_t - filter_radius);
        ptrdiff_t n_end = (ptrdiff_t)floor(center_t + filter_radius);

        if (n_start < 0) n_start = 0;
        if ((size_t)n_end >= in_count) n_end = (ptrdiff_t)(in_count - 1);

        double acc = 0.0;
        double weight_sum = 0.0;

        for (ptrdiff_t n = n_start; n <= n_end; ++n) {
            double diff = (double)n - center_t;
            double sinc_val = flex_sinc_pi(2.0 * cutoff * diff);
            double win_val = flex_blackman_win(diff, filter_radius);
            double h = sinc_val * win_val;

            acc += (double)in_samples[n] * h;
            weight_sum += h;
        }

        if (fabs(weight_sum) > 1e-9) {
            acc /= weight_sum;
        }

        if (isnan(acc) || isinf(acc)) {
            acc = 0.0;
        } else if (acc > 1.0) {
            acc = 1.0;
        } else if (acc < -1.0) {
            acc = -1.0;
        }
        out_samples[i] = (float)acc;
    }

    return out_count;
}

flex_audio_norm_result_t flex_audio_normalize_speech(
    float* samples,
    size_t count,
    float target_peak,
    float max_gain,
    float noise_gate_rms
) {
    flex_audio_norm_result_t res;
    memset(&res, 0, sizeof(res));
    res.gain_applied = 1.0f;

    if (!samples || count == 0) {
        return res;
    }

    float peak_before = 0.0f, rms_before = 0.0f;
    flex_audio_calculate_rms(samples, count, &rms_before, &peak_before);
    res.peak_before = peak_before;
    res.rms_before = rms_before;
    res.peak_after = peak_before;
    res.rms_after = rms_before;

    /* Noise gate check: do not amplify pure silence or near-silence room noise */
    if (peak_before < 0.005f || rms_before < noise_gate_rms) {
        res.is_silence = true;
        res.was_normalized = false;
        res.gain_applied = 1.0f;
        for (size_t i = 0; i < count; ++i) {
            float val = samples[i];
            if (isnan(val) || isinf(val)) {
                val = 0.0f;
            } else if (val > 1.0f) {
                val = 1.0f;
            } else if (val < -1.0f) {
                val = -1.0f;
            }
            samples[i] = val;
        }
        return res;
    }

    float desired_gain = 1.0f;
    if (peak_before < 0.50f) {
        desired_gain = target_peak / peak_before;
        if (desired_gain > max_gain) {
            desired_gain = max_gain;
        }
        if (rms_before > 0.0f && (desired_gain * rms_before) > 0.60f) {
            desired_gain = 0.60f / rms_before;
        }
        if (desired_gain < 1.0f) {
            desired_gain = 1.0f;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        float val = samples[i];
        if (isnan(val) || isinf(val)) {
            val = 0.0f;
        } else {
            val *= desired_gain;
            if (val > 1.0f) val = 1.0f;
            if (val < -1.0f) val = -1.0f;
        }
        samples[i] = val;
    }

    float peak_after = 0.0f, rms_after = 0.0f;
    flex_audio_calculate_rms(samples, count, &rms_after, &peak_after);
    res.peak_after = peak_after;
    res.rms_after = rms_after;
    res.gain_applied = desired_gain;
    res.was_normalized = (desired_gain > 1.01f);
    res.is_silence = false;

    return res;
}

