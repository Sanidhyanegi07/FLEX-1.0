#include "tts_pipeline.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void flex_tts_queue_init(flex_tts_queue_t* q) {
    if (!q) return;
    memset(q, 0, sizeof(flex_tts_queue_t));
    q->next_id = 1;
}

bool flex_tts_queue_push(
    flex_tts_queue_t* q,
    const char* text,
    bool is_final
) {
    if (!q || !text || *text == '\0' || q->count >= FLEX_TTS_QUEUE_CAP) {
        return false;
    }

    flex_tts_utterance_t* item = &q->items[q->tail];
    snprintf(item->text, sizeof(item->text), "%.255s", text);
    item->utterance_id = q->next_id++;
    item->is_final_in_turn = is_final;

    q->tail = (q->tail + 1) % FLEX_TTS_QUEUE_CAP;
    q->count++;
    return true;
}

bool flex_tts_queue_pop(
    flex_tts_queue_t* q,
    flex_tts_utterance_t* out_item
) {
    if (!q || q->count == 0) return false;

    if (out_item) {
        *out_item = q->items[q->head];
    }

    q->head = (q->head + 1) % FLEX_TTS_QUEUE_CAP;
    q->count--;
    return true;
}

void flex_tts_queue_clear(flex_tts_queue_t* q) {
    if (!q) return;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

typedef struct {
    const char* pattern;
    const char* replacement;
} abbreviation_map_t;

static const abbreviation_map_t S_ABBREVIATIONS[] = {
    { "C++", "C plus plus" },
    { "C11", "C eleven" },
    { "e.g.", "for example" },
    { "i.e.", "that is" },
    { "etc.", "and so on" },
    { "vs.", "versus" },
    { "API", "A P I" },
    { "HTTP", "H T T P" },
    { "SQL", "S Q L" },
    { "URL", "U R L" },
    { "DB", "database" },
    { "ms", "milliseconds" },
    { "sec", "seconds" },
    { "MB", "megabytes" },
    { "kB", "kilobytes" },
    { NULL, NULL }
};

size_t flex_tts_normalize_text(
    const char* input_text,
    char* out_speech_text,
    size_t out_cap
) {
    if (!input_text || !out_speech_text || out_cap < 2) return 0;

    size_t out_idx = 0;
    const char* p = input_text;

    while (*p && out_idx + 1 < out_cap) {
        /* Strip markdown syntax: *, _, #, `, >, ~ */
        if (*p == '*' || *p == '_' || *p == '#' || *p == '`' || *p == '>' || *p == '~') {
            p++;
            continue;
        }

        /* Check abbreviation expansion */
        bool expanded = false;
        bool prev_is_boundary = (p == input_text || isspace((unsigned char)p[-1]) || ispunct((unsigned char)p[-1]));

        if (prev_is_boundary) {
            for (size_t k = 0; S_ABBREVIATIONS[k].pattern != NULL; ++k) {
                size_t pat_len = strlen(S_ABBREVIATIONS[k].pattern);
                if (strncmp(p, S_ABBREVIATIONS[k].pattern, pat_len) == 0) {
                    /* Check following boundary (space, punctuation, or end) */
                    char next_c = p[pat_len];
                    if (next_c == '\0' || isspace((unsigned char)next_c) || ispunct((unsigned char)next_c)) {
                        const char* rep = S_ABBREVIATIONS[k].replacement;
                        size_t rep_len = strlen(rep);
                        if (out_idx + rep_len < out_cap) {
                            memcpy(out_speech_text + out_idx, rep, rep_len);
                            out_idx += rep_len;
                            p += pat_len;
                            expanded = true;
                            break;
                        }
                    }
                }
            }
        }
        if (expanded) continue;

        /* Normal character copy */
        out_speech_text[out_idx++] = *p++;
    }

    out_speech_text[out_idx] = '\0';
    return out_idx;
}

size_t flex_tts_split_sentences(
    const char* speech_text,
    char out_sentences[][FLEX_TTS_UTTERANCE_MAX],
    size_t max_sentences
) {
    if (!speech_text || !out_sentences || max_sentences == 0) return 0;

    size_t sentence_count = 0;
    const char* p = speech_text;

    while (*p && sentence_count < max_sentences) {
        /* Skip leading whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;

        size_t s_idx = 0;
        char* cur_dest = out_sentences[sentence_count];

        while (*p && s_idx + 1 < FLEX_TTS_UTTERANCE_MAX) {
            char c = *p++;
            cur_dest[s_idx++] = c;

            /* Check sentence boundary: . ! ? followed by whitespace or end */
            if ((c == '.' || c == '!' || c == '?') && s_idx >= 5) {
                if (*p == '\0' || isspace((unsigned char)*p)) {
                    break;
                }
            }
        }

        /* Trim trailing space */
        while (s_idx > 0 && isspace((unsigned char)cur_dest[s_idx - 1])) {
            s_idx--;
        }

        if (s_idx > 0) {
            cur_dest[s_idx] = '\0';
            sentence_count++;
        }
    }

    return sentence_count;
}
