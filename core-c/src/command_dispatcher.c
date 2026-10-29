#include "command_dispatcher.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

bool flex_is_slash_command(const char* input) {
    if (!input) return false;
    while (*input && isspace((unsigned char)*input)) input++;
    return (*input == '/');
}

static void evaluate_simple_calc(const char* expr, char* out_buf, size_t buf_cap) {
    if (!expr || !out_buf || buf_cap < 32) return;

    while (*expr && isspace((unsigned char)*expr)) expr++;
    if (*expr == '\0') {
        snprintf(out_buf, buf_cap, "Usage: /calc <num1> <op> <num2>");
        return;
    }

    double a = 0.0, b = 0.0;
    char op = 0;

    if (sscanf(expr, "%lf %c %lf", &a, &op, &b) == 3) {
        double result = 0.0;
        bool valid = true;
        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': result = a * b; break;
            case '/':
                if (b != 0.0) {
                    result = a / b;
                } else {
                    snprintf(out_buf, buf_cap, "Error: Division by zero");
                    return;
                }
                break;
            default:
                valid = false;
                break;
        }
        if (valid) {
            snprintf(out_buf, buf_cap, "Calculation: %g %c %g = %g", a, op, b, result);
            return;
        }
    }

    snprintf(out_buf, buf_cap, "Usage: /calc <num1> [+-*/] <num2>");
}

flex_dispatch_action_t flex_dispatch_input(
    const char* raw_input,
    flex_dispatch_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_dispatch_result_t));
        out_result->action = FLEX_DISPATCH_FORWARD_TO_AI;
    }

    if (!raw_input) {
        if (out_result) {
            out_result->action = FLEX_DISPATCH_ERR_SYNTAX;
            snprintf(out_result->response_text, sizeof(out_result->response_text), "Error: Input is NULL");
        }
        return FLEX_DISPATCH_ERR_SYNTAX;
    }

    /* Skip leading whitespace */
    const char* p = raw_input;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p != '/') {
        /* Normal chat message, forward to AI pipeline */
        if (out_result) {
            out_result->action = FLEX_DISPATCH_FORWARD_TO_AI;
            snprintf(out_result->command_verb, sizeof(out_result->command_verb), "chat");
        }
        return FLEX_DISPATCH_FORWARD_TO_AI;
    }

    /* Parse slash command */
    p++; /* skip '/' */
    char verb[32];
    size_t v_idx = 0;
    while (*p && !isspace((unsigned char)*p) && v_idx + 1 < sizeof(verb)) {
        verb[v_idx++] = (char)tolower((unsigned char)*p++);
    }
    verb[v_idx] = '\0';

    while (*p && isspace((unsigned char)*p)) p++; /* skip space to args */

    if (out_result) {
        snprintf(out_result->command_verb, sizeof(out_result->command_verb), "%.31s", verb);
        out_result->action = FLEX_DISPATCH_HANDLED_INTERNAL;
    }

    if (strcmp(verb, "help") == 0) {
        if (out_result) {
            snprintf(out_result->response_text, sizeof(out_result->response_text),
                     "FLEX 1.0 Built-in Commands:\n"
                     "  /help             - Displays this command reference\n"
                     "  /status           - Reports C Core lifecycle and memory diagnostics\n"
                     "  /voice            - Shows voice pipeline and Whisper status\n"
                     "  /calc <a op b>    - Evaluates basic arithmetic\n"
                     "  /clear            - Requests clearing conversation screen\n"
                     "  /reset            - Resets C Core state machine to IDLE");
            out_result->response_len = strlen(out_result->response_text);
        }
        return FLEX_DISPATCH_HANDLED_INTERNAL;
    }

    if (strcmp(verb, "status") == 0) {
        if (out_result) {
            snprintf(out_result->response_text, sizeof(out_result->response_text),
                     "FLEX 1.0 Architecture Status:\n"
                     "  C Core Engine: C11 (Deterministic, 1024B limit, zero-heap parser)\n"
                     "  Desktop GUI: Qt 6.11 (Native C++17)\n"
                     "  Inference: Local Ollama Daemon (http://127.0.0.1:11434)\n"
                     "  Speech STT: whisper.cpp v1.7.4 (Offline GGML tiny.en)\n"
                     "  Speech TTS: Windows SAPI 5.4 COM");
            out_result->response_len = strlen(out_result->response_text);
        }
        return FLEX_DISPATCH_HANDLED_INTERNAL;
    }

    if (strcmp(verb, "voice") == 0) {
        if (out_result) {
            snprintf(out_result->response_text, sizeof(out_result->response_text),
                     "Voice Pipeline Diagnostics:\n"
                     "  Capture: WASAPI 16000 Hz Mono Float32 (30s max buffer)\n"
                     "  Speech Recognition: whisper.cpp ggml-tiny.en.bin\n"
                     "  Synthesis: Windows SAPI 5.4 Multithreaded COM Queue");
            out_result->response_len = strlen(out_result->response_text);
        }
        return FLEX_DISPATCH_HANDLED_INTERNAL;
    }

    if (strcmp(verb, "calc") == 0) {
        if (out_result) {
            evaluate_simple_calc(p, out_result->response_text, sizeof(out_result->response_text));
            out_result->response_len = strlen(out_result->response_text);
        }
        return FLEX_DISPATCH_HANDLED_INTERNAL;
    }

    if (strcmp(verb, "clear") == 0) {
        if (out_result) {
            out_result->requires_clear_ui = true;
            snprintf(out_result->response_text, sizeof(out_result->response_text), "Conversation screen cleared.");
            out_result->response_len = strlen(out_result->response_text);
        }
        return FLEX_DISPATCH_HANDLED_INTERNAL;
    }

    if (strcmp(verb, "reset") == 0) {
        if (out_result) {
            out_result->modifies_session = true;
            snprintf(out_result->response_text, sizeof(out_result->response_text), "C Core state and session tracker reset to IDLE.");
            out_result->response_len = strlen(out_result->response_text);
        }
        return FLEX_DISPATCH_HANDLED_INTERNAL;
    }

    /* Unknown slash command */
    if (out_result) {
        out_result->action = FLEX_DISPATCH_ERR_SYNTAX;
        snprintf(out_result->response_text, sizeof(out_result->response_text),
                 "Unrecognized slash command '/%s'. Type /help for available commands.", verb);
        out_result->response_len = strlen(out_result->response_text);
    }
    return FLEX_DISPATCH_ERR_SYNTAX;
}

