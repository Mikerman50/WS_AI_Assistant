// Settings from config.json on the SD card. Pure C + cJSON, so it is unit-tested
// on the host as well as built into the firmware.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define APP_CONTEXT_MAX 1000

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];

    char claude_api_key[200];
    char claude_model[64];
    int claude_max_tokens;
    bool claude_web_search;
    char claude_context[APP_CONTEXT_MAX + 1];  // extra notes for Claude about the household

    char elevenlabs_api_key[128];  // used from milestone 2
    char elevenlabs_voice_id[64];

    int display_rotation;  // quarter turns, 0-3
    bool display_busy_active_high;

    int speech_pace_cps;  // subtitle pace while there is no audio
} app_config_t;

void config_set_defaults(app_config_t *c);

// Parses the text of config.json into `c`, starting from the defaults. Returns
// true when the settings are usable. Otherwise `err` gets a one-line message
// short enough for the screen. C and C++ style comments are tolerated.
bool config_parse(const char *json, app_config_t *c, char *err, size_t errsz);
