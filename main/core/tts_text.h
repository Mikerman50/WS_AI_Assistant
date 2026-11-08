// Prepares a sentence for the Pico voice.
#pragma once

#include <stddef.h>

// Copies `text` into `out` ready for Pico: characters Pico would read as markup
// (< > &) are replaced, typographic quotes and dashes become plain ones, and the
// text is wrapped in speed and pitch markup when they aren't 100 (%).
// Always NUL-terminates; returns the length written.
size_t tts_prepare(const char *text, int speed_pct, int pitch_pct, char *out, size_t outsz);
