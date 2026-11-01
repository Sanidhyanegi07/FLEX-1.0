#ifndef FLEX_MEMORY_INDEX_H
#define FLEX_MEMORY_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_MEM_MAX_RECORDS 128
#define FLEX_MEM_TEXT_SNIPPET_MAX 256
#define FLEX_MEM_MAX_SEARCH_RESULTS 16

typedef struct {
    uint64_t message_id;
    char role[16];
    char text[FLEX_MEM_TEXT_SNIPPET_MAX];
    uint64_t timestamp_ms;
    size_t token_count;
} flex_mem_record_t;

typedef struct {
    uint64_t message_id;
    char role[16];
    char snippet[FLEX_MEM_TEXT_SNIPPET_MAX];
    float relevance_score;
    size_t match_offset;
} flex_mem_search_result_t;

typedef struct {
    flex_mem_record_t records[FLEX_MEM_MAX_RECORDS];
    size_t record_count;
    size_t insert_head;
    uint64_t total_indexed_messages;
} flex_memory_index_t;

/**
 * Initializes the memory search index.
 */
void flex_memory_index_init(flex_memory_index_t* index);

/**
 * Adds or updates a message record in the in-memory index.
 */
bool flex_memory_index_add(
    flex_memory_index_t* index,
    uint64_t message_id,
    const char* role,
    const char* text,
    uint64_t timestamp_ms
);

/**
 * Performs a ranked keyword and substring search across indexed messages.
 * Fills out_results up to max_results, returns number of results found.
 */
size_t flex_memory_index_search(
    const flex_memory_index_t* index,
    const char* query,
    flex_mem_search_result_t* out_results,
    size_t max_results
);

/**
 * Clears all records from the index.
 */
void flex_memory_index_clear(flex_memory_index_t* index);

typedef struct {
    char key[64];
    char content[256];
    char category[32];
    uint64_t updated_at;
} flex_mem_fact_t;

/**
 * Validates memory fact fields (non-empty key and content, length boundaries).
 */
bool flex_mem_validate_fact(
    const char* key,
    const char* content,
    const char* category
);

/**
 * Calculates a relevance score for a memory fact given a search query.
 */
float flex_mem_fact_relevance(
    const char* query,
    const char* key,
    const char* content
);

/**
 * Formats a memory fact into an injection-safe context line.
 */
size_t flex_mem_format_context(
    const char* key,
    const char* content,
    char* out_buf,
    size_t out_buf_size
);

#define FLEX_MEM_FACT_TABLE_CAPACITY 64

typedef struct {
    flex_mem_fact_t facts[FLEX_MEM_FACT_TABLE_CAPACITY];
    size_t count;
    uint64_t total_lookups;
    uint64_t total_hits;
} flex_mem_fact_table_t;

/**
 * Initializes the in-memory fact table.
 */
void flex_mem_fact_table_init(flex_mem_fact_table_t* table);

/**
 * Inserts or updates a fact in the fact table. Returns index slot or -1 on error.
 */
int flex_mem_fact_table_upsert(
    flex_mem_fact_table_t* table,
    const char* key,
    const char* content,
    const char* category,
    uint64_t timestamp_ms
);

/**
 * Finds a fact in the fact table by exact key match. Returns NULL if not found.
 */
const flex_mem_fact_t* flex_mem_fact_table_get(
    flex_mem_fact_table_t* table,
    const char* key
);

/**
 * Deletes a fact from the fact table by key. Returns true if removed.
 */
bool flex_mem_fact_table_delete(
    flex_mem_fact_table_t* table,
    const char* key
);

/**
 * Computes Levenshtein edit distance between two strings.
 */
size_t flex_mem_levenshtein_distance(
    const char* s1,
    const char* s2
);

/**
 * Computes Jaro-Winkler string similarity (0.0 to 1.0).
 */
float flex_mem_jaro_winkler_similarity(
    const char* s1,
    const char* s2
);

/**
 * Evaluates whether two facts are near-duplicates based on key and content similarity.
 */
bool flex_mem_fact_is_duplicate(
    const flex_mem_fact_t* f1,
    const flex_mem_fact_t* f2,
    float similarity_threshold
);

/**
 * Serializes a fact to a compact JSON string.
 */
size_t flex_mem_fact_to_json(
    const flex_mem_fact_t* fact,
    char* out_buf,
    size_t buf_cap
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_MEMORY_INDEX_H */

