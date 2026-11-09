// Wake word spotting with microWakeWord models (Kevin Ahrendt / Open Home
// Foundation, Apache-2.0): TensorFlow's audio front end turns 16 kHz sound into
// 40-band spectrogram slices every 10 ms, and a small streaming network scores
// them. Runs the same on the board and on a PC (for the tests).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ww ww_t;

typedef struct {
    float cutoff;        // 0..1: how sure the model must be, averaged over the window
    int window;          // probabilities averaged (the model's sliding_window_size)
    size_t arena_bytes;  // the model's tensor_arena_size; more is tried if needed
} ww_params_t;

// Where the detector's memory comes from. `fast` asks for internal RAM when
// there's room (it's used on every slice). Memory must be 16-byte aligned.
typedef void *(*ww_alloc_fn)(size_t n, bool fast);
typedef void (*ww_free_fn)(void *p);

// `model` is the .tflite file; it's copied, so it needn't stay valid. NULL on
// failure (with the reason in `err`).
ww_t *ww_create(const uint8_t *model, size_t model_len, const ww_params_t *p, ww_alloc_fn alloc, ww_free_fn free_fn,
                char *err, size_t errsz);

// Feeds 16 kHz mono samples (any number). True if the wake word was heard; the
// detector then ignores the next second, so one phrase gives one detection.
bool ww_feed(ww_t *w, const int16_t *pcm, size_t n);

// Forgets what it has heard (call when listening resumes after a gap); the
// first second after a reset can't trigger.
void ww_reset(ww_t *w);

// The highest probability (0..1) since the last call, for tuning.
float ww_take_peak(ww_t *w);

// Bytes of tensor arena in use.
size_t ww_arena_bytes(const ww_t *w);

void ww_destroy(ww_t *w);

// On the board: the model built into the firmware for a file name such as
// "hey_jarvis" (components/wakeword/models). False if there isn't one.
bool ww_builtin_model(const char *file, const uint8_t **data, size_t *len);

#ifdef __cplusplus
}
#endif
