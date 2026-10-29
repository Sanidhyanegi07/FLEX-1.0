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

