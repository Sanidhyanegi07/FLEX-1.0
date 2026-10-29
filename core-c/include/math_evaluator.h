#ifndef FLEX_MATH_EVALUATOR_H
#define FLEX_MATH_EVALUATOR_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLEX_MATH_OK = 0,
    FLEX_MATH_ERR_NULL_EXPR,
    FLEX_MATH_ERR_EMPTY_EXPR,
    FLEX_MATH_ERR_SYNTAX,
    FLEX_MATH_ERR_UNMATCHED_PAREN,
    FLEX_MATH_ERR_DIVISION_BY_ZERO,
    FLEX_MATH_ERR_DOMAIN,
    FLEX_MATH_ERR_OVERFLOW
} FlexMathStatus;

typedef struct {
    FlexMathStatus status;
    double result;
    char error_msg[64];
    size_t error_pos;
} FlexMathResult;

/**
 * Evaluate an arithmetic expression string (e.g. "(12.5 + 7.5) * 2 - sqrt(16)").
 */
FlexMathResult flex_math_evaluate(const char* expr);

/**
 * Format evaluation result into a human-readable string (e.g. "40.0" or "Error: division by zero").
 */
size_t flex_math_format_result(const FlexMathResult* res, char* out_buf, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_MATH_EVALUATOR_H */

