#include "config_validator.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static bool is_valid_ipv4(const char* str) {
    int octets = 0;
    int cur_val = 0;
    int digit_count = 0;

    for (const char* p = str; *p; ++p) {
        if (isdigit((unsigned char)*p)) {
            cur_val = cur_val * 10 + (*p - '0');
            digit_count++;
            if (cur_val > 255 || digit_count > 3) return false;
        } else if (*p == '.') {
            if (digit_count == 0) return false;
            octets++;
            cur_val = 0;
            digit_count = 0;
            if (octets > 3) return false;
        } else {
            return false;
        }
    }
    return (octets == 3 && digit_count > 0);
}

flex_cfg_status_t flex_validate_host(
    const char* host,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (!host) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_NULL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Host string pointer is NULL");
        }
        return FLEX_CFG_ERR_NULL;
    }

    size_t len = strlen(host);
    if (len == 0 || len > 253) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_HOST;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Host length (%zu) is invalid", len);
        }
        return FLEX_CFG_ERR_INVALID_HOST;
    }

    /* Check if the host consists only of digits and dots (an attempted IP address) */
    bool all_digits_and_dots = true;
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)host[i]) && host[i] != '.') {
            all_digits_and_dots = false;
            break;
        }
    }
    if (all_digits_and_dots) {
        if (!is_valid_ipv4(host)) {
            if (out_result) {
                out_result->status = FLEX_CFG_ERR_INVALID_HOST;
                snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Invalid IPv4 address format or octet > 255");
            }
            return FLEX_CFG_ERR_INVALID_HOST;
        }
        return FLEX_CFG_VALID;
    }

    if (strcmp(host, "localhost") == 0) {
        return FLEX_CFG_VALID;
    }

    /* Standard hostname characters: letters, digits, hyphen, dot */
    for (size_t i = 0; i < len; ++i) {
        char c = host[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '.') {
            if (out_result) {
                out_result->status = FLEX_CFG_ERR_INVALID_HOST;
                snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Invalid character '%c' in hostname", c);
            }
            return FLEX_CFG_ERR_INVALID_HOST;
        }
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_port(
    int port,
    bool allow_privileged,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (port <= 0 || port > 65535) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_PORT;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Port %d is outside valid TCP range 1-65535", port);
        }
        return FLEX_CFG_ERR_INVALID_PORT;
    }

    if (port < 1024 && !allow_privileged) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_PRIVILEGED_PORT;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Port %d is a privileged system port (< 1024)", port);
        }
        return FLEX_CFG_ERR_PRIVILEGED_PORT;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_model_name(
    const char* model_name,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (!model_name) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_NULL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Model name is NULL");
        }
        return FLEX_CFG_ERR_NULL;
    }

    size_t len = strlen(model_name);
    if (len == 0 || len > 128) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_MODEL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Model name length (%zu) invalid", len);
        }
        return FLEX_CFG_ERR_INVALID_MODEL;
    }

    /* Valid characters: alphanumeric, '.', '-', '_', '/', ':' */
    int slash_count = 0;
    int colon_count = 0;

    for (size_t i = 0; i < len; ++i) {
        char c = model_name[i];
        if (c == '/') slash_count++;
        else if (c == ':') colon_count++;
        else if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != '_') {
            if (out_result) {
                out_result->status = FLEX_CFG_ERR_INVALID_MODEL;
                snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Invalid character '%c' in model name", c);
            }
            return FLEX_CFG_ERR_INVALID_MODEL;
        }
    }

    if (slash_count > 1 || colon_count > 1) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_MODEL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Model name format must be [namespace/]name[:tag]");
        }
        return FLEX_CFG_ERR_INVALID_MODEL;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_whisper_path(
    const char* path,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (!path) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_NULL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Path is NULL");
        }
        return FLEX_CFG_ERR_NULL;
    }

    size_t len = strlen(path);
    if (len < 5) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_WHISPER_PATH;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Whisper model path is too short");
        }
        return FLEX_CFG_ERR_INVALID_WHISPER_PATH;
    }

    const char* ext = path + len - 4;
    if (strcmp(ext, ".bin") != 0 && strcmp(ext, ".BIN") != 0) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_WHISPER_PATH;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Whisper model file must have .bin extension");
        }
        return FLEX_CFG_ERR_INVALID_WHISPER_PATH;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_whisper_language(
    const char* language,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (!language) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_NULL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Whisper language pointer is NULL");
        }
        return FLEX_CFG_ERR_NULL;
    }

    char lower[16];
    size_t len = strlen(language);
    if (len == 0 || len >= sizeof(lower)) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_WHISPER_LANG;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail),
                     "Whisper language '%s' has invalid length (%zu)", language, len);
        }
        return FLEX_CFG_ERR_INVALID_WHISPER_LANG;
    }

    for (size_t i = 0; i < len; ++i) {
        lower[i] = (char)tolower((unsigned char)language[i]);
    }
    lower[len] = '\0';

    if (strcmp(lower, "en") != 0 && strcmp(lower, "hi") != 0 && strcmp(lower, "auto") != 0) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_WHISPER_LANG;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail),
                     "Whisper language '%s' is unsupported. Allowed: 'en', 'hi', 'auto'", language);
        }
        return FLEX_CFG_ERR_INVALID_WHISPER_LANG;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_whisper_prompt(
    const char* prompt,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (!prompt) {
        /* NULL prompt is valid (prompt is optional) */
        return FLEX_CFG_VALID;
    }

    size_t len = strlen(prompt);
    if (len > 255) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_WHISPER_PROMPT;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail),
                     "Whisper prompt length (%zu) exceeds 255 character limit", len);
        }
        return FLEX_CFG_ERR_INVALID_WHISPER_PROMPT;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_tts_rate(
    int rate,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (rate < -10 || rate > 10) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_RATE_OUT_OF_RANGE;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "TTS rate %d must be in range -10 to +10", rate);
        }
        return FLEX_CFG_ERR_RATE_OUT_OF_RANGE;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_tts_volume(
    int volume,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (volume < 0 || volume > 100) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_VOLUME_OUT_OF_RANGE;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "TTS volume %d must be in range 0 to 100", volume);
        }
        return FLEX_CFG_ERR_VOLUME_OUT_OF_RANGE;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_audio_sample_rate(
    uint32_t sample_rate,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (sample_rate != 8000 && sample_rate != 16000 &&
        sample_rate != 22050 && sample_rate != 44100 &&
        sample_rate != 48000 && sample_rate != 96000) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_UNSUPPORTED_SAMPLE_RATE;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Sample rate %u Hz is not supported", sample_rate);
        }
        return FLEX_CFG_ERR_UNSUPPORTED_SAMPLE_RATE;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_audio_channels(
    uint32_t channels,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (channels == 0 || channels > 8) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_CHANNELS;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Channel count %u must be between 1 and 8", channels);
        }
        return FLEX_CFG_ERR_INVALID_CHANNELS;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_endpoint_url(
    const char* url,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (!url) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_NULL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "URL string is NULL");
        }
        return FLEX_CFG_ERR_NULL;
    }

    size_t len = strlen(url);
    if (len < 10 || len > 256) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_URL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "URL length (%zu) invalid", len);
        }
        return FLEX_CFG_ERR_INVALID_URL;
    }

    const char* p = url;
    int default_port = 80;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        default_port = 80;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        default_port = 443;
    } else {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_URL;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "URL scheme must be http:// or https://");
        }
        return FLEX_CFG_ERR_INVALID_URL;
    }

    char host_buf[256];
    const char* colon = strchr(p, ':');
    const char* slash = strchr(p, '/');
    int port = default_port;

    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - p);
        if (hlen == 0 || hlen >= sizeof(host_buf)) {
            if (out_result) {
                out_result->status = FLEX_CFG_ERR_INVALID_HOST;
                snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Host in URL is empty or too long");
            }
            return FLEX_CFG_ERR_INVALID_HOST;
        }
        memcpy(host_buf, p, hlen);
        host_buf[hlen] = '\0';

        const char* port_str = colon + 1;
        char* endptr = NULL;
        long parsed_port = strtol(port_str, &endptr, 10);
        if (endptr == port_str || (slash && endptr != slash) || (!slash && *endptr != '\0')) {
            if (out_result) {
                out_result->status = FLEX_CFG_ERR_INVALID_PORT;
                snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Malformed port in URL");
            }
            return FLEX_CFG_ERR_INVALID_PORT;
        }
        port = (int)parsed_port;
    } else if (slash) {
        size_t hlen = (size_t)(slash - p);
        if (hlen == 0 || hlen >= sizeof(host_buf)) {
            if (out_result) {
                out_result->status = FLEX_CFG_ERR_INVALID_HOST;
                snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Host in URL is empty or too long");
            }
            return FLEX_CFG_ERR_INVALID_HOST;
        }
        memcpy(host_buf, p, hlen);
        host_buf[hlen] = '\0';
    } else {
        size_t hlen = strlen(p);
        if (hlen == 0 || hlen >= sizeof(host_buf)) {
            if (out_result) {
                out_result->status = FLEX_CFG_ERR_INVALID_HOST;
                snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Host in URL is empty or too long");
            }
            return FLEX_CFG_ERR_INVALID_HOST;
        }
        memcpy(host_buf, p, hlen);
        host_buf[hlen] = '\0';
    }

    flex_cfg_status_t hst = flex_validate_host(host_buf, out_result);
    if (hst != FLEX_CFG_VALID) {
        return hst;
    }

    return flex_validate_port(port, true, out_result);
}

flex_cfg_status_t flex_validate_temperature(
    double temperature,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (isnan(temperature) || isinf(temperature) || temperature < 0.0 || temperature > 2.0) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_TEMPERATURE;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Temperature must be between 0.0 and 2.0");
        }
        return FLEX_CFG_ERR_INVALID_TEMPERATURE;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_context_window(
    int num_ctx,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (num_ctx < 256 || num_ctx > 131072) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_NUM_CTX;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Context window tokens %d must be in range 256 to 131072", num_ctx);
        }
        return FLEX_CFG_ERR_INVALID_NUM_CTX;
    }

    return FLEX_CFG_VALID;
}

flex_cfg_status_t flex_validate_timeout(
    int timeout_seconds,
    flex_cfg_result_t* out_result
) {
    if (out_result) {
        memset(out_result, 0, sizeof(flex_cfg_result_t));
        out_result->status = FLEX_CFG_VALID;
    }

    if (timeout_seconds < 1 || timeout_seconds > 600) {
        if (out_result) {
            out_result->status = FLEX_CFG_ERR_INVALID_TIMEOUT;
            snprintf(out_result->error_detail, sizeof(out_result->error_detail), "Timeout %d must be in range 1 to 600 seconds", timeout_seconds);
        }
        return FLEX_CFG_ERR_INVALID_TIMEOUT;
    }

    return FLEX_CFG_VALID;
}

const char* flex_cfg_status_to_string(flex_cfg_status_t status) {
    switch (status) {
        case FLEX_CFG_VALID: return "VALID";
        case FLEX_CFG_ERR_NULL: return "ERR_NULL";
        case FLEX_CFG_ERR_INVALID_HOST: return "ERR_INVALID_HOST";
        case FLEX_CFG_ERR_INVALID_PORT: return "ERR_INVALID_PORT";
        case FLEX_CFG_ERR_PRIVILEGED_PORT: return "ERR_PRIVILEGED_PORT";
        case FLEX_CFG_ERR_INVALID_MODEL: return "ERR_INVALID_MODEL";
        case FLEX_CFG_ERR_INVALID_WHISPER_PATH: return "ERR_INVALID_WHISPER_PATH";
        case FLEX_CFG_ERR_RATE_OUT_OF_RANGE: return "ERR_RATE_OUT_OF_RANGE";
        case FLEX_CFG_ERR_VOLUME_OUT_OF_RANGE: return "ERR_VOLUME_OUT_OF_RANGE";
        case FLEX_CFG_ERR_UNSUPPORTED_SAMPLE_RATE: return "ERR_UNSUPPORTED_SAMPLE_RATE";
        case FLEX_CFG_ERR_INVALID_CHANNELS: return "ERR_INVALID_CHANNELS";
        case FLEX_CFG_ERR_INVALID_URL: return "ERR_INVALID_URL";
        case FLEX_CFG_ERR_INVALID_TEMPERATURE: return "ERR_INVALID_TEMPERATURE";
        case FLEX_CFG_ERR_INVALID_NUM_CTX: return "ERR_INVALID_NUM_CTX";
        case FLEX_CFG_ERR_INVALID_TIMEOUT: return "ERR_INVALID_TIMEOUT";
        case FLEX_CFG_ERR_INVALID_WHISPER_LANG: return "ERR_INVALID_WHISPER_LANG";
        case FLEX_CFG_ERR_INVALID_WHISPER_PROMPT: return "ERR_INVALID_WHISPER_PROMPT";
        default: return "UNKNOWN";
    }
}

void flex_app_config_init_defaults(FlexAppConfig* config) {
    if (!config) return;
    memset(config, 0, sizeof(FlexAppConfig));
    strncpy(config->endpoint, "http://127.0.0.1:11434", sizeof(config->endpoint) - 1);
    strncpy(config->model, "qwen2.5-coder:3b", sizeof(config->model) - 1);
    config->temperature = 0.2;
    config->num_ctx = 4096;
    config->timeout_seconds = 30;
    config->audio_input_device[0] = '\0';
    config->tts_rate = 0;
    config->tts_volume = 100;
    config->whisper_model_path[0] = '\0';
    strncpy(config->whisper_language, "en", sizeof(config->whisper_language) - 1);
    config->whisper_prompt[0] = '\0';
}

bool flex_validate_app_config(
    const FlexAppConfig* config,
    FlexAppConfigValidation* out_validation
) {
    if (out_validation) {
        memset(out_validation, 0, sizeof(FlexAppConfigValidation));
        out_validation->is_valid = true;
    }
    if (!config) {
        if (out_validation) {
            out_validation->is_valid = false;
            out_validation->error_count = 1;
            snprintf(out_validation->errors[0], sizeof(out_validation->errors[0]), "Configuration is NULL");
        }
        return false;
    }

    bool valid = true;
    flex_cfg_result_t res;

    #define RECORD_CFG_ERR(func_call, field_name) do { \
        if ((func_call) != FLEX_CFG_VALID) { \
            valid = false; \
            if (out_validation && out_validation->error_count < 8) { \
                snprintf(out_validation->errors[out_validation->error_count], \
                         sizeof(out_validation->errors[0]), "%s: %s", field_name, res.error_detail); \
                out_validation->error_count++; \
            } \
        } \
    } while(0)

    RECORD_CFG_ERR(flex_validate_endpoint_url(config->endpoint, &res), "endpoint");
    RECORD_CFG_ERR(flex_validate_model_name(config->model, &res), "model");
    RECORD_CFG_ERR(flex_validate_temperature(config->temperature, &res), "temperature");
    RECORD_CFG_ERR(flex_validate_context_window(config->num_ctx, &res), "num_ctx");
    RECORD_CFG_ERR(flex_validate_timeout(config->timeout_seconds, &res), "timeout_seconds");
    RECORD_CFG_ERR(flex_validate_tts_rate(config->tts_rate, &res), "tts_rate");
    RECORD_CFG_ERR(flex_validate_tts_volume(config->tts_volume, &res), "tts_volume");

    if (config->whisper_model_path[0] != '\0') {
        RECORD_CFG_ERR(flex_validate_whisper_path(config->whisper_model_path, &res), "whisper_model_path");
    }

    if (config->whisper_language[0] != '\0') {
        RECORD_CFG_ERR(flex_validate_whisper_language(config->whisper_language, &res), "whisper_language");
    } else {
        valid = false;
        if (out_validation && out_validation->error_count < 8) {
            snprintf(out_validation->errors[out_validation->error_count],
                     sizeof(out_validation->errors[0]), "whisper_language: Language code cannot be empty");
            out_validation->error_count++;
        }
    }

    if (config->whisper_prompt[0] != '\0') {
        RECORD_CFG_ERR(flex_validate_whisper_prompt(config->whisper_prompt, &res), "whisper_prompt");
    }

    #undef RECORD_CFG_ERR

    if (out_validation) {
        out_validation->is_valid = valid;
    }
    return valid;
}

bool flex_app_config_from_kv(
    const FlexKvStore* store,
    FlexAppConfig* out_config,
    FlexAppConfigValidation* out_validation
) {
    if (!store || !out_config) return false;
    flex_app_config_init_defaults(out_config);

    const char* ep = flex_kv_get_str(store, "ollama.endpoint");
    if (ep && ep[0] != '\0') {
        strncpy(out_config->endpoint, ep, sizeof(out_config->endpoint) - 1);
        out_config->endpoint[sizeof(out_config->endpoint) - 1] = '\0';
    }

    const char* mdl = flex_kv_get_str(store, "ollama.model");
    if (mdl && mdl[0] != '\0') {
        strncpy(out_config->model, mdl, sizeof(out_config->model) - 1);
        out_config->model[sizeof(out_config->model) - 1] = '\0';
    }

    double temp = 0.0;
    if (flex_kv_parse_float(store, "ollama.temperature", &temp) == FLEX_KV_OK) {
        out_config->temperature = temp;
    }

    long long ctx = 0;
    if (flex_kv_parse_int(store, "ollama.num_ctx", &ctx) == FLEX_KV_OK) {
        out_config->num_ctx = (int)ctx;
    }

    long long timeout = 0;
    if (flex_kv_parse_int(store, "ollama.timeout", &timeout) == FLEX_KV_OK) {
        out_config->timeout_seconds = (int)timeout;
    }

    const char* audio = flex_kv_get_str(store, "audio.input_device");
    if (audio) {
        strncpy(out_config->audio_input_device, audio, sizeof(out_config->audio_input_device) - 1);
        out_config->audio_input_device[sizeof(out_config->audio_input_device) - 1] = '\0';
    }

    long long rate = 0;
    if (flex_kv_parse_int(store, "tts.rate", &rate) == FLEX_KV_OK) {
        out_config->tts_rate = (int)rate;
    }

    long long vol = 0;
    if (flex_kv_parse_int(store, "tts.volume", &vol) == FLEX_KV_OK) {
        out_config->tts_volume = (int)vol;
    }

    const char* whisper = flex_kv_get_str(store, "whisper.model_path");
    if (whisper && whisper[0] != '\0') {
        strncpy(out_config->whisper_model_path, whisper, sizeof(out_config->whisper_model_path) - 1);
        out_config->whisper_model_path[sizeof(out_config->whisper_model_path) - 1] = '\0';
    }

    const char* lang = flex_kv_get_str(store, "whisper.language");
    if (lang && lang[0] != '\0') {
        strncpy(out_config->whisper_language, lang, sizeof(out_config->whisper_language) - 1);
        out_config->whisper_language[sizeof(out_config->whisper_language) - 1] = '\0';
    } else {
        strncpy(out_config->whisper_language, "en", sizeof(out_config->whisper_language) - 1);
        out_config->whisper_language[sizeof(out_config->whisper_language) - 1] = '\0';
    }

    const char* prompt = flex_kv_get_str(store, "whisper.initial_prompt");
    if (prompt && prompt[0] != '\0') {
        strncpy(out_config->whisper_prompt, prompt, sizeof(out_config->whisper_prompt) - 1);
        out_config->whisper_prompt[sizeof(out_config->whisper_prompt) - 1] = '\0';
    } else {
        out_config->whisper_prompt[0] = '\0';
    }

    return flex_validate_app_config(out_config, out_validation);
}

bool flex_app_config_to_kv(
    const FlexAppConfig* config,
    FlexKvStore* out_store
) {
    if (!config || !out_store) return false;
    flex_kv_init(out_store);

    if (!flex_kv_set(out_store, "ollama.endpoint", config->endpoint) ||
        !flex_kv_set(out_store, "ollama.model", config->model) ||
        !flex_kv_set_float(out_store, "ollama.temperature", config->temperature) ||
        !flex_kv_set_int(out_store, "ollama.num_ctx", config->num_ctx) ||
        !flex_kv_set_int(out_store, "ollama.timeout", config->timeout_seconds) ||
        !flex_kv_set(out_store, "audio.input_device", config->audio_input_device) ||
        !flex_kv_set_int(out_store, "tts.rate", config->tts_rate) ||
        !flex_kv_set_int(out_store, "tts.volume", config->tts_volume)) {
        return false;
    }

    if (config->whisper_model_path[0] != '\0') {
        if (!flex_kv_set(out_store, "whisper.model_path", config->whisper_model_path)) {
            return false;
        }
    }

    const char* lang_to_save = (config->whisper_language[0] != '\0') ? config->whisper_language : "en";
    if (!flex_kv_set(out_store, "whisper.language", lang_to_save)) {
        return false;
    }

    if (config->whisper_prompt[0] != '\0') {
        if (!flex_kv_set(out_store, "whisper.initial_prompt", config->whisper_prompt)) {
            return false;
        }
    }

    return true;
}
