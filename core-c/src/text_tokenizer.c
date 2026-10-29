#include "text_tokenizer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char* const S_STOPWORDS[] = {
    "a", "about", "above", "after", "again", "against", "all", "am", "an", "and",
    "any", "are", "aren't", "as", "at", "be", "because", "been", "before", "being",
    "below", "between", "both", "but", "by", "can't", "cannot", "could", "couldn't",
    "did", "didn't", "do", "does", "doesn't", "doing", "don't", "down", "during",
    "each", "few", "for", "from", "further", "had", "hadn't", "has", "hasn't",
    "have", "haven't", "having", "he", "he'd", "he'll", "he's", "her", "here",
    "here's", "hers", "herself", "him", "himself", "his", "how", "how's", "i",
    "i'd", "i'll", "i'm", "i've", "if", "in", "into", "is", "isn't", "it", "it's",
    "its", "itself", "let's", "me", "more", "most", "mustn't", "my", "myself",
    "no", "nor", "not", "of", "off", "on", "once", "only", "or", "other", "ought",
    "our", "ours", "ourselves", "out", "over", "own", "same", "shan't", "she",
    "she'd", "she'll", "she's", "should", "shouldn't", "so", "some", "such",
    "than", "that", "that's", "the", "their", "theirs", "them", "themselves",
    "then", "there", "there's", "these", "they", "they'd", "they'll", "they're",
    "they've", "this", "those", "through", "to", "too", "under", "until", "up",
    "very", "was", "wasn't", "we", "we'd", "we'll", "we're", "we've", "were",
    "weren't", "what", "what's", "when", "when's", "where", "where's", "which",
    "while", "who", "who's", "whom", "why", "why's", "with", "won't", "would",
    "wouldn't", "you", "you'd", "you'll", "you're", "you've", "your", "yours",
    "yourself", "yourselves", NULL
};

static FlexTokenType classify_char(char c) {
    if (isalpha((unsigned char)c) || c == '_') return FLEX_TOKEN_WORD;
    if (isdigit((unsigned char)c)) return FLEX_TOKEN_NUMBER;
    if (isspace((unsigned char)c)) return FLEX_TOKEN_WHITESPACE;
    if (ispunct((unsigned char)c)) return FLEX_TOKEN_PUNCTUATION;
    return FLEX_TOKEN_SYMBOL;
}

bool flex_tokenizer_tokenize(const char* text, size_t text_len, FlexTokenSequence* out_seq) {
    if (!text || !out_seq) return false;
    
    out_seq->count = 0;
    out_seq->total_chars = text_len;
    
    size_t i = 0;
    while (i < text_len && out_seq->count < FLEX_TOKENIZER_MAX_TOKENS) {
        /* Skip leading whitespace */
        while (i < text_len && isspace((unsigned char)text[i])) {
            i++;
        }
        if (i >= text_len) break;
        
        size_t token_start = i;
        FlexTokenType type = classify_char(text[i]);
        
        size_t len = 0;
        if (type == FLEX_TOKEN_WORD) {
            while (i < text_len && (isalnum((unsigned char)text[i]) || text[i] == '_' || text[i] == '-')) {
                if (len + 1 < FLEX_TOKENIZER_MAX_TOKEN_LEN) {
                    out_seq->tokens[out_seq->count].text[len++] = text[i];
                }
                i++;
            }
        } else if (type == FLEX_TOKEN_NUMBER) {
            while (i < text_len && (isdigit((unsigned char)text[i]) || text[i] == '.' || text[i] == 'x' || text[i] == 'X')) {
                if (len + 1 < FLEX_TOKENIZER_MAX_TOKEN_LEN) {
                    out_seq->tokens[out_seq->count].text[len++] = text[i];
                }
                i++;
            }
        } else if (type == FLEX_TOKEN_PUNCTUATION) {
            /* Single character punctuation or digraphs like ==, !=, <=, >=, -> */
            out_seq->tokens[out_seq->count].text[len++] = text[i++];
            if (i < text_len && ispunct((unsigned char)text[i])) {
                char next = text[i];
                char prev = out_seq->tokens[out_seq->count].text[0];
                if ((prev == '=' && next == '=') ||
                    (prev == '!' && next == '=') ||
                    (prev == '<' && next == '=') ||
                    (prev == '>' && next == '=') ||
                    (prev == '-' && next == '>') ||
                    (prev == '&' && next == '&') ||
                    (prev == '|' && next == '|')) {
                    if (len + 1 < FLEX_TOKENIZER_MAX_TOKEN_LEN) {
                        out_seq->tokens[out_seq->count].text[len++] = next;
                    }
                    i++;
                }
            }
        } else {
            out_seq->tokens[out_seq->count].text[len++] = text[i++];
        }
        
        out_seq->tokens[out_seq->count].text[len] = '\0';
        out_seq->tokens[out_seq->count].type = type;
        out_seq->tokens[out_seq->count].start_offset = token_start;
        out_seq->tokens[out_seq->count].length = len;
        out_seq->count++;
    }
    
    return true;
}

size_t flex_tokenizer_estimate_llm_tokens(const char* text, size_t text_len) {
    if (!text || text_len == 0) return 0;
    
    /* Byte-pair encoding conservative estimate:
     * Words: ~1 token per word + extra for longer words or punctuation
     * Formula: text_len / 4 + 1 for baseline text, plus penalty for symbols/code
     */
    size_t words = 0;
    size_t symbols = 0;
    bool in_word = false;
    
    for (size_t i = 0; i < text_len; ++i) {
        char c = text[i];
        if (isalnum((unsigned char)c)) {
            if (!in_word) {
                words++;
                in_word = true;
            }
        } else {
            in_word = false;
            if (ispunct((unsigned char)c)) {
                symbols++;
            }
        }
    }
    
    /* Estimate: words + extra tokens for long text + symbols / 2 */
    size_t est = words + (symbols + 1) / 2;
    if (est == 0 && text_len > 0) est = 1;
    return est;
}

bool flex_tokenizer_is_stopword(const char* word) {
    if (!word || word[0] == '\0') return false;
    
    char lower[FLEX_TOKENIZER_MAX_TOKEN_LEN];
    size_t i = 0;
    for (; word[i] && i + 1 < sizeof(lower); ++i) {
        lower[i] = (char)tolower((unsigned char)word[i]);
    }
    lower[i] = '\0';
    
    for (size_t k = 0; S_STOPWORDS[k] != NULL; ++k) {
        if (strcmp(lower, S_STOPWORDS[k]) == 0) {
            return true;
        }
    }
    return false;
}

bool flex_tokenizer_build_vocab(const FlexTokenSequence* seq, FlexVocabTable* out_vocab) {
    if (!seq || !out_vocab) return false;
    
    out_vocab->count = 0;
    
    for (size_t i = 0; i < seq->count; ++i) {
        const FlexToken* tok = &seq->tokens[i];
        if (tok->type != FLEX_TOKEN_WORD) continue;
        
        char lower[FLEX_TOKENIZER_MAX_TOKEN_LEN];
        size_t k = 0;
        for (; tok->text[k] && k + 1 < sizeof(lower); ++k) {
            lower[k] = (char)tolower((unsigned char)tok->text[k]);
        }
        lower[k] = '\0';
        
        /* Search in existing table */
        bool found = false;
        for (size_t v = 0; v < out_vocab->count; ++v) {
            if (strcmp(out_vocab->entries[v].term, lower) == 0) {
                out_vocab->entries[v].count++;
                found = true;
                break;
            }
        }
        
        if (!found && out_vocab->count < FLEX_TOKENIZER_MAX_VOCAB) {
            strncpy(out_vocab->entries[out_vocab->count].term, lower, FLEX_TOKENIZER_MAX_TOKEN_LEN - 1);
            out_vocab->entries[out_vocab->count].term[FLEX_TOKENIZER_MAX_TOKEN_LEN - 1] = '\0';
            out_vocab->entries[out_vocab->count].count = 1;
            out_vocab->count++;
        }
    }
    
    return true;
}

size_t flex_tokenizer_vocab_lookup(const FlexVocabTable* vocab, const char* term) {
    if (!vocab || !term) return 0;
    
    char lower[FLEX_TOKENIZER_MAX_TOKEN_LEN];
    size_t i = 0;
    for (; term[i] && i + 1 < sizeof(lower); ++i) {
        lower[i] = (char)tolower((unsigned char)term[i]);
    }
    lower[i] = '\0';
    
    for (size_t v = 0; v < vocab->count; ++v) {
        if (strcmp(vocab->entries[v].term, lower) == 0) {
            return vocab->entries[v].count;
        }
    }
    return 0;
}

size_t flex_tokenizer_filter_content_words(const FlexTokenSequence* src,
                                           FlexTokenSequence* dst) {
    if (!src || !dst) return 0;
    
    dst->count = 0;
    dst->total_chars = 0;
    
    for (size_t i = 0; i < src->count && dst->count < FLEX_TOKENIZER_MAX_TOKENS; ++i) {
        const FlexToken* tok = &src->tokens[i];
        if (tok->type == FLEX_TOKEN_WORD && !flex_tokenizer_is_stopword(tok->text)) {
            dst->tokens[dst->count] = *tok;
            dst->total_chars += tok->length;
            dst->count++;
        }
    }
    
    return dst->count;
}

