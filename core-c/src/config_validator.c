#include "config_validator.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

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
        default: return "UNKNOWN";
    }
}
