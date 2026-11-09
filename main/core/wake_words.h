// The wake words the board can listen for (microWakeWord models built into the
// firmware), and how the settings pick one.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *name;   // as said, e.g. "Hey Jarvis"
    const char *file;   // model file name without .tflite
    float cutoff;       // the model's own probability cutoff
    int window;         // its sliding window
    size_t arena_bytes; // its tensor arena size
} wake_word_t;

typedef enum { WAKE_SENS_LOW, WAKE_SENS_NORMAL, WAKE_SENS_HIGH } wake_sens_t;

#define WAKE_WORD_COUNT 4
extern const wake_word_t WAKE_WORDS[WAKE_WORD_COUNT];

// Finds a wake word by name, ignoring case, spaces, underscores and commas
// ("hey jarvis", "Hey_Jarvis", "okay nabu" / "ok nabu"). NULL if unknown or "off".
const wake_word_t *wake_word_find(const char *name);

// True for "off", "none", "no" or "" (no wake word).
bool wake_word_is_off(const char *name);

// -1 if unknown.
int wake_sens_from_name(const char *name);

// The cutoff to use: the model's own for normal; lower (wakes more easily, more
// false alarms) for high; higher for low.
float wake_word_cutoff(const wake_word_t *w, wake_sens_t s);
