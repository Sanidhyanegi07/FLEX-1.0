#ifndef FLEX_CONFIG_VALIDATOR_H
#define FLEX_CONFIG_VALIDATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_CFG_VALID = 0,
    FLEX_CFG_ERR_NULL = 1,
    FLEX_CFG_ERR_INVALID_HOST = 2,
    FLEX_CFG_ERR_INVALID_PORT = 3,
    FLEX_CFG_ERR_PRIVILEGED_PORT = 4,
    FLEX_CFG_ERR_INVALID_MODEL = 5,
    FLEX_CFG_ERR_INVALID_WHISPER_PATH = 6,
    FLEX_CFG_ERR_RATE_OUT_OF_RANGE = 7,
    FLEX_CFG_ERR_VOLUME_OUT_OF_RANGE = 8,
    FLEX_CFG_ERR_UNSUPPORTED_SAMPLE_RATE = 9,
    FLEX_CFG_ERR_INVALID_CHANNELS = 10,
    FLEX_CFG_ERR_INVALID_URL = 11,
    FLEX_CFG_ERR_INVALID_TEMPERATURE = 12,
    FLEX_CFG_ERR_INVALID_NUM_CTX = 13,
    FLEX_CFG_ERR_INVALID_TIMEOUT = 14
} flex_cfg_status_t;

typedef struct {
    flex_cfg_status_t status;
    char error_detail[128];
} flex_cfg_result_t;

/**
 * Validates network host string (IPv4 or hostname/localhost).
 */
flex_cfg_status_t flex_validate_host(
    const char* host,
    flex_cfg_result_t* out_result
);

/**
 * Validates network port (1-65535).
 */
flex_cfg_status_t flex_validate_port(
    int port,
    bool allow_privileged,
    flex_cfg_result_t* out_result
);

/**
 * Validates Ollama model name string ([namespace/]name[:tag]).
 */
flex_cfg_status_t flex_validate_model_name(
    const char* model_name,
    flex_cfg_result_t* out_result
);

/**
 * Validates whisper model file path (non-empty, ends with .bin).
 */
flex_cfg_status_t flex_validate_whisper_path(
    const char* path,
    flex_cfg_result_t* out_result
);

/**
 * Validates SAPI TTS speech rate (-10 to +10).
 */
flex_cfg_status_t flex_validate_tts_rate(
    int rate,
    flex_cfg_result_t* out_result
);

/**
 * Validates SAPI TTS volume (0 to 100).
 */
flex_cfg_status_t flex_validate_tts_volume(
    int volume,
    flex_cfg_result_t* out_result
);

/**
 * Validates audio sample rate (e.g. 16000, 44100, 48000).
 */
flex_cfg_status_t flex_validate_audio_sample_rate(
    uint32_t sample_rate,
    flex_cfg_result_t* out_result
);

/**
 * Validates audio channel count (1 to 8).
 */
flex_cfg_status_t flex_validate_audio_channels(
    uint32_t channels,
    flex_cfg_result_t* out_result
);

/**
 * Validates endpoint URL (must be valid http:// or https:// with host and port).
 */
flex_cfg_status_t flex_validate_endpoint_url(
    const char* url,
    flex_cfg_result_t* out_result
);

/**
 * Validates inference temperature (0.0 to 2.0).
 */
flex_cfg_status_t flex_validate_temperature(
    double temperature,
    flex_cfg_result_t* out_result
);

/**
 * Validates context window token limit (256 to 131072).
 */
flex_cfg_status_t flex_validate_context_window(
    int num_ctx,
    flex_cfg_result_t* out_result
);

/**
 * Validates inference timeout in seconds (1 to 600).
 */
flex_cfg_status_t flex_validate_timeout(
    int timeout_seconds,
    flex_cfg_result_t* out_result
);

/**
 * Converts status code to string.
 */
const char* flex_cfg_status_to_string(flex_cfg_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_CONFIG_VALIDATOR_H */

