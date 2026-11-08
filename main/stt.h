// Speech to text on the board itself, with no service or key: Oído (NVIDIA's
// Conformer-CTC model, fine-tuned and quantised to 4 bits by Lokutor) reads its
// weights in place from the "asr" flash partition. An optional language model,
// loaded from the SD card, corrects words that sound alike.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config_parse.h"
#include "esp_err.h"

// `lm` is the language model file (models/oido_v2.tlm) in memory, or NULL; it
// must stay valid. It's used unless listen.language_model is false.
void stt_init(const app_config_t *cfg, const void *lm, size_t lm_len);

// True if the speech model was found in flash.
bool stt_ready(void);

// True if the language model is in use.
bool stt_has_language_model(void);

// Transcribes 16 kHz mono 16-bit samples, adjusting their level in place first.
// Blocks while the recogniser works (a few seconds for a typical question).
// Returns ESP_OK with `text` set (possibly "" if nothing was recognised), or an
// error with a short message in `err`. Returns ESP_ERR_NOT_FINISHED, without a
// message, if *cancel became true meanwhile (the work itself can't be cut short).
esp_err_t stt_transcribe(int16_t *pcm, size_t samples, char *text, size_t textsz, char *err, size_t errsz,
                         volatile bool *cancel);
