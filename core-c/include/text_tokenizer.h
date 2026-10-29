#ifndef FLEX_TEXT_TOKENIZER_H
#define FLEX_TEXT_TOKENIZER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_TOKENIZER_MAX_TOKEN_LEN 64
#define FLEX_TOKENIZER_MAX_TOKENS    256
#define FLEX_TOKENIZER_MAX_VOCAB     128

typedef enum {
    FLEX_TOKEN_WORD = 0,
    FLEX_TOKEN_NUMBER,
    FLEX_TOKEN_PUNCTUATION,
    FLEX_TOKEN_SYMBOL,
    FLEX_TOKEN_WHITESPACE,
    FLEX_TOKEN_UNKNOWN
} FlexTokenType;

typedef struct {
    char text[FLEX_TOKENIZER_MAX_TOKEN_LEN];
    FlexTokenType type;
    size_t start_offset;
    size_t length;
} FlexToken;

typedef struct {
    FlexToken tokens[FLEX_TOKENIZER_MAX_TOKENS];
    size_t count;
    size_t total_chars;
} FlexTokenSequence;

typedef struct {
    char term[FLEX_TOKENIZER_MAX_TOKEN_LEN];
    size_t count;
} FlexVocabEntry;

typedef struct {
    FlexVocabEntry entries[FLEX_TOKENIZER_MAX_VOCAB];
    size_t count;
} FlexVocabTable;

/**
 * Tokenize input text into discrete tokens (words, numbers, punctuation).
 * Bounded by max tokens and buffer safety.
 */
bool flex_tokenizer_tokenize(const char* text, size_t text_len, FlexTokenSequence* out_seq);

/**
 * Estimate LLM token count using conservative BPE byte-pair heuristic
 * (avg ~3.8 chars per token for English text/code).
 */
size_t flex_tokenizer_estimate_llm_tokens(const char* text, size_t text_len);

/**
 * Check if a word token is an English stopword (e.g., "the", "is", "at", etc.).
 */
bool flex_tokenizer_is_stopword(const char* word);

/**
 * Build vocabulary frequency table from token sequence.
 */
bool flex_tokenizer_build_vocab(const FlexTokenSequence* seq, FlexVocabTable* out_vocab);

/**
 * Look up frequency count of a term in the vocabulary table.
 */
size_t flex_tokenizer_vocab_lookup(const FlexVocabTable* vocab, const char* term);

/**
 * Filter token sequence by removing punctuation and stopwords, preserving meaningful content.
 */
size_t flex_tokenizer_filter_content_words(const FlexTokenSequence* src,
                                           FlexTokenSequence* dst);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_TEXT_TOKENIZER_H */

