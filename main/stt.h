// Speech to text with ElevenLabs (Scribe), from a recording in memory.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config_parse.h"
#include "esp_err.h"

void stt_init(const app_config_t *cfg);

// True if an ElevenLabs API key is set.
bool stt_ready(void);

// Uploads 16 kHz mono 16-bit samples and waits for the transcript.
// Returns ESP_OK with `text` set (possibly "" if nothing was recognised), or an
// error with a short message in `err`. Returns ESP_ERR_NOT_FINISHED, without a
// message, if *cancel becomes true.
esp_err_t stt_transcribe(const int16_t *pcm, size_t samples, char *text, size_t textsz, char *err,
                         size_t errsz, volatile bool *cancel);
