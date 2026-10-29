#ifndef FLEX_TTS_PIPELINE_H
#define FLEX_TTS_PIPELINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_TTS_UTTERANCE_MAX 256
#define FLEX_TTS_QUEUE_CAP 16

typedef struct {
    char text[FLEX_TTS_UTTERANCE_MAX];
    uint32_t utterance_id;
    bool is_final_in_turn;
} flex_tts_utterance_t;

typedef struct {
    flex_tts_utterance_t items[FLEX_TTS_QUEUE_CAP];
    size_t head;
    size_t tail;
    size_t count;
    uint32_t next_id;
} flex_tts_queue_t;

/**
 * Initializes the TTS utterance queue.
 */
void flex_tts_queue_init(flex_tts_queue_t* q);

/**
 * Enqueues a speech utterance. Returns true on success, false if full.
 */
bool flex_tts_queue_push(
    flex_tts_queue_t* q,
    const char* text,
    bool is_final
);

/**
 * Dequeues the next utterance. Returns true if an item was popped.
 */
bool flex_tts_queue_pop(
    flex_tts_queue_t* q,
    flex_tts_utterance_t* out_item
);

/**
 * Clears all pending utterances from the queue.
 */
void flex_tts_queue_clear(flex_tts_queue_t* q);

/**
 * Normalizes text for speech synthesis: expands abbreviations,
 * strips markdown formatting, and sanitizes punctuation.
 */
size_t flex_tts_normalize_text(
    const char* input_text,
    char* out_speech_text,
    size_t out_cap
);

/**
 * Splits normalized text into sentence-bounded utterances for the TTS queue.
 * Returns number of utterances produced.
 */
size_t flex_tts_split_sentences(
    const char* speech_text,
    char out_sentences[][FLEX_TTS_UTTERANCE_MAX],
    size_t max_sentences
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_TTS_PIPELINE_H */

