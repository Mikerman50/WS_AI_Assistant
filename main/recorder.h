// Records the microphone into PSRAM until told to stop or the time limit is hit.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int16_t *pcm;    // 16 kHz mono, in PSRAM; free with recorder_free()
    size_t samples;
    bool hit_limit;  // stopped because max_seconds ran out
    float peak_dbfs; // loudest moment, for checking the mic gain in the log
} recording_t;

// Called a few times a second with the current level, 0..1.
typedef void (*recorder_level_cb_t)(float level);

// Blocks until *stop becomes true or max_seconds pass, or, when `hands_free`,
// until a pause after speaking (see core/endpoint.h). Hands-free with nothing
// said gives an empty recording.
esp_err_t recorder_capture(recording_t *out, int max_seconds, volatile bool *stop, recorder_level_cb_t on_level,
                           bool hands_free);

void recorder_free(recording_t *r);
