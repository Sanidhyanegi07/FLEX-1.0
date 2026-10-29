#include "telemetry_collector.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void flex_telemetry_init(flex_telemetry_t* tel) {
    if (!tel) return;
    memset(tel, 0, sizeof(flex_telemetry_t));
    tel->min_latency_ms = 0xFFFFFFFF;
}

void flex_telemetry_record_turn(
    flex_telemetry_t* tel,
    uint32_t tokens,
    uint32_t elapsed_ms
) {
    if (!tel) return;

    tel->total_requests++;
    tel->total_tokens_generated += tokens;
    tel->total_inference_time_ms += elapsed_ms;
    tel->last_latency_ms = elapsed_ms;

    if (elapsed_ms < tel->min_latency_ms) {
        tel->min_latency_ms = elapsed_ms;
    }
    if (elapsed_ms > tel->max_latency_ms) {
        tel->max_latency_ms = elapsed_ms;
    }

    /* Update EMA for tokens per second */
    if (elapsed_ms > 0 && tokens > 0) {
        double instant_tps = ((double)tokens / (double)elapsed_ms) * 1000.0;
        if (tel->current_tokens_per_sec_ema == 0.0) {
            tel->current_tokens_per_sec_ema = instant_tps;
        } else {
            /* Alpha = 0.2 */
            tel->current_tokens_per_sec_ema = (0.2 * instant_tps) + (0.8 * tel->current_tokens_per_sec_ema);
        }
    }

    /* Add sample to ring */
    tel->latency_samples[tel->sample_head] = elapsed_ms;
    tel->sample_head = (tel->sample_head + 1) % FLEX_TELEMETRY_SAMPLE_CAP;
    if (tel->sample_count < FLEX_TELEMETRY_SAMPLE_CAP) {
        tel->sample_count++;
    }
}

static int compare_uint32(const void* a, const void* b) {
    uint32_t u1 = *(const uint32_t*)a;
    uint32_t u2 = *(const uint32_t*)b;
    if (u1 < u2) return -1;
    if (u1 > u2) return 1;
    return 0;
}

void flex_telemetry_get_percentiles(
    const flex_telemetry_t* tel,
    uint32_t* out_p50,
    uint32_t* out_p90
) {
    if (!tel || tel->sample_count == 0) {
        if (out_p50) *out_p50 = 0;
        if (out_p90) *out_p90 = 0;
        return;
    }

    uint32_t sorted[FLEX_TELEMETRY_SAMPLE_CAP];
    memcpy(sorted, tel->latency_samples, tel->sample_count * sizeof(uint32_t));
    qsort(sorted, tel->sample_count, sizeof(uint32_t), compare_uint32);

    if (out_p50) {
        size_t idx50 = tel->sample_count / 2;
        *out_p50 = sorted[idx50];
    }

    if (out_p90) {
        size_t idx90 = (tel->sample_count * 9) / 10;
        if (idx90 >= tel->sample_count) idx90 = tel->sample_count - 1;
        *out_p90 = sorted[idx90];
    }
}

size_t flex_telemetry_to_json(
    const flex_telemetry_t* tel,
    char* out_buf,
    size_t buf_cap
) {
    if (!tel || !out_buf || buf_cap < 32) return 0;

    uint32_t p50 = 0, p90 = 0;
    flex_telemetry_get_percentiles(tel, &p50, &p90);

    uint32_t min_lat = (tel->min_latency_ms == 0xFFFFFFFF) ? 0 : tel->min_latency_ms;

    int written = snprintf(
        out_buf, buf_cap,
        "{\"requests\":%llu,\"tokens\":%llu,\"tps_ema\":%.2f,\"last_latency_ms\":%u,\"p50_latency_ms\":%u,\"p90_latency_ms\":%u,\"min_ms\":%u,\"max_ms\":%u}",
        (unsigned long long)tel->total_requests,
        (unsigned long long)tel->total_tokens_generated,
        tel->current_tokens_per_sec_ema,
        tel->last_latency_ms,
        p50, p90, min_lat,
        tel->max_latency_ms
    );

    if (written < 0 || (size_t)written >= buf_cap) {
        out_buf[buf_cap - 1] = '\0';
        return buf_cap - 1;
    }
    return (size_t)written;
}

void flex_telemetry_reset(flex_telemetry_t* tel) {
    if (!tel) return;
    memset(tel, 0, sizeof(flex_telemetry_t));
    tel->min_latency_ms = 0xFFFFFFFF;
}

