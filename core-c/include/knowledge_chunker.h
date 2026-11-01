#ifndef FLEX_KNOWLEDGE_CHUNKER_H
#define FLEX_KNOWLEDGE_CHUNKER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_KB_MAX_CHUNK_CHARS 1024
#define FLEX_KB_MAX_CHUNKS_PER_DOC 512
#define FLEX_KB_MAX_FILE_BYTES (10 * 1024 * 1024) /* 10 MB maximum document size */

typedef struct {
    size_t chunk_index;
    char content[FLEX_KB_MAX_CHUNK_CHARS];
    size_t char_count;
    size_t estimated_tokens;
    size_t start_char_offset;
    size_t end_char_offset;
} flex_kb_chunk_t;

typedef struct {
    size_t target_chunk_chars; /* e.g. 500 chars */
    size_t overlap_chars;      /* e.g. 80 chars */
    size_t min_chunk_chars;    /* e.g. 40 chars */
} flex_kb_chunk_config_t;

/**
 * Returns default chunk configuration (500 chars target, 80 chars overlap, 40 min).
 */
flex_kb_chunk_config_t flex_kb_default_config(void);

/**
 * Validates document text buffer: checks non-null, size within bounds,
 * and verifies that content consists predominantly of printable/text characters.
 */
bool flex_kb_validate_document_text(
    const char* text,
    size_t text_len,
    size_t max_allowed_bytes
);

/**
 * Normalizes input text: converts CRLF and CR to LF, collapses redundant blank lines,
 * and trims whitespace. Returns written character count.
 */
size_t flex_kb_normalize_text(
    const char* input,
    size_t in_len,
    char* out_buf,
    size_t out_cap
);

/**
 * Splits normalized text into chunks using word-boundary awareness and configurable overlap.
 * Fills out_chunks up to max_chunks, returns total chunk count produced.
 */
size_t flex_kb_split_chunks(
    const char* text,
    size_t text_len,
    const flex_kb_chunk_config_t* config,
    flex_kb_chunk_t* out_chunks,
    size_t max_chunks
);

/**
 * Computes a relevance score (0.0f to 10.0f+) between a search query and a chunk content.
 * Evaluates term match frequency, exact substring presence, and word boundaries.
 */
float flex_kb_score_chunk_relevance(
    const char* query,
    const char* chunk_content
);

/**
 * Computes a hex-formatted CRC32 content hash for duplicate detection.
 */
void flex_kb_compute_content_hash(
    const char* text,
    size_t len,
    char out_hex[16]
);

/**
 * Splits markdown document by headers (#, ##, ###) into hierarchical section chunks.
 */
size_t flex_kb_split_markdown_sections(
    const char* markdown,
    size_t md_len,
    flex_kb_chunk_t* out_chunks,
    size_t max_chunks
);

/**
 * Calculates Jaccard n-gram similarity between two text snippets (0.0f to 1.0f).
 */
float flex_kb_jaccard_similarity(
    const char* text1,
    size_t len1,
    const char* text2,
    size_t len2,
    int n_gram_size
);

/**
 * Extracts dominant keyword terms from a text chunk, excluding common stopwords.
 */
size_t flex_kb_extract_chunk_keywords(
    const char* text,
    size_t text_len,
    char out_keywords[][32],
    size_t max_keywords
);

/**
 * Formats an array of retrieved chunks into an injection-safe LLM prompt context block.
 */
size_t flex_kb_format_retrieval_context(
    const flex_kb_chunk_t* chunks,
    const char* const* doc_names,
    size_t chunk_count,
    size_t max_total_chars,
    char* out_buf,
    size_t out_cap
);

/**
 * Clips an array of chunks into an aggregate token budget (e.g. 350 tokens).
 * Returns number of chunks that fit within budget.
 */
size_t flex_kb_clip_chunks_to_token_budget(
    const flex_kb_chunk_t* in_chunks,
    size_t in_count,
    size_t token_budget,
    flex_kb_chunk_t* out_chunks,
    size_t max_out
);

/**
 * Extracts a centered snippet around the first keyword match in a text chunk.
 */
size_t flex_kb_extract_match_snippet(
    const char* text,
    size_t text_len,
    const char* keyword,
    char* out_snippet,
    size_t snippet_cap
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_KNOWLEDGE_CHUNKER_H */

