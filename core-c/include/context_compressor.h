#ifndef FLEX_CONTEXT_COMPRESSOR_H
#define FLEX_CONTEXT_COMPRESSOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_MAX_COMPRESS_TURNS 32
#define FLEX_COMPRESS_SUMMARY_MAX 1024

typedef struct {
    char role[16];
    char text[512];
    size_t token_count;
} flex_raw_turn_t;

typedef struct {
    size_t original_tokens;
    size_t compressed_tokens;
    float compression_ratio;
    size_t turns_retained_verbatim;
    size_t turns_summarized;
    char summary_block[FLEX_COMPRESS_SUMMARY_MAX];
} flex_compression_result_t;

/**
 * Compresses an array of conversation turns into a bounded context window:
 * Preserves the system prompt, condenses older intermediate turns into a summary block,
 * and keeps the most recent recent_turns_to_keep turns verbatim.
 */
bool flex_compress_conversation_context(
    const flex_raw_turn_t* turns,
    size_t turn_count,
    size_t recent_turns_to_keep,
    size_t target_token_limit,
    flex_compression_result_t* out_result
);

/**
 * Extracts key topic nouns and tokens from a text string.
 */
size_t flex_extract_key_terms(
    const char* text,
    char out_terms[][32],
    size_t max_terms
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_CONTEXT_COMPRESSOR_H */

