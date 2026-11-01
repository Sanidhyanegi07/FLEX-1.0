#include "knowledge_chunker.h"
#include "crc32.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

flex_kb_chunk_config_t flex_kb_default_config(void) {
    flex_kb_chunk_config_t cfg;
    cfg.target_chunk_chars = 500;
    cfg.overlap_chars = 80;
    cfg.min_chunk_chars = 40;
    return cfg;
}

bool flex_kb_validate_document_text(
    const char* text,
    size_t text_len,
    size_t max_allowed_bytes
) {
    if (!text || text_len == 0) {
        return false;
    }

    size_t limit = (max_allowed_bytes > 0) ? max_allowed_bytes : FLEX_KB_MAX_FILE_BYTES;
    if (text_len > limit) {
        return false;
    }

    /* Verify content is text, not binary executable or media */
    size_t unprintable_count = 0;
    size_t sample_size = (text_len < 4096) ? text_len : 4096;

    for (size_t i = 0; i < sample_size; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0) {
            /* Null bytes indicate binary data */
            return false;
        }
        if (c < 32 && c != '\t' && c != '\n' && c != '\r') {
            unprintable_count++;
        }
    }

    /* Reject if more than 3% control characters */
    if (sample_size > 0 && (unprintable_count * 100 / sample_size) > 3) {
        return false;
    }

    return true;
}

size_t flex_kb_normalize_text(
    const char* input,
    size_t in_len,
    char* out_buf,
    size_t out_cap
) {
    if (!input || in_len == 0 || !out_buf || out_cap < 2) {
        if (out_buf && out_cap > 0) out_buf[0] = '\0';
        return 0;
    }

    /* Skip leading whitespace */
    size_t start = 0;
    while (start < in_len && (unsigned char)input[start] <= 32) {
        start++;
    }

    if (start >= in_len) {
        out_buf[0] = '\0';
        return 0;
    }

    /* Find trailing non-whitespace */
    size_t end = in_len;
    while (end > start && (unsigned char)input[end - 1] <= 32) {
        end--;
    }

    size_t out_idx = 0;
    size_t newline_streak = 0;

    for (size_t i = start; i < end && out_idx + 1 < out_cap; ++i) {
        char c = input[i];

        if (c == '\r') {
            /* Peek if next is \n, otherwise treat \r as \n */
            if (i + 1 < end && input[i + 1] == '\n') {
                continue;
            }
            c = '\n';
        }

        if (c == '\n') {
            newline_streak++;
            if (newline_streak <= 2) {
                out_buf[out_idx++] = '\n';
            }
        } else {
            newline_streak = 0;
            /* Replace vertical tabs or form feeds with space */
            if ((unsigned char)c < 32 && c != '\t') {
                c = ' ';
            }
            out_buf[out_idx++] = c;
        }
    }

    out_buf[out_idx] = '\0';
    return out_idx;
}

static bool is_word_boundary_char(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == '.' || c == ',' || c == ';' || c == '!' || c == '?');
}

size_t flex_kb_split_chunks(
    const char* text,
    size_t text_len,
    const flex_kb_chunk_config_t* config,
    flex_kb_chunk_t* out_chunks,
    size_t max_chunks
) {
    if (!text || text_len == 0 || !out_chunks || max_chunks == 0) {
        return 0;
    }

    flex_kb_chunk_config_t cfg = config ? *config : flex_kb_default_config();
    if (cfg.target_chunk_chars == 0) cfg.target_chunk_chars = 500;
    if (cfg.target_chunk_chars >= FLEX_KB_MAX_CHUNK_CHARS) {
        cfg.target_chunk_chars = FLEX_KB_MAX_CHUNK_CHARS - 1;
    }
    if (cfg.min_chunk_chars == 0) cfg.min_chunk_chars = 40;
    if (cfg.min_chunk_chars >= cfg.target_chunk_chars) {
        cfg.min_chunk_chars = cfg.target_chunk_chars / 2;
    }
    if (cfg.overlap_chars >= cfg.target_chunk_chars) {
        cfg.overlap_chars = cfg.target_chunk_chars / 4;
    }

    size_t chunk_count = 0;
    size_t offset = 0;

    while (offset < text_len && chunk_count < max_chunks) {
        /* Skip leading whitespace at offset */
        while (offset < text_len && ((unsigned char)text[offset] <= 32)) {
            offset++;
        }
        if (offset >= text_len) {
            break;
        }

        size_t remaining = text_len - offset;
        size_t chunk_end;

        if (remaining <= cfg.target_chunk_chars) {
            chunk_end = text_len;
        } else {
            size_t desired_end = offset + cfg.target_chunk_chars;
            size_t min_end = offset + cfg.min_chunk_chars;
            size_t best_break = desired_end;
            bool found_natural_break = false;

            /* 1. First priority: look for paragraph break (\n\n) */
            for (size_t p = desired_end; p > min_end; --p) {
                if (text[p - 1] == '\n' && p > offset + 1 && text[p - 2] == '\n') {
                    best_break = p;
                    found_natural_break = true;
                    break;
                }
            }

            /* 2. Second priority: look for sentence boundary (. ! ? followed by space or newline) */
            if (!found_natural_break) {
                for (size_t p = desired_end; p > min_end; --p) {
                    char prev = text[p - 1];
                    if ((prev == '.' || prev == '!' || prev == '?' || prev == ';') &&
                        (p < text_len && (text[p] == ' ' || text[p] == '\n' || text[p] == '\t'))) {
                        best_break = p;
                        found_natural_break = true;
                        break;
                    }
                }
            }

            /* 3. Third priority: look for word boundary (space, tab, or newline) */
            if (!found_natural_break) {
                for (size_t p = desired_end; p > min_end; --p) {
                    if (is_word_boundary_char(text[p - 1])) {
                        best_break = p;
                        found_natural_break = true;
                        break;
                    }
                }
            }

            chunk_end = best_break;
        }

        size_t copy_len = chunk_end - offset;
        if (copy_len >= FLEX_KB_MAX_CHUNK_CHARS) {
            copy_len = FLEX_KB_MAX_CHUNK_CHARS - 1;
        }

        flex_kb_chunk_t* chk = &out_chunks[chunk_count];
        chk->chunk_index = chunk_count;
        chk->start_char_offset = offset;
        chk->end_char_offset = offset + copy_len;
        chk->char_count = copy_len;
        chk->estimated_tokens = (copy_len / 4) + 1;

        memcpy(chk->content, text + offset, copy_len);
        chk->content[copy_len] = '\0';

        chunk_count++;

        if (chunk_end >= text_len) {
            break;
        }

        /* Next offset takes overlap into account */
        if (chunk_end > cfg.overlap_chars) {
            size_t next_off = chunk_end - cfg.overlap_chars;
            /* Rewind to word boundary for cleaner overlap */
            while (next_off > offset && !is_word_boundary_char(text[next_off - 1])) {
                next_off--;
            }
            if (next_off > offset) {
                offset = next_off;
            } else {
                offset = chunk_end;
            }
        } else {
            offset = chunk_end;
        }
    }

    return chunk_count;
}

static char to_lower_char(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool case_insensitive_contains(const char* haystack, const char* needle, size_t needle_len) {
    if (!haystack || !needle || needle_len == 0) return false;
    size_t h_len = strlen(haystack);
    if (h_len < needle_len) return false;

    for (size_t i = 0; i <= h_len - needle_len; ++i) {
        bool match = true;
        for (size_t j = 0; j < needle_len; ++j) {
            if (to_lower_char(haystack[i + j]) != to_lower_char(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

float flex_kb_score_chunk_relevance(
    const char* query,
    const char* chunk_content
) {
    if (!query || !chunk_content || *query == '\0' || *chunk_content == '\0') {
        return 0.0f;
    }

    size_t qlen = strlen(query);
    float score = 0.0f;

    /* Exact phrase bonus */
    if (case_insensitive_contains(chunk_content, query, qlen)) {
        score += 4.0f;
    }

    /* Individual term matching */
    char word[64];
    size_t word_idx = 0;
    size_t terms_matched = 0;
    size_t total_terms = 0;

    for (size_t i = 0; i <= qlen; ++i) {
        char c = query[i];
        if (c != '\0' && !is_word_boundary_char(c) && word_idx + 1 < sizeof(word)) {
            word[word_idx++] = c;
        } else if (word_idx > 0) {
            word[word_idx] = '\0';
            total_terms++;

            /* Ignore very short stopwords like "a", "an", "the", "in", "is" */
            if (word_idx > 2 || (word_idx > 1 && strcmp(word, "C") == 0)) {
                if (case_insensitive_contains(chunk_content, word, word_idx)) {
                    score += 1.5f;
                    terms_matched++;
                }
            } else if (word_idx > 0) {
                if (case_insensitive_contains(chunk_content, word, word_idx)) {
                    score += 0.5f;
                    terms_matched++;
                }
            }
            word_idx = 0;
        }
    }

    if (total_terms > 0 && terms_matched == total_terms) {
        /* All query terms present in this chunk */
        score += 2.0f;
    }

    return score;
}

void flex_kb_compute_content_hash(
    const char* text,
    size_t len,
    char out_hex[16]
) {
    if (!out_hex) return;
    if (!text || len == 0) {
        strcpy(out_hex, "00000000");
        return;
    }

    uint32_t crc = flex_crc32(text, len);
    flex_crc32_to_hex(crc, out_hex, 16);
}

size_t flex_kb_split_markdown_sections(
    const char* markdown,
    size_t md_len,
    flex_kb_chunk_t* out_chunks,
    size_t max_chunks
) {
    if (!markdown || md_len == 0 || !out_chunks || max_chunks == 0) {
        return 0;
    }

    size_t chunk_count = 0;
    size_t section_start = 0;

    for (size_t i = 0; i < md_len && chunk_count < max_chunks; ++i) {
        bool is_header = false;
        if (i == 0 || markdown[i - 1] == '\n') {
            if (markdown[i] == '#' && (i + 1 < md_len && (markdown[i + 1] == ' ' || markdown[i + 1] == '#'))) {
                is_header = true;
            }
        }

        if (is_header && i > section_start) {
            size_t sec_len = i - section_start;
            if (sec_len > 10) {
                if (sec_len >= FLEX_KB_MAX_CHUNK_CHARS) {
                    sec_len = FLEX_KB_MAX_CHUNK_CHARS - 1;
                }
                flex_kb_chunk_t* chk = &out_chunks[chunk_count];
                chk->chunk_index = chunk_count;
                chk->start_char_offset = section_start;
                chk->end_char_offset = section_start + sec_len;
                chk->char_count = sec_len;
                chk->estimated_tokens = (sec_len / 4) + 1;
                memcpy(chk->content, markdown + section_start, sec_len);
                chk->content[sec_len] = '\0';
                chunk_count++;
            }
            section_start = i;
        }
    }

    if (section_start < md_len && chunk_count < max_chunks) {
        size_t sec_len = md_len - section_start;
        if (sec_len >= FLEX_KB_MAX_CHUNK_CHARS) {
            sec_len = FLEX_KB_MAX_CHUNK_CHARS - 1;
        }
        flex_kb_chunk_t* chk = &out_chunks[chunk_count];
        chk->chunk_index = chunk_count;
        chk->start_char_offset = section_start;
        chk->end_char_offset = section_start + sec_len;
        chk->char_count = sec_len;
        chk->estimated_tokens = (sec_len / 4) + 1;
        memcpy(chk->content, markdown + section_start, sec_len);
        chk->content[sec_len] = '\0';
        chunk_count++;
    }

    return chunk_count;
}

static uint32_t simple_hash_str(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

float flex_kb_jaccard_similarity(
    const char* text1,
    size_t len1,
    const char* text2,
    size_t len2,
    int n_gram_size
) {
    if (!text1 || !text2 || len1 == 0 || len2 == 0) return 0.0f;
    if (n_gram_size <= 0) n_gram_size = 3;

    if (len1 < (size_t)n_gram_size || len2 < (size_t)n_gram_size) {
        return (strcmp(text1, text2) == 0) ? 1.0f : 0.0f;
    }

    uint32_t set1[256];
    size_t count1 = 0;
    size_t max_shingles1 = len1 - n_gram_size + 1;
    if (max_shingles1 > 256) max_shingles1 = 256;

    for (size_t i = 0; i < max_shingles1; ++i) {
        uint32_t h = simple_hash_str(text1 + i, n_gram_size);
        bool exists = false;
        for (size_t j = 0; j < count1; ++j) {
            if (set1[j] == h) { exists = true; break; }
        }
        if (!exists && count1 < 256) {
            set1[count1++] = h;
        }
    }

    uint32_t set2[256];
    size_t count2 = 0;
    size_t max_shingles2 = len2 - n_gram_size + 1;
    if (max_shingles2 > 256) max_shingles2 = 256;

    for (size_t i = 0; i < max_shingles2; ++i) {
        uint32_t h = simple_hash_str(text2 + i, n_gram_size);
        bool exists = false;
        for (size_t j = 0; j < count2; ++j) {
            if (set2[j] == h) { exists = true; break; }
        }
        if (!exists && count2 < 256) {
            set2[count2++] = h;
        }
    }

    if (count1 == 0 && count2 == 0) return 1.0f;
    if (count1 == 0 || count2 == 0) return 0.0f;

    size_t intersection = 0;
    for (size_t i = 0; i < count1; ++i) {
        for (size_t j = 0; j < count2; ++j) {
            if (set1[i] == set2[j]) {
                intersection++;
                break;
            }
        }
    }

    size_t union_size = count1 + count2 - intersection;
    if (union_size == 0) return 0.0f;

    return (float)intersection / (float)union_size;
}

size_t flex_kb_extract_chunk_keywords(
    const char* text,
    size_t text_len,
    char out_keywords[][32],
    size_t max_keywords
) {
    if (!text || text_len == 0 || !out_keywords || max_keywords == 0) {
        return 0;
    }

    const char* stopwords[] = {
        "the", "and", "for", "with", "that", "this", "from", "are", "was",
        "were", "will", "have", "has", "had", "not", "but", "all", "any",
        "can", "could", "should", "would", "into", "over", "more", "then",
        NULL
    };

    size_t count = 0;
    char current_word[32];
    size_t word_len = 0;

    for (size_t i = 0; i <= text_len && count < max_keywords; ++i) {
        char c = (i < text_len) ? text[i] : ' ';
        bool is_letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');

        if (is_letter && word_len + 1 < sizeof(current_word)) {
            current_word[word_len++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        } else if (word_len > 0) {
            current_word[word_len] = '\0';

            if (word_len >= 3) {
                bool is_stop = false;
                for (int s = 0; stopwords[s] != NULL; ++s) {
                    if (strcmp(current_word, stopwords[s]) == 0) {
                        is_stop = true;
                        break;
                    }
                }

                if (!is_stop) {
                    bool already_have = false;
                    for (size_t k = 0; k < count; ++k) {
                        if (strcmp(out_keywords[k], current_word) == 0) {
                            already_have = true;
                            break;
                        }
                    }

                    if (!already_have) {
                        strncpy(out_keywords[count], current_word, 31);
                        out_keywords[count][31] = '\0';
                        count++;
                    }
                }
            }
            word_len = 0;
        }
    }

    return count;
}

size_t flex_kb_format_retrieval_context(
    const flex_kb_chunk_t* chunks,
    const char* const* doc_names,
    size_t chunk_count,
    size_t max_total_chars,
    char* out_buf,
    size_t out_cap
) {
    if (!chunks || chunk_count == 0 || !out_buf || out_cap < 32) {
        if (out_buf && out_cap > 0) out_buf[0] = '\0';
        return 0;
    }

    const char* header = "\n\n[Knowledge Base Reference Documents (Untrusted Context)]:\n";
    size_t written = 0;
    size_t hlen = strlen(header);
    if (hlen < out_cap) {
        memcpy(out_buf, header, hlen);
        written = hlen;
    }

    for (size_t i = 0; i < chunk_count; ++i) {
        if (written >= max_total_chars || written + 64 >= out_cap) {
            break;
        }

        const char* dname = (doc_names && doc_names[i]) ? doc_names[i] : "Document";
        char chunk_hdr[128];
        int ch_len = snprintf(
            chunk_hdr, sizeof(chunk_hdr),
            "--- Source: %s (Chunk %zu) ---\n",
            dname, chunks[i].chunk_index
        );

        if (ch_len > 0 && written + (size_t)ch_len < out_cap) {
            memcpy(out_buf + written, chunk_hdr, (size_t)ch_len);
            written += (size_t)ch_len;
        }

        size_t content_len = strlen(chunks[i].content);
        size_t available = out_cap - written - 2;
        if (content_len > available) {
            content_len = available;
        }

        if (written + content_len < max_total_chars) {
            memcpy(out_buf + written, chunks[i].content, content_len);
            written += content_len;
            out_buf[written++] = '\n';
        }
    }

    out_buf[written] = '\0';
    return written;
}

size_t flex_kb_clip_chunks_to_token_budget(
    const flex_kb_chunk_t* in_chunks,
    size_t in_count,
    size_t token_budget,
    flex_kb_chunk_t* out_chunks,
    size_t max_out
) {
    if (!in_chunks || in_count == 0 || !out_chunks || max_out == 0 || token_budget == 0) {
        return 0;
    }

    size_t accumulated_tokens = 0;
    size_t out_count = 0;

    for (size_t i = 0; i < in_count && out_count < max_out; ++i) {
        size_t est = in_chunks[i].estimated_tokens;
        if (accumulated_tokens + est > token_budget && out_count > 0) {
            break;
        }

        out_chunks[out_count] = in_chunks[i];
        accumulated_tokens += est;
        out_count++;
    }

    return out_count;
}

size_t flex_kb_extract_match_snippet(
    const char* text,
    size_t text_len,
    const char* keyword,
    char* out_snippet,
    size_t snippet_cap
) {
    if (!text || text_len == 0 || !out_snippet || snippet_cap < 8) {
        if (out_snippet && snippet_cap > 0) out_snippet[0] = '\0';
        return 0;
    }

    if (!keyword || *keyword == '\0') {
        size_t take = (text_len < snippet_cap - 1) ? text_len : snippet_cap - 1;
        memcpy(out_snippet, text, take);
        out_snippet[take] = '\0';
        return take;
    }

    size_t klen = strlen(keyword);
    const char* match = NULL;

    for (size_t i = 0; i + klen <= text_len; ++i) {
        bool eq = true;
        for (size_t j = 0; j < klen; ++j) {
            char c1 = (text[i + j] >= 'A' && text[i + j] <= 'Z') ? (char)(text[i + j] + 32) : text[i + j];
            char c2 = (keyword[j] >= 'A' && keyword[j] <= 'Z') ? (char)(keyword[j] + 32) : keyword[j];
            if (c1 != c2) {
                eq = false;
                break;
            }
        }
        if (eq) {
            match = text + i;
            break;
        }
    }

    size_t match_offset = match ? (size_t)(match - text) : 0;
    size_t context_window = (snippet_cap > 10) ? (snippet_cap - 8) / 2 : 20;

    size_t start = (match_offset > context_window) ? (match_offset - context_window) : 0;
    while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\n') {
        start--;
    }

    size_t end = match_offset + klen + context_window;
    if (end > text_len) end = text_len;
    while (end < text_len && text[end] != ' ' && text[end] != '\n') {
        end++;
    }

    size_t written = 0;
    if (start > 0 && written + 3 < snippet_cap) {
        memcpy(out_snippet + written, "...", 3);
        written += 3;
    }

    size_t body_len = end - start;
    if (written + body_len >= snippet_cap) {
        body_len = snippet_cap - written - 4;
    }

    memcpy(out_snippet + written, text + start, body_len);
    written += body_len;

    if (end < text_len && written + 3 < snippet_cap) {
        memcpy(out_snippet + written, "...", 3);
        written += 3;
    }

    out_snippet[written] = '\0';
    return written;
}
