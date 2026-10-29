#ifndef FLEX_OPERATION_POLICY_H
#define FLEX_OPERATION_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_POLICY_ALLOW = 0,
    FLEX_POLICY_REJECT_RATE_LIMIT = 1,
    FLEX_POLICY_REJECT_BURST_EXCEEDED = 2,
    FLEX_POLICY_REJECT_PAYLOAD_TOO_LARGE = 3,
    FLEX_POLICY_REJECT_OPERATION_DISABLED = 4,
    FLEX_POLICY_REJECT_QUOTA_EXCEEDED = 5
} flex_policy_verdict_code_t;

typedef struct {
    flex_policy_verdict_code_t code;
    bool is_allowed;
    uint32_t retry_after_sec;
    uint32_t current_bucket_tokens;
    char reason[128];
} flex_policy_verdict_t;

typedef struct {
    uint32_t max_rate_per_minute;
    uint32_t burst_capacity;
    size_t max_payload_bytes;
    uint32_t max_audio_duration_sec;
    bool chat_enabled;
    bool voice_enabled;
    bool memory_enabled;

    /* Runtime token bucket state */
    double bucket_tokens;
    uint64_t last_refill_timestamp_ms;

    /* Metrics */
    uint64_t total_requests;
    uint64_t total_allowed;
    uint64_t total_rejected;
    uint64_t total_tokens_consumed;
} flex_operation_policy_t;

/**
 * Initializes policy with default thresholds.
 */
void flex_policy_init_defaults(flex_operation_policy_t* policy);

/**
 * Evaluates an incoming request against policy rules and token bucket rate limits.
 */
flex_policy_verdict_code_t flex_policy_evaluate(
    flex_operation_policy_t* policy,
    int op_type,
    size_t payload_bytes,
    uint64_t now_ms,
    flex_policy_verdict_t* out_verdict
);

/**
 * Records completed operation usage.
 */
void flex_policy_record_usage(
    flex_operation_policy_t* policy,
    size_t tokens_consumed
);

/**
 * Manually enables or disables an operation type.
 */
void flex_policy_set_enabled(
    flex_operation_policy_t* policy,
    int op_type,
    bool enabled
);

/**
 * Resets token bucket and counters.
 */
void flex_policy_reset(flex_operation_policy_t* policy);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_OPERATION_POLICY_H */

