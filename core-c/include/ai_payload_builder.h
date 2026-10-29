#ifndef FLEX_AI_PAYLOAD_BUILDER_H
#define FLEX_AI_PAYLOAD_BUILDER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature;
    float top_p;
    int num_predict;
    bool stream;
} flex_model_options_t;

/**
 * Initializes default model options.
 */
void flex_model_options_init_defaults(flex_model_options_t* opts);

/**
 * Serializes a complete Ollama /api/chat JSON request payload.
 * Takes system_prompt (optional), user_prompt, model_name, and model options.
 * Returns bytes written, or 0 if out_cap is insufficient.
 */
size_t flex_build_ollama_chat_payload(
    const char* model_name,
    const char* system_prompt,
    const char* user_prompt,
    const flex_model_options_t* options,
    char* out_buf,
    size_t buf_cap
);

#ifdef __cplusplus
}
#endif

#endif /* FLEX_AI_PAYLOAD_BUILDER_H */

