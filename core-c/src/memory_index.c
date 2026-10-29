#include "memory_index.h"
#include "string_util.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void flex_memory_index_init(flex_memory_index_t* index) {
    if (!index) return;
    memset(index, 0, sizeof(flex_memory_index_t));
}

void flex_memory_index_clear(flex_memory_index_t* index) {
    if (!index) return;
    index->record_count = 0;
    index->insert_head = 0;
    index->total_indexed_messages = 0;
}

bool flex_memory_index_add(
    flex_memory_index_t* index,
    uint64_t message_id,
    const char* role,
    const char* text,
    uint64_t timestamp_ms
) {
    if (!index || !text) return false;

    size_t slot;
    if (index->record_count < FLEX_MEM_MAX_RECORDS) {
        slot = index->record_count++;
    } else {
        slot = index->insert_head;
        index->insert_head = (index->insert_head + 1) % FLEX_MEM_MAX_RECORDS;
    }

    flex_mem_record_t* rec = &index->records[slot];
    rec->message_id = message_id;
    snprintf(rec->role, sizeof(rec->role), "%.15s", role ? role : "user");
    snprintf(rec->text, sizeof(rec->text), "%.255s", text);
    rec->timestamp_ms = timestamp_ms;
    rec->token_count = strlen(rec->text) / 4 + 1;

    index->total_indexed_messages++;
    return true;
}

static float score_record_match(const char* text, const char* query, size_t* out_offset) {
    if (!text || !query || *query == '\0') return 0.0f;

    float score = 0.0f;
    *out_offset = 0;

    /* Check exact case-insensitive substring match */
    const char* sub = flex_str_casestr(text, query);
    if (sub) {
        score += 10.0f; /* High reward for substring match */
        *out_offset = (size_t)(sub - text);
    }

    /* Word token matching with plural stem support */
    char q_copy[64];
    snprintf(q_copy, sizeof(q_copy), "%.63s", query);
    char* token = strtok(q_copy, " \t\r\n.,?!;:");

    while (token) {
        size_t t_len = strlen(token);
        if (t_len > 1) {
            char stem[64];
            snprintf(stem, sizeof(stem), "%.63s", token);
            size_t stem_len = t_len;
            if (stem_len > 3 && stem[stem_len - 1] == 's') {
                stem[--stem_len] = '\0';
            }

            const char* match = text;
            while ((match = flex_str_casestr(match, stem)) != NULL) {
                score += 2.0f;
                match++;
            }
        }
        token = strtok(NULL, " \t\r\n.,?!;:");
    }

    return score;
}

size_t flex_memory_index_search(
    const flex_memory_index_t* index,
    const char* query,
    flex_mem_search_result_t* out_results,
    size_t max_results
) {
    if (!index || !query || !out_results || max_results == 0 || index->record_count == 0) {
        return 0;
    }

    size_t found = 0;

    for (size_t i = 0; i < index->record_count; ++i) {
        const flex_mem_record_t* rec = &index->records[i];
        size_t match_offset = 0;
        float score = score_record_match(rec->text, query, &match_offset);

        if (score > 0.0f) {
            /* Insertion sort into out_results by score descending */
            size_t insert_pos = (found < max_results) ? found : max_results;
            while (insert_pos > 0 && out_results[insert_pos - 1].relevance_score < score) {
                if (insert_pos < max_results) {
                    out_results[insert_pos] = out_results[insert_pos - 1];
                }
                insert_pos--;
            }

            if (insert_pos < max_results) {
                flex_mem_search_result_t* res = &out_results[insert_pos];
                res->message_id = rec->message_id;
                snprintf(res->role, sizeof(res->role), "%.15s", rec->role);
                snprintf(res->snippet, sizeof(res->snippet), "%.255s", rec->text);
                res->relevance_score = score;
                res->match_offset = match_offset;

                if (found < max_results) {
                    found++;
                }
            }
        }
    }

    return found;
}
