#ifndef FLEX_LEXICAL_ANALYZER_H
#define FLEX_LEXICAL_ANALYZER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_LEX_MAX_LINE_LEN 256

typedef enum {
    FLEX_LANG_UNKNOWN = 0,
    FLEX_LANG_C,
    FLEX_LANG_PYTHON,
    FLEX_LANG_SQL,
    FLEX_LANG_JSON
} FlexCodeLanguage;

typedef enum {
    FLEX_SYNATX_KEYWORD = 0,
    FLEX_SYNTAX_IDENTIFIER,
    FLEX_SYNTAX_STRING,
    FLEX_SYNTAX_NUMBER,
    FLEX_SYNTAX_COMMENT,
    FLEX_SYNTAX_OPERATOR,
    FLEX_SYNTAX_OTHER
} FlexSyntaxTokenType;

typedef struct {
    int round_brackets;   /* ( ) balance */
    int curly_braces;     /* { } balance */
    int square_brackets;  /* [ ] balance */
    bool is_balanced;
} FlexBracketBalance;

/**
 * Detect language from markdown fence tag (e.g. "c", "cpp", "python", "py", "sql", "json").
 */
FlexCodeLanguage flex_lex_detect_language(const char* tag);

/**
 * Check if word is a keyword in the specified language.
 */
bool flex_lex_is_keyword(FlexCodeLanguage lang, const char* word);

/**
 * Check bracket balance across source code snippet.
 */
FlexBracketBalance flex_lex_check_brackets(const char* code, size_t len);

/**
 * Add line-number prefixes to a multi-line code string (e.g., "  1 | int x = 0;\n").
 * Bounded by out_buf capacity.
 */
size_t flex_lex_format_line_numbers(const char* code,
                                    size_t code_len,
                                    char* out_buf,
                                    size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_LEXICAL_ANALYZER_H */

