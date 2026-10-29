#include "operation_policy.h"
#include <string.h>
#include <stdio.h>

void flex_policy_init_defaults(flex_operation_policy_t* policy) {
    if (!policy) return;
    memset(policy, 0, sizeof(flex_operation_policy_t));

    policy->max_rate_per_minute = 60;      /* 1 request per second baseline */
    policy->burst_capacity = 10;           /* Allow burst of up to 10 requests */
    policy->max_payload_bytes = 1024;      /* Matches FLEX_MAX_INPUT_LEN */
    policy->max_audio_duration_sec = 30;   /* Strict 30-second cap */
    policy->chat_enabled = true;
    policy->voice_enabled = true;
    policy->memory_enabled = true;

    policy->bucket_tokens = (double)policy->burst_capacity;
    policy->last_refill_timestamp_ms = 0;
}

static void refill_bucket(flex_operation_policy_t* policy, uint64_t now_ms) {
    if (policy->last_refill_timestamp_ms == 0) {
        policy->last_refill_timestamp_ms = now_ms;
        return;
    }

    if (now_ms <= policy->last_refill_timestamp_ms) {
        return;
    }

    uint64_t elapsed_ms = now_ms - policy->last_refill_timestamp_ms;
    double refill_rate_per_ms = (double)policy->max_rate_per_minute / 60000.0;
    double added_tokens = (double)elapsed_ms * refill_rate_per_ms;

    policy->bucket_tokens += added_tokens;
    if (policy->bucket_tokens > (double)policy->burst_capacity) {
        policy->bucket_tokens = (double)policy->burst_capacity;
    }

    policy->last_refill_timestamp_ms = now_ms;
}

flex_policy_verdict_code_t flex_policy_evaluate(
    flex_operation_policy_t* policy,
    int op_type,
    size_t payload_bytes,
    uint64_t now_ms,
    flex_policy_verdict_t* out_verdict
) {
    if (!policy) return FLEX_POLICY_REJECT_OPERATION_DISABLED;

    if (out_verdict) {
        memset(out_verdict, 0, sizeof(flex_policy_verdict_t));
        out_verdict->is_allowed = false;
    }

    policy->total_requests++;

    /* 1. Operation type enabled check */
    if (op_type == 1 && !policy->chat_enabled) { /* CHAT */
        if (out_verdict) {
            out_verdict->code = FLEX_POLICY_REJECT_OPERATION_DISABLED;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Chat operations are currently disabled by policy");
        }
        policy->total_rejected++;
        return FLEX_POLICY_REJECT_OPERATION_DISABLED;
    }

    if (op_type == 2 && !policy->voice_enabled) { /* VOICE */
        if (out_verdict) {
            out_verdict->code = FLEX_POLICY_REJECT_OPERATION_DISABLED;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Voice operations are currently disabled by policy");
        }
        policy->total_rejected++;
        return FLEX_POLICY_REJECT_OPERATION_DISABLED;
    }

    if (op_type == 3 && !policy->memory_enabled) { /* MEMORY */
        if (out_verdict) {
            out_verdict->code = FLEX_POLICY_REJECT_OPERATION_DISABLED;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Memory query operations are currently disabled by policy");
        }
        policy->total_rejected++;
        return FLEX_POLICY_REJECT_OPERATION_DISABLED;
    }

    /* 2. Payload size check */
    if (payload_bytes > policy->max_payload_bytes) {
        if (out_verdict) {
            out_verdict->code = FLEX_POLICY_REJECT_PAYLOAD_TOO_LARGE;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason),
                     "Payload size (%zu bytes) exceeds configured policy limit (%zu bytes)",
                     payload_bytes, policy->max_payload_bytes);
        }
        policy->total_rejected++;
        return FLEX_POLICY_REJECT_PAYLOAD_TOO_LARGE;
    }

    /* 3. Rate limiting token bucket check */
    refill_bucket(policy, now_ms);

    if (policy->bucket_tokens < 1.0) {
        double refill_rate_per_sec = (double)policy->max_rate_per_minute / 60.0;
        uint32_t wait_sec = (refill_rate_per_sec > 0.0) ? (uint32_t)((1.0 - policy->bucket_tokens) / refill_rate_per_sec + 1.0) : 1;

        if (out_verdict) {
            out_verdict->code = FLEX_POLICY_REJECT_RATE_LIMIT;
            out_verdict->retry_after_sec = wait_sec;
            out_verdict->current_bucket_tokens = (uint32_t)policy->bucket_tokens;
            snprintf(out_verdict->reason, sizeof(out_verdict->reason),
                     "Rate limit exceeded. Bucket empty. Retry in %u seconds", wait_sec);
        }
        policy->total_rejected++;
        return FLEX_POLICY_REJECT_RATE_LIMIT;
    }

    /* Deduct 1 token for allowed request */
    policy->bucket_tokens -= 1.0;
    policy->total_allowed++;

    if (out_verdict) {
        out_verdict->code = FLEX_POLICY_ALLOW;
        out_verdict->is_allowed = true;
        out_verdict->current_bucket_tokens = (uint32_t)policy->bucket_tokens;
        snprintf(out_verdict->reason, sizeof(out_verdict->reason), "Request approved by operation policy");
    }

    return FLEX_POLICY_ALLOW;
}

void flex_policy_record_usage(
    flex_operation_policy_t* policy,
    size_t tokens_consumed
) {
    if (!policy) return;
    policy->total_tokens_consumed += tokens_consumed;
}

void flex_policy_set_enabled(
    flex_operation_policy_t* policy,
    int op_type,
    bool enabled
) {
    if (!policy) return;
    if (op_type == 1) policy->chat_enabled = enabled;
    else if (op_type == 2) policy->voice_enabled = enabled;
    else if (op_type == 3) policy->memory_enabled = enabled;
}

void flex_policy_reset(flex_operation_policy_t* policy) {
    if (!policy) return;
    policy->bucket_tokens = (double)policy->burst_capacity;
    policy->last_refill_timestamp_ms = 0;
    policy->total_requests = 0;
    policy->total_allowed = 0;
    policy->total_rejected = 0;
    policy->total_tokens_consumed = 0;
}

