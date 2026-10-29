#include "math_evaluator.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    const char* str;
    size_t pos;
    size_t len;
    FlexMathResult result;
} MathParser;

static void skip_ws(MathParser* p) {
    while (p->pos < p->len && isspace((unsigned char)p->str[p->pos])) {
        p->pos++;
    }
}

static char peek(MathParser* p) {
    skip_ws(p);
    if (p->pos >= p->len) return '\0';
    return p->str[p->pos];
}

static char get_char(MathParser* p) {
    skip_ws(p);
    if (p->pos >= p->len) return '\0';
    return p->str[p->pos++];
}

static double parse_expression(MathParser* p);

static double parse_primary(MathParser* p) {
    skip_ws(p);
    char c = peek(p);
    
    /* Unary plus / minus */
    if (c == '+') {
        get_char(p);
        return parse_primary(p);
    }
    if (c == '-') {
        get_char(p);
        return -parse_primary(p);
    }
    
    /* Parentheses */
    if (c == '(') {
        get_char(p);
        double val = parse_expression(p);
        if (p->result.status != FLEX_MATH_OK) return 0.0;
        if (peek(p) == ')') {
            get_char(p);
            return val;
        } else {
            p->result.status = FLEX_MATH_ERR_UNMATCHED_PAREN;
            p->result.error_pos = p->pos;
            snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Expected ')'");
            return 0.0;
        }
    }
    
    /* Functions or numbers */
    if (isalpha((unsigned char)c)) {
        char fn[32];
        size_t fn_len = 0;
        while (p->pos < p->len && isalpha((unsigned char)p->str[p->pos])) {
            if (fn_len + 1 < sizeof(fn)) {
                fn[fn_len++] = (char)tolower((unsigned char)p->str[p->pos]);
            }
            p->pos++;
        }
        fn[fn_len] = '\0';
        
        if (peek(p) != '(') {
            p->result.status = FLEX_MATH_ERR_SYNTAX;
            p->result.error_pos = p->pos;
            snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Expected '(' after function '%s'", fn);
            return 0.0;
        }
        get_char(p); /* '(' */
        
        double arg1 = parse_expression(p);
        if (p->result.status != FLEX_MATH_OK) return 0.0;
        
        double arg2 = 0.0;
        bool has_arg2 = false;
        if (peek(p) == ',') {
            get_char(p);
            has_arg2 = true;
            arg2 = parse_expression(p);
            if (p->result.status != FLEX_MATH_OK) return 0.0;
        }
        
        if (peek(p) != ')') {
            p->result.status = FLEX_MATH_ERR_UNMATCHED_PAREN;
            p->result.error_pos = p->pos;
            snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Expected ')' after function arguments");
            return 0.0;
        }
        get_char(p); /* ')' */
        
        if (strcmp(fn, "sqrt") == 0) {
            if (arg1 < 0.0) {
                p->result.status = FLEX_MATH_ERR_DOMAIN;
                snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Square root of negative number");
                return 0.0;
            }
            return sqrt(arg1);
        } else if (strcmp(fn, "abs") == 0) {
            return fabs(arg1);
        } else if (strcmp(fn, "round") == 0) {
            return round(arg1);
        } else if (strcmp(fn, "floor") == 0) {
            return floor(arg1);
        } else if (strcmp(fn, "ceil") == 0) {
            return ceil(arg1);
        } else if (strcmp(fn, "pow") == 0) {
            if (!has_arg2) {
                p->result.status = FLEX_MATH_ERR_SYNTAX;
                snprintf(p->result.error_msg, sizeof(p->result.error_msg), "pow() requires 2 arguments");
                return 0.0;
            }
            return pow(arg1, arg2);
        } else if (strcmp(fn, "min") == 0) {
            if (!has_arg2) return arg1;
            return arg1 < arg2 ? arg1 : arg2;
        } else if (strcmp(fn, "max") == 0) {
            if (!has_arg2) return arg1;
            return arg1 > arg2 ? arg1 : arg2;
        } else {
            p->result.status = FLEX_MATH_ERR_SYNTAX;
            p->result.error_pos = p->pos;
            snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Unknown function '%s'", fn);
            return 0.0;
        }
    }
    
    /* Number parsing */
    if (isdigit((unsigned char)c) || c == '.') {
        char* endptr = NULL;
        double val = strtod(&p->str[p->pos], &endptr);
        if (endptr != &p->str[p->pos]) {
            p->pos = (size_t)(endptr - p->str);
            return val;
        }
    }
    
    p->result.status = FLEX_MATH_ERR_SYNTAX;
    p->result.error_pos = p->pos;
    snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Unexpected character '%c'", c ? c : ' ');
    return 0.0;
}

static double parse_factor(MathParser* p) {
    double base = parse_primary(p);
    if (p->result.status != FLEX_MATH_OK) return 0.0;
    
    while (peek(p) == '^') {
        get_char(p);
        double exp = parse_primary(p);
        if (p->result.status != FLEX_MATH_OK) return 0.0;
        base = pow(base, exp);
    }
    return base;
}

static double parse_term(MathParser* p) {
    double val = parse_factor(p);
    if (p->result.status != FLEX_MATH_OK) return 0.0;
    
    while (true) {
        char op = peek(p);
        if (op == '*' || op == '/' || op == '%') {
            get_char(p);
            double next = parse_factor(p);
            if (p->result.status != FLEX_MATH_OK) return 0.0;
            
            if (op == '*') {
                val *= next;
            } else if (op == '/') {
                if (fabs(next) < 1e-12) {
                    p->result.status = FLEX_MATH_ERR_DIVISION_BY_ZERO;
                    snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Division by zero");
                    return 0.0;
                }
                val /= next;
            } else if (op == '%') {
                if (fabs(next) < 1e-12) {
                    p->result.status = FLEX_MATH_ERR_DIVISION_BY_ZERO;
                    snprintf(p->result.error_msg, sizeof(p->result.error_msg), "Modulo by zero");
                    return 0.0;
                }
                val = fmod(val, next);
            }
        } else {
            break;
        }
    }
    return val;
}

static double parse_expression(MathParser* p) {
    double val = parse_term(p);
    if (p->result.status != FLEX_MATH_OK) return 0.0;
    
    while (true) {
        char op = peek(p);
        if (op == '+' || op == '-') {
            get_char(p);
            double next = parse_term(p);
            if (p->result.status != FLEX_MATH_OK) return 0.0;
            
            if (op == '+') {
                val += next;
            } else {
                val -= next;
            }
        } else {
            break;
        }
    }
    return val;
}

FlexMathResult flex_math_evaluate(const char* expr) {
    FlexMathResult res;
    memset(&res, 0, sizeof(res));
    res.status = FLEX_MATH_OK;
    
    if (!expr) {
        res.status = FLEX_MATH_ERR_NULL_EXPR;
        snprintf(res.error_msg, sizeof(res.error_msg), "Expression is null");
        return res;
    }
    
    size_t len = strlen(expr);
    if (len == 0) {
        res.status = FLEX_MATH_ERR_EMPTY_EXPR;
        snprintf(res.error_msg, sizeof(res.error_msg), "Expression is empty");
        return res;
    }
    
    MathParser p;
    p.str = expr;
    p.pos = 0;
    p.len = len;
    p.result = res;
    
    double val = parse_expression(&p);
    
    if (p.result.status == FLEX_MATH_OK) {
        skip_ws(&p);
        if (p.pos < p.len) {
            p.result.status = FLEX_MATH_ERR_SYNTAX;
            p.result.error_pos = p.pos;
            snprintf(p.result.error_msg, sizeof(p.result.error_msg), "Trailing characters at end of expression");
        } else {
            p.result.result = val;
        }
    }
    
    return p.result;
}

size_t flex_math_format_result(const FlexMathResult* res, char* out_buf, size_t out_capacity) {
    if (!res || !out_buf || out_capacity == 0) return 0;
    
    if (res->status == FLEX_MATH_OK) {
        /* Format cleanly: if integer value, omit trailing zeros */
        if (fabs(res->result - round(res->result)) < 1e-6) {
            return (size_t)snprintf(out_buf, out_capacity, "%.0f", res->result);
        } else {
            return (size_t)snprintf(out_buf, out_capacity, "%.4g", res->result);
        }
    } else {
        return (size_t)snprintf(out_buf, out_capacity, "Error: %s", res->error_msg);
    }
}

