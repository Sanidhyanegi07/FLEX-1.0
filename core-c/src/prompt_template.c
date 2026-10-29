#include "prompt_template.h"
#include <string.h>
#include <stdio.h>

static const char S_DEFAULT_SYSTEM_PROMPT[] =
    "You are FLEX 1.0, an intelligent, deterministic desktop AI assistant engineered in strict C11 and C++17 with Qt 6. "
    "You operate completely locally on the user's workstation with zero cloud dependencies. "
    "Provide clear, technically precise, and concise answers. When writing code, prefer memory-safe, portable idioms.";

static const char S_C_TUTOR_SYSTEM_PROMPT[] =
    "You are the FLEX 1.0 C Systems Programming Tutor. You specialize in strict C11 standard idioms, zero-heap memory management, "
    "pointer arithmetic, memory alignment, cache efficiency, POSIX/Win32 systems programming, and deterministic state machines. "
    "Explain low-level concepts with clear, bounded C examples.";

static const char S_VOICE_CONCISE_SYSTEM_PROMPT[] =
    "You are in Voice Interaction Mode for FLEX 1.0. The user is speaking to you and listening to your response via Text-To-Speech. "
    "Keep answers conversational, clear, natural, and under 3 sentences unless explicitly asked for detailed code.";

static const char S_PBL_DEMO_SYSTEM_PROMPT[] =
    "You are demonstrating FLEX 1.0 for the College Project-Based Learning (PBL) final defense. "
    "Highlight the deterministic C11 core, bounded input buffers (1024B max), zero-allocation parser, and offline whisper.cpp voice pipeline.";

const char* flex_get_system_prompt(flex_prompt_mode_t mode) {
    switch (mode) {
        case FLEX_PROMPT_MODE_C_TUTOR: return S_C_TUTOR_SYSTEM_PROMPT;
        case FLEX_PROMPT_MODE_VOICE_CONCISE: return S_VOICE_CONCISE_SYSTEM_PROMPT;
        case FLEX_PROMPT_MODE_PBL_DEMO: return S_PBL_DEMO_SYSTEM_PROMPT;
        case FLEX_PROMPT_MODE_DEFAULT:
        default:
            return S_DEFAULT_SYSTEM_PROMPT;
    }
}

size_t flex_get_welcome_suggestion_chips(
    flex_suggestion_chip_t* out_chips,
    size_t max_chips
) {
    if (!out_chips || max_chips == 0) return 0;

    static const flex_suggestion_chip_t chips[] = {
        { "Pointers", "Explain pointer arithmetic and memory layout in C11." },
        { "Systems", "Show me a zero-allocation command parser in C." },
        { "Voice", "How does local offline Whisper speech-to-text inference work?" },
        { "Telemetry", "Inspect active C Core lifecycle and runtime telemetry." }
    };

    size_t count = sizeof(chips) / sizeof(chips[0]);
    if (count > max_chips) count = max_chips;

    for (size_t i = 0; i < count; ++i) {
        out_chips[i] = chips[i];
    }
    return count;
}

size_t flex_substitute_template_vars(
    const char* template_str,
    const char* model,
    const char* os_name,
    const char* version,
    char* out_buf,
    size_t buf_cap
) {
    if (!template_str || !out_buf || buf_cap < 2) return 0;

    const char* m = model ? model : "qwen2.5-coder:3b";
    const char* o = os_name ? os_name : "Windows x64";
    const char* v = version ? version : "1.0.0";

    size_t out_idx = 0;
    const char* p = template_str;

    while (*p && out_idx + 1 < buf_cap) {
        if (strncmp(p, "{{model}}", 9) == 0) {
            size_t len = strlen(m);
            for (size_t i = 0; i < len && out_idx + 1 < buf_cap; ++i) {
                out_buf[out_idx++] = m[i];
            }
            p += 9;
        } else if (strncmp(p, "{{os}}", 6) == 0) {
            size_t len = strlen(o);
            for (size_t i = 0; i < len && out_idx + 1 < buf_cap; ++i) {
                out_buf[out_idx++] = o[i];
            }
            p += 6;
        } else if (strncmp(p, "{{version}}", 11) == 0) {
            size_t len = strlen(v);
            for (size_t i = 0; i < len && out_idx + 1 < buf_cap; ++i) {
                out_buf[out_idx++] = v[i];
            }
            p += 11;
        } else {
            out_buf[out_idx++] = *p++;
        }
    }

    out_buf[out_idx] = '\0';
    return out_idx;
}

const char* flex_prompt_mode_name(flex_prompt_mode_t mode) {
    switch (mode) {
        case FLEX_PROMPT_MODE_C_TUTOR: return "C_TUTOR";
        case FLEX_PROMPT_MODE_VOICE_CONCISE: return "VOICE_CONCISE";
        case FLEX_PROMPT_MODE_PBL_DEMO: return "PBL_DEMO";
        case FLEX_PROMPT_MODE_DEFAULT:
        default:
            return "DEFAULT";
    }
}

