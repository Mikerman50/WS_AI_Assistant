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
    char claude_personality[301];              // a name from claude_prompt.h, or a description

    int listen_max_seconds;      // recording stops and is transcribed after this long
    int listen_mic_gain_db;      // ES8311 microphone gain, 0-42 dB
    bool listen_language_model;  // use models/oido_v2.tlm from the SD card when transcribing

    char idle_activity[8];  // "off", "calm" or "lively"
    int idle_doze_minutes;  // drowsy after this long with nothing happening (0 = never)

    char wake_word[24];        // a name from wake_words.h ("hey jarvis"...) or "off"
    char wake_listen[8];       // "usb": only on USB power; "always": on battery too
    char wake_sensitivity[8];  // "low", "normal" or "high"
    bool wake_chime;           // a short tone when it hears the wake word

    int display_rotation;  // quarter turns, 0-3
    bool display_busy_active_high;

    bool speech_enabled;  // speak replies with the on-device voice
    int speech_volume;    // 0-100
    int speech_speed;     // percent of normal, 50-200
    int speech_pitch;     // percent of normal, 50-200
    int speech_pace_cps;  // subtitle pace when the voice is off
} app_config_t;

void config_set_defaults(app_config_t *c);

// Parses the text of config.json into `c`, starting from the defaults. Returns
// true when the settings are usable. Otherwise `err` gets a one-line message
// short enough for the screen. C and C++ style comments are tolerated.
bool config_parse(const char *json, app_config_t *c, char *err, size_t errsz);
