#include "context_compressor.h"
#include "string_util.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static bool is_common_stopword(const char* word) {
    static const char* const S_STOPWORDS[] = {
        "the", "is", "at", "which", "on", "a", "an", "and", "or", "in", "to",
        "of", "for", "with", "as", "by", "that", "this", "it", "from", "be",
        "are", "was", "were", "what", "how", "can", "you", "i", "me", "my",
        NULL
    };

    for (size_t i = 0; S_STOPWORDS[i] != NULL; ++i) {
        if (strcmp(word, S_STOPWORDS[i]) == 0) return true;
    }
    return false;
}

size_t flex_extract_key_terms(
    const char* text,
    char out_terms[][32],
    size_t max_terms
) {
    if (!text || !out_terms || max_terms == 0) return 0;

    char copy[512];
    snprintf(copy, sizeof(copy), "%.511s", text);

    size_t term_count = 0;
    char* token = strtok(copy, " \t\r\n.,?!;:\'\"()[]{}");

    while (token && term_count < max_terms) {
        size_t len = strlen(token);
        if (len >= 3) {
            char lower[32];
            flex_str_tolower(token, lower, sizeof(lower));

            if (!is_common_stopword(lower)) {
                /* Check duplicate */
                bool duplicate = false;
                for (size_t k = 0; k < term_count; ++k) {
                    if (strcmp(out_terms[k], lower) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    snprintf(out_terms[term_count++], 32, "%s", lower);
                }
            }
        }
        token = strtok(NULL, " \t\r\n.,?!;:\'\"()[]{}");
    }

    return term_count;
}

bool flex_compress_conversation_context(
    const flex_raw_turn_t* turns,
    size_t turn_count,
    size_t recent_turns_to_keep,
    size_t target_token_limit,
    flex_compression_result_t* out_result
) {
    if (!turns || turn_count == 0 || !out_result) return false;
    memset(out_result, 0, sizeof(flex_compression_result_t));

    size_t orig_tokens = 0;
    for (size_t i = 0; i < turn_count; ++i) {
        orig_tokens += turns[i].token_count;
    }
    out_result->original_tokens = orig_tokens;

    if (turn_count <= recent_turns_to_keep || orig_tokens <= target_token_limit) {
        out_result->compressed_tokens = orig_tokens;
        out_result->compression_ratio = 1.0f;
        out_result->turns_retained_verbatim = turn_count;
        out_result->turns_summarized = 0;
        return true;
    }

    /* Split: 0 is system (if present), [1 .. turn_count - recent_turns_to_keep] are summarized */
    size_t sum_start = 0;
    if (strcmp(turns[0].role, "system") == 0) {
        sum_start = 1;
    }

    size_t sum_end = turn_count - recent_turns_to_keep;
    if (sum_end <= sum_start) {
        sum_end = sum_start + 1;
    }

    char summary[FLEX_COMPRESS_SUMMARY_MAX];
    size_t s_idx = 0;
    s_idx += snprintf(summary + s_idx, sizeof(summary) - s_idx, "[Conversation Summary: Earlier discussion covered topics: ");

    char terms[16][32];
    size_t total_terms = 0;

    for (size_t i = sum_start; i < sum_end && i < turn_count; ++i) {
        char turn_terms[8][32];
        size_t n = flex_extract_key_terms(turns[i].text, turn_terms, 8);
        for (size_t k = 0; k < n && total_terms < 16; ++k) {
            bool dup = false;
            for (size_t d = 0; d < total_terms; ++d) {
                if (strcmp(terms[d], turn_terms[k]) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                snprintf(terms[total_terms++], 32, "%s", turn_terms[k]);
            }
        }
    }

    for (size_t k = 0; k < total_terms && s_idx + 35 < sizeof(summary); ++k) {
        s_idx += snprintf(summary + s_idx, sizeof(summary) - s_idx, "%s%s", terms[k], (k + 1 < total_terms) ? ", " : ".");
    }
    s_idx += snprintf(summary + s_idx, sizeof(summary) - s_idx, "]");

    snprintf(out_result->summary_block, sizeof(out_result->summary_block), "%s", summary);
    out_result->turns_summarized = sum_end - sum_start;
    out_result->turns_retained_verbatim = turn_count - out_result->turns_summarized;

    size_t summary_tokens = s_idx / 4 + 1;
    size_t recent_tokens = 0;
    for (size_t i = sum_end; i < turn_count; ++i) {
        recent_tokens += turns[i].token_count;
    }
    if (sum_start == 1) {
        recent_tokens += turns[0].token_count; /* include system prompt */
    }

    out_result->compressed_tokens = recent_tokens + summary_tokens;
    out_result->compression_ratio = (float)out_result->compressed_tokens / (float)(orig_tokens > 0 ? orig_tokens : 1);

    return true;
}

