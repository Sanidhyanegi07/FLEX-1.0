#ifndef FLEX_AUDIO_DSP_H
#define FLEX_AUDIO_DSP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_AUDIO_TARGET_SAMPLE_RATE 16000
#define FLEX_AUDIO_MAX_CHANNELS 8

typedef struct {
    float* buffer;
    size_t capacity_samples;
    size_t write_pos;
    size_t read_pos;
    size_t available_samples;
} flex_audio_ring_buffer_t;

typedef struct {
    float noise_floor_rms;
    float speech_threshold_rms;
    uint32_t hangover_frames;
    uint32_t current_hangover;
    bool is_speech_active;
} flex_vad_detector_t;

/**
 * Initializes a ring buffer with external storage pointer.
 */
void flex_audio_ring_init(
    flex_audio_ring_buffer_t* ring,
    float* storage,
    size_t capacity_samples
);

/**
 * Writes samples into the ring buffer. Overwrites oldest if overflow.
 */
size_t flex_audio_ring_write(
    flex_audio_ring_buffer_t* ring,
    const float* samples,
    size_t count
);

/**
 * Reads samples from the ring buffer.
 */
size_t flex_audio_ring_read(
    flex_audio_ring_buffer_t* ring,
    float* dest,
    size_t count
);

/**
 * Converts 16-bit signed PCM to 32-bit float normalized to [-1.0, 1.0].
 */
void flex_pcm16_to_float(
    const int16_t* in_pcm,
    float* out_float,
    size_t sample_count
);

/**
 * Converts 24-bit packed PCM (3 bytes per sample) to 32-bit float.
 */
void flex_pcm24_to_float(
    const uint8_t* in_bytes,
    float* out_float,
    size_t sample_count
);

/**
 * Downmixes multi-channel interleaved float samples to mono.
 */
void flex_audio_downmix_to_mono(
    const float* in_interleaved,
    float* out_mono,
    size_t frame_count,
    uint32_t channel_count
);

typedef struct {
    float correlation;       /* Pearson correlation between channel 0 and 1 (-1.0 to +1.0) */
    float ch0_rms;
    float ch1_rms;
    float ch0_peak;
    float ch1_peak;
    int downmix_mode;        /* 0 = balanced average (ch0+ch1)*0.5f,
                              * 1 = ch0 only (ch1 dead, or anti-phase where ch0 >= ch1 or tie),
                              * 2 = ch1 only (ch0 dead, or anti-phase where ch1 > ch0) */
} flex_stereo_analysis_t;

typedef struct {
    float peak_before;
    float rms_before;
    float peak_after;
    float rms_after;
    float gain_applied;
    bool was_normalized;
    bool is_silence;
} flex_audio_norm_result_t;

/**
 * Analyzes stereo channel energies, peaks, and cross-correlation to detect
 * phase inversion or dead channels.
 *
 * Downmix mode decision:
 * - Mode 0 (Balanced sum): Both channels active and in reasonable phase (corr >= -0.5f),
 *   or both channels silent (< 0.0001f RMS).
 * - Mode 1 (Ch0 only): Ch1 dead (rms0 >= 0.001f while ch1 is silent or >=20 dB quieter
 *   under noise floor), OR anti-phase (corr < -0.5f) where Ch0 is stronger or tied.
 * - Mode 2 (Ch1 only): Ch0 dead (rms1 >= 0.001f while ch0 is silent or >=20 dB quieter
 *   under noise floor), OR anti-phase (corr < -0.5f) where Ch1 is measurably stronger (>5%).
 *
 * Note: When anti-phase occurs, comparing RMS levels selects the channel with higher received
 * amplitude, which is an effective heuristic for asymmetrical mic positioning. True acoustic
 * signal-to-noise ratio cannot be proven without an explicit calibrated noise reference.
 */
void flex_audio_analyze_stereo(
    const float* in_interleaved,
    size_t frame_count,
    flex_stereo_analysis_t* out_analysis
);

/**
 * Downmixes multi-channel interleaved float samples to mono using the analyzed
 * channel balance and correlation policy to avoid destructive phase cancellation.
 */
void flex_audio_downmix_to_mono_safe(
    const float* in_interleaved,
    float* out_mono,
    size_t frame_count,
    uint32_t channel_count,
    const flex_stereo_analysis_t* analysis_hint
);

/**
 * Bandlimited windowed-sinc resampler with anti-aliasing low-pass filtering.
 * Attenuates frequencies above target Nyquist limit (e.g. 8 kHz for 16 kHz output).
 */
size_t flex_audio_resample_bandlimited(
    const float* in_samples,
    size_t in_count,
    uint32_t in_rate,
    float* out_samples,
    size_t out_capacity,
    uint32_t out_rate
);

/**
 * Applies conservative peak/RMS normalization for quiet speech recordings while
 * preserving noise gate to prevent boosting background ambient noise or silence.
 */
flex_audio_norm_result_t flex_audio_normalize_speech(
    float* samples,
    size_t count,
    float target_peak,
    float max_gain,
    float noise_gate_rms
);

/**
 * High-quality linear interpolation resampler to 16 kHz Mono.
 */
size_t flex_audio_resample_linear(
    const float* in_samples,
    size_t in_count,
    uint32_t in_rate,
    float* out_samples,
    size_t out_capacity,
    uint32_t out_rate
);

/**
 * Calculates RMS and peak amplitude.
 */
void flex_audio_calculate_rms(
    const float* samples,
    size_t count,
    float* out_rms,
    float* out_peak
);

/**
 * Converts RMS amplitude to decibels (dBFS, 0 dB is full scale).
 */
float flex_audio_rms_to_db(float rms);

/**
 * Initializes energy-based Voice Activity Detector.
 */
void flex_vad_init(
    flex_vad_detector_t* vad,
    float initial_noise_floor,
    float speech_threshold_ratio
);

/**
 * Processes an audio frame through VAD. Returns true if speech active.
 */
bool flex_vad_process_frame(
    flex_vad_detector_t* vad,
    const float* frame_samples,
    size_t frame_count
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_AUDIO_DSP_H */

