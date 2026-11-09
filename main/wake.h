// Listens for the wake word between conversations (see core/wake_words.h for
// the words, core/power_src.h for how it tells USB power from battery).
#pragma once

#include <stdbool.h>

#include "config_parse.h"

// Starts listening if the settings ask for a wake word and the microphone works.
void wake_init(const app_config_t *cfg, bool audio_ok);

// Frees the microphone for a recording; returns once it's free. Pair with
// wake_resume().
void wake_pause(void);
void wake_resume(void);

// What it's doing, for the console ("listening for Hey Jarvis"...).
const char *wake_status(void);
