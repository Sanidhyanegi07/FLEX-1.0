#ifndef FLEX_PROMPT_TEMPLATE_H
#define FLEX_PROMPT_TEMPLATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_MAX_SUGGESTION_CHIPS 8
#define FLEX_CHIP_TEXT_MAX 128
#define FLEX_CHIP_TAG_MAX 32

typedef enum {
    FLEX_PROMPT_MODE_DEFAULT = 0,
    FLEX_PROMPT_MODE_C_TUTOR = 1,
    FLEX_PROMPT_MODE_VOICE_CONCISE = 2,
    FLEX_PROMPT_MODE_PBL_DEMO = 3
} flex_prompt_mode_t;

typedef struct {
    char tag[FLEX_CHIP_TAG_MAX];
    char prompt_text[FLEX_CHIP_TEXT_MAX];
} flex_suggestion_chip_t;

/**
 * Returns the system prompt string for the requested assistant mode.
 */
const char* flex_get_system_prompt(flex_prompt_mode_t mode);

/**
 * Retrieves the standard suggestion chips for the welcome screen.
 * Fills out_chips up to max_chips, returns number of chips retrieved.
 */
size_t flex_get_welcome_suggestion_chips(
    flex_suggestion_chip_t* out_chips,
    size_t max_chips
);

/**
 * Substitutes variables like {{model}}, {{os}}, {{version}} in template_str.
 */
size_t flex_substitute_template_vars(
    const char* template_str,
    const char* model,
    const char* os_name,
    const char* version,
    char* out_buf,
    size_t buf_cap
);

/**
 * Returns string name of prompt mode.
 */
const char* flex_prompt_mode_name(flex_prompt_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_PROMPT_TEMPLATE_H */

