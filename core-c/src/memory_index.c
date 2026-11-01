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
    } else {
        /* Reverse substring match: does query contain text (e.g. "What's my name?" contains "My Name") */
        const char* rev_sub = flex_str_casestr(query, text);
        if (rev_sub) {
            score += 10.0f;
            *out_offset = 0;
        }
    }

    /* Word token matching with plural stem support */
    char q_copy[64];
    snprintf(q_copy, sizeof(q_copy), "%.63s", query);
    char* token = strtok(q_copy, " \t\r\n.,?!;:'\"");

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
        token = strtok(NULL, " \t\r\n.,?!;:'\"");
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

bool flex_mem_validate_fact(
    const char* key,
    const char* content,
    const char* category
) {
    if (!key || !content) return false;
    size_t klen = strlen(key);
    size_t clen = strlen(content);
    if (klen == 0 || klen > 64) return false;
    if (clen == 0 || clen > 1024) return false;
    if (category && strlen(category) > 32) return false;
    return true;
}

float flex_mem_fact_relevance(
    const char* query,
    const char* key,
    const char* content
) {
    if (!query || *query == '\0') return 0.0f;
    float score = 0.0f;
    size_t dummy_offset = 0;

    if (key && *key != '\0') {
        score += score_record_match(key, query, &dummy_offset) * 2.5f; /* Key match weighted higher */
        if (flex_str_casestr(query, key) != NULL) {
            score += 15.0f;
        }
    }
    if (content && *content != '\0') {
        score += score_record_match(content, query, &dummy_offset);
        if (flex_str_casestr(query, content) != NULL) {
            score += 10.0f;
        }
    }
    return score;
}

size_t flex_mem_format_context(
    const char* key,
    const char* content,
    char* out_buf,
    size_t out_buf_size
) {
    if (!key || !content || !out_buf || out_buf_size < 8) return 0;

    int written = snprintf(
        out_buf, out_buf_size,
        "- %s: %s\n",
        key, content
    );

    if (written < 0 || (size_t)written >= out_buf_size) return 0;
    return (size_t)written;
}

void flex_mem_fact_table_init(flex_mem_fact_table_t* table) {
    if (!table) return;
    memset(table, 0, sizeof(flex_mem_fact_table_t));
}

int flex_mem_fact_table_upsert(
    flex_mem_fact_table_t* table,
    const char* key,
    const char* content,
    const char* category,
    uint64_t timestamp_ms
) {
    if (!table || !key || !content) return -1;
    if (!flex_mem_validate_fact(key, content, category)) return -1;

    /* Check if key already exists */
    for (size_t i = 0; i < table->count; ++i) {
        if (strcmp(table->facts[i].key, key) == 0) {
            strncpy(table->facts[i].content, content, sizeof(table->facts[i].content) - 1);
            table->facts[i].content[sizeof(table->facts[i].content) - 1] = '\0';
            if (category) {
                strncpy(table->facts[i].category, category, sizeof(table->facts[i].category) - 1);
                table->facts[i].category[sizeof(table->facts[i].category) - 1] = '\0';
            }
            table->facts[i].updated_at = timestamp_ms;
            return (int)i;
        }
    }

    if (table->count >= FLEX_MEM_FACT_TABLE_CAPACITY) {
        return -1;
    }

    size_t idx = table->count++;
    strncpy(table->facts[idx].key, key, sizeof(table->facts[idx].key) - 1);
    table->facts[idx].key[sizeof(table->facts[idx].key) - 1] = '\0';
    strncpy(table->facts[idx].content, content, sizeof(table->facts[idx].content) - 1);
    table->facts[idx].content[sizeof(table->facts[idx].content) - 1] = '\0';
    if (category) {
        strncpy(table->facts[idx].category, category, sizeof(table->facts[idx].category) - 1);
        table->facts[idx].category[sizeof(table->facts[idx].category) - 1] = '\0';
    } else {
        strcpy(table->facts[idx].category, "general");
    }
    table->facts[idx].updated_at = timestamp_ms;
    return (int)idx;
}

const flex_mem_fact_t* flex_mem_fact_table_get(
    flex_mem_fact_table_t* table,
    const char* key
) {
    if (!table || !key) return NULL;
    table->total_lookups++;

    for (size_t i = 0; i < table->count; ++i) {
        if (strcmp(table->facts[i].key, key) == 0) {
            table->total_hits++;
            return &table->facts[i];
        }
    }
    return NULL;
}

bool flex_mem_fact_table_delete(
    flex_mem_fact_table_t* table,
    const char* key
) {
    if (!table || !key) return false;

    for (size_t i = 0; i < table->count; ++i) {
        if (strcmp(table->facts[i].key, key) == 0) {
            for (size_t j = i; j + 1 < table->count; ++j) {
                table->facts[j] = table->facts[j + 1];
            }
            table->count--;
            memset(&table->facts[table->count], 0, sizeof(flex_mem_fact_t));
            return true;
        }
    }
    return false;
}

size_t flex_mem_levenshtein_distance(
    const char* s1,
    const char* s2
) {
    if (!s1 && !s2) return 0;
    if (!s1) return strlen(s2);
    if (!s2) return strlen(s1);

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;

    size_t d[256];
    if (len2 >= 256) len2 = 255;

    for (size_t j = 0; j <= len2; ++j) {
        d[j] = j;
    }

    for (size_t i = 1; i <= len1; ++i) {
        size_t prev = d[0];
        d[0] = i;
        for (size_t j = 1; j <= len2; ++j) {
            size_t temp = d[j];
            size_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            size_t ins = d[j] + 1;
            size_t del = d[j - 1] + 1;
            size_t sub = prev + cost;
            size_t min_val = ins < del ? ins : del;
            if (sub < min_val) min_val = sub;
            d[j] = min_val;
            prev = temp;
        }
    }

    return d[len2];
}

float flex_mem_jaro_winkler_similarity(
    const char* s1,
    const char* s2
) {
    if (!s1 || !s2) return 0.0f;
    if (strcmp(s1, s2) == 0) return 1.0f;

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    if (len1 == 0 || len2 == 0) return 0.0f;

    size_t match_distance = (len1 > len2 ? len1 : len2) / 2;
    if (match_distance > 0) match_distance--;

    bool s1_matches[128] = {false};
    bool s2_matches[128] = {false};
    if (len1 > 128) len1 = 128;
    if (len2 > 128) len2 = 128;

    size_t matches = 0;
    for (size_t i = 0; i < len1; ++i) {
        size_t start = (i >= match_distance) ? (i - match_distance) : 0;
        size_t end = (i + match_distance + 1 < len2) ? (i + match_distance + 1) : len2;
        for (size_t j = start; j < end; ++j) {
            if (s2_matches[j]) continue;
            if (s1[i] != s2[j]) continue;
            s1_matches[i] = true;
            s2_matches[j] = true;
            matches++;
            break;
        }
    }

    if (matches == 0) return 0.0f;

    size_t k = 0;
    size_t transpositions = 0;
    for (size_t i = 0; i < len1; ++i) {
        if (!s1_matches[i]) continue;
        while (!s2_matches[k]) k++;
        if (s1[i] != s2[k]) transpositions++;
        k++;
    }

    float m = (float)matches;
    float jaro = ((m / (float)len1) + (m / (float)len2) + ((m - (float)transpositions / 2.0f) / m)) / 3.0f;

    /* Winkler prefix scaling */
    size_t prefix_len = 0;
    size_t max_prefix = 4;
    while (prefix_len < len1 && prefix_len < len2 && prefix_len < max_prefix && s1[prefix_len] == s2[prefix_len]) {
        prefix_len++;
    }

    float jaro_winkler = jaro + ((float)prefix_len * 0.1f * (1.0f - jaro));
    return (jaro_winkler > 1.0f) ? 1.0f : jaro_winkler;
}

bool flex_mem_fact_is_duplicate(
    const flex_mem_fact_t* f1,
    const flex_mem_fact_t* f2,
    float similarity_threshold
) {
    if (!f1 || !f2) return false;
    if (strcmp(f1->key, f2->key) == 0) return true;

    float key_sim = flex_mem_jaro_winkler_similarity(f1->key, f2->key);
    float content_sim = flex_mem_jaro_winkler_similarity(f1->content, f2->content);

    float combined = (key_sim * 0.6f) + (content_sim * 0.4f);
    return combined >= similarity_threshold;
}

size_t flex_mem_fact_to_json(
    const flex_mem_fact_t* fact,
    char* out_buf,
    size_t buf_cap
) {
    if (!fact || !out_buf || buf_cap < 128) return 0;

    int written = snprintf(
        out_buf, buf_cap,
        "{\"key\":\"%s\",\"content\":\"%s\",\"category\":\"%s\",\"updated_at\":%llu}",
        fact->key, fact->content, fact->category, (unsigned long long)fact->updated_at
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    return (size_t)written;
}


size_t flex_mem_extract_query_keywords(
    const char* query,
    char keywords[][32],
    size_t max_keywords
) {
    if (!query || !keywords || max_keywords == 0) return 0;

    char q_copy[256];
    snprintf(q_copy, sizeof(q_copy), "%.255s", query);
    char* token = strtok(q_copy, " \t\r\n.,?!;:'\"`~@#$%^&*()-_=+[]{}|/<>");
    size_t count = 0;

    while (token && count < max_keywords) {
        size_t len = strlen(token);
        if (len > 1) {
            for (size_t i = 0; i < len && i < 31; ++i) {
                keywords[count][i] = (char)tolower((unsigned char)token[i]);
                keywords[count][i + 1] = '\0';
            }
            count++;
        }
        token = strtok(NULL, " \t\r\n.,?!;:'\"`~@#$%^&*()-_=+[]{}|/<>");
    }
    return count;
}

float flex_mem_query_similarity(
    const char* query,
    const char* target
) {
    if (!query || !target) return 0.0f;
    size_t qlen = strlen(query);
    size_t tlen = strlen(target);
    if (qlen == 0 || tlen == 0) return 0.0f;

    if (flex_str_casecmp(query, target) == 0) return 1.0f;

    if (flex_str_casestr(query, target) != NULL || flex_str_casestr(target, query) != NULL) {
        return 0.85f;
    }

    char kw[16][32];
    size_t nkw = flex_mem_extract_query_keywords(query, kw, 16);
    if (nkw == 0) return 0.0f;

    size_t hits = 0;
    for (size_t i = 0; i < nkw; ++i) {
        if (flex_str_casestr(target, kw[i]) != NULL) {
            hits++;
        }
    }

    float token_ratio = (float)hits / (float)nkw;
    float jw = flex_mem_jaro_winkler_similarity(query, target);

    return (token_ratio * 0.7f) + (jw * 0.3f);
}
