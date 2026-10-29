#ifndef FLEX_TELEMETRY_COLLECTOR_H
#define FLEX_TELEMETRY_COLLECTOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_TELEMETRY_SAMPLE_CAP 32

typedef struct {
    uint32_t latency_samples[FLEX_TELEMETRY_SAMPLE_CAP];
    size_t sample_count;
    size_t sample_head;

    uint64_t total_requests;
    uint64_t total_tokens_generated;
    uint64_t total_inference_time_ms;
    double current_tokens_per_sec_ema;

    uint32_t min_latency_ms;
    uint32_t max_latency_ms;
    uint32_t last_latency_ms;
} flex_telemetry_t;

/**
 * Initializes telemetry metrics.
 */
void flex_telemetry_init(flex_telemetry_t* tel);

/**
 * Records a completed inference turn with token count and elapsed duration.
 */
void flex_telemetry_record_turn(
    flex_telemetry_t* tel,
    uint32_t tokens,
    uint32_t elapsed_ms
);

/**
 * Calculates median (P50) and 90th percentile (P90) latency across recent samples.
 */
void flex_telemetry_get_percentiles(
    const flex_telemetry_t* tel,
    uint32_t* out_p50,
    uint32_t* out_p90
);

/**
 * Formats telemetry summary into a JSON string for UI telemetry panel.
 */
size_t flex_telemetry_to_json(
    const flex_telemetry_t* tel,
    char* out_buf,
    size_t buf_cap
);

/**
 * Resets telemetry data.
 */
void flex_telemetry_reset(flex_telemetry_t* tel);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_TELEMETRY_COLLECTOR_H */

