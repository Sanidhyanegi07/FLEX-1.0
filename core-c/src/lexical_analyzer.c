#include "lexical_analyzer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char* const S_C_KEYWORDS[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
    "void", "volatile", "while", "_Bool", "_Complex", NULL
};

static const char* const S_PY_KEYWORDS[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue",
    "def", "del", "elif", "else", "except", "finally", "for", "from",
    "global", "if", "import", "in", "is", "lambda", "nonlocal", "not",
    "or", "pass", "raise", "return", "try", "while", "with", "yield",
    "True", "False", "None", NULL
};

static const char* const S_SQL_KEYWORDS[] = {
    "select", "from", "where", "insert", "into", "update", "delete",
    "create", "table", "drop", "alter", "join", "inner", "left", "right",
    "outer", "on", "group", "by", "order", "asc", "desc", "having",
    "limit", "offset", "primary", "key", "foreign", "references", "null",
    "not", "and", "or", "values", "distinct", "union", "all", NULL
};

FlexCodeLanguage flex_lex_detect_language(const char* tag) {
    if (!tag || tag[0] == '\0') return FLEX_LANG_UNKNOWN;
    
    char lower[32];
    size_t i = 0;
    for (; tag[i] && i + 1 < sizeof(lower); ++i) {
        lower[i] = (char)tolower((unsigned char)tag[i]);
    }
    lower[i] = '\0';
    
    if (strcmp(lower, "c") == 0 || strcmp(lower, "cpp") == 0 || strcmp(lower, "c++") == 0 || strcmp(lower, "h") == 0) {
        return FLEX_LANG_C;
    }
    if (strcmp(lower, "python") == 0 || strcmp(lower, "py") == 0) {
        return FLEX_LANG_PYTHON;
    }
    if (strcmp(lower, "sql") == 0 || strcmp(lower, "sqlite") == 0) {
        return FLEX_LANG_SQL;
    }
    if (strcmp(lower, "json") == 0) {
        return FLEX_LANG_JSON;
    }
    return FLEX_LANG_UNKNOWN;
}

bool flex_lex_is_keyword(FlexCodeLanguage lang, const char* word) {
    if (!word || word[0] == '\0') return false;
    
    char lower[64];
    size_t i = 0;
    for (; word[i] && i + 1 < sizeof(lower); ++i) {
        lower[i] = (char)tolower((unsigned char)word[i]);
    }
    lower[i] = '\0';
    
    const char* const* table = NULL;
    switch (lang) {
        case FLEX_LANG_C:      table = S_C_KEYWORDS; break;
        case FLEX_LANG_PYTHON: table = S_PY_KEYWORDS; break;
        case FLEX_LANG_SQL:    table = S_SQL_KEYWORDS; break;
        default: return false;
    }
    
    for (size_t k = 0; table[k] != NULL; ++k) {
        if (strcmp(lower, table[k]) == 0) {
            return true;
        }
    }
    return false;
}

FlexBracketBalance flex_lex_check_brackets(const char* code, size_t len) {
    FlexBracketBalance bal = {0, 0, 0, true};
    if (!code || len == 0) return bal;
    
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    
    for (size_t i = 0; i < len; ++i) {
        char c = code[i];
        
        /* Line comment end */
        if (in_line_comment) {
            if (c == '\n') in_line_comment = false;
            continue;
        }
        
        /* Block comment end */
        if (in_block_comment) {
            if (c == '*' && i + 1 < len && code[i + 1] == '/') {
                in_block_comment = false;
                i++;
            }
            continue;
        }
        
        /* String literals */
        if (in_double_quote) {
            if (c == '"' && (i == 0 || code[i - 1] != '\\')) {
                in_double_quote = false;
            }
            continue;
        }
        if (in_single_quote) {
            if (c == '\'' && (i == 0 || code[i - 1] != '\\')) {
                in_single_quote = false;
            }
            continue;
        }
        
        /* Check comments */
        if (c == '/' && i + 1 < len) {
            if (code[i + 1] == '/') {
                in_line_comment = true;
                i++;
                continue;
            } else if (code[i + 1] == '*') {
                in_block_comment = true;
                i++;
                continue;
            }
        }
        
        /* Check quotes */
        if (c == '"') { in_double_quote = true; continue; }
        if (c == '\'') { in_single_quote = true; continue; }
        
        /* Count brackets */
        if (c == '(') bal.round_brackets++;
        else if (c == ')') bal.round_brackets--;
        else if (c == '{') bal.curly_braces++;
        else if (c == '}') bal.curly_braces--;
        else if (c == '[') bal.square_brackets++;
        else if (c == ']') bal.square_brackets--;
        
        if (bal.round_brackets < 0 || bal.curly_braces < 0 || bal.square_brackets < 0) {
            bal.is_balanced = false;
        }
    }
    
    if (bal.round_brackets != 0 || bal.curly_braces != 0 || bal.square_brackets != 0) {
        bal.is_balanced = false;
    }
    
    return bal;
}

size_t flex_lex_format_line_numbers(const char* code,
                                    size_t code_len,
                                    char* out_buf,
                                    size_t out_capacity) {
    if (!code || !out_buf || out_capacity == 0) return 0;
    
    out_buf[0] = '\0';
    size_t written = 0;
    int line_num = 1;
    
    size_t line_start = 0;
    for (size_t i = 0; i <= code_len; ++i) {
        if (i == code_len || code[i] == '\n') {
            size_t seg_len = i - line_start;
            
            char line_prefix[32];
            int prefix_len = snprintf(line_prefix, sizeof(line_prefix), "%3d | ", line_num);
            if (prefix_len > 0 && written + (size_t)prefix_len < out_capacity) {
                memcpy(&out_buf[written], line_prefix, (size_t)prefix_len);
                written += (size_t)prefix_len;
            }
            
            if (seg_len > 0 && written + seg_len < out_capacity) {
                memcpy(&out_buf[written], &code[line_start], seg_len);
                written += seg_len;
            }
            
            if (written + 1 < out_capacity) {
                out_buf[written++] = '\n';
            }
            
            line_num++;
            line_start = i + 1;
        }
    }
    
    out_buf[written] = '\0';
    return written;
}

