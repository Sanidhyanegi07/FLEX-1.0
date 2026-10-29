#include "ai_payload_builder.h"
#include <string.h>
#include <stdio.h>

void flex_model_options_init_defaults(flex_model_options_t* opts) {
    if (!opts) return;
    opts->temperature = 0.7f;
    opts->top_p = 0.9f;
    opts->num_predict = 2048;
    opts->stream = true;
}

static size_t append_escaped_json(const char* in, char* out, size_t out_cap, size_t cur_len) {
    if (!in || !out || cur_len >= out_cap) return cur_len;

    for (size_t i = 0; in[i] && cur_len + 3 < out_cap; ++i) {
        char c = in[i];
        if (c == '"') {
            out[cur_len++] = '\\';
            out[cur_len++] = '"';
        } else if (c == '\\') {
            out[cur_len++] = '\\';
            out[cur_len++] = '\\';
        } else if (c == '\n') {
            out[cur_len++] = '\\';
            out[cur_len++] = 'n';
        } else if (c == '\r') {
            out[cur_len++] = '\\';
            out[cur_len++] = 'r';
        } else if (c == '\t') {
            out[cur_len++] = '\\';
            out[cur_len++] = 't';
        } else if ((unsigned char)c >= 32) {
            out[cur_len++] = c;
        }
    }
    out[cur_len] = '\0';
    return cur_len;
}

size_t flex_build_ollama_chat_payload(
    const char* model_name,
    const char* system_prompt,
    const char* user_prompt,
    const flex_model_options_t* options,
    char* out_buf,
    size_t buf_cap
) {
    if (!model_name || !user_prompt || !out_buf || buf_cap < 64) return 0;

    flex_model_options_t default_opts;
    if (!options) {
        flex_model_options_init_defaults(&default_opts);
        options = &default_opts;
    }

    int written = snprintf(
        out_buf, buf_cap,
        "{\"model\":\"%s\",\"stream\":%s,\"messages\":[",
        model_name,
        options->stream ? "true" : "false"
    );

    if (written < 0 || (size_t)written >= buf_cap) return 0;
    size_t cur = (size_t)written;

    /* Add system prompt if present */
    if (system_prompt && *system_prompt != '\0') {
        const char sys_prefix[] = "{\"role\":\"system\",\"content\":\"";
        size_t pref_len = sizeof(sys_prefix) - 1;
        if (cur + pref_len >= buf_cap) return 0;
        memcpy(out_buf + cur, sys_prefix, pref_len);
        cur += pref_len;

        cur = append_escaped_json(system_prompt, out_buf, buf_cap, cur);

        const char sys_suffix[] = "\"},";
        size_t suff_len = sizeof(sys_suffix) - 1;
        if (cur + suff_len >= buf_cap) return 0;
        memcpy(out_buf + cur, sys_suffix, suff_len);
        cur += suff_len;
    }

    /* Add user prompt */
    const char user_prefix[] = "{\"role\":\"user\",\"content\":\"";
    size_t user_pref_len = sizeof(user_prefix) - 1;
    if (cur + user_pref_len >= buf_cap) return 0;
    memcpy(out_buf + cur, user_prefix, user_pref_len);
    cur += user_pref_len;

    cur = append_escaped_json(user_prompt, out_buf, buf_cap, cur);

    const char user_suffix[] = "\"}],";
    size_t user_suff_len = sizeof(user_suffix) - 1;
    if (cur + user_suff_len >= buf_cap) return 0;
    memcpy(out_buf + cur, user_suffix, user_suff_len);
    cur += user_suff_len;

    /* Add options */
    written = snprintf(
        out_buf + cur, buf_cap - cur,
        "\"options\":{\"temperature\":%.2f,\"top_p\":%.2f,\"num_predict\":%d}}",
        options->temperature,
        options->top_p,
        options->num_predict
    );

    if (written < 0 || (size_t)written >= buf_cap - cur) return 0;
    cur += (size_t)written;

    return cur;
}

