// Hands-free listening: decides when you've finished speaking, from the level
// of each chunk of the recording.
//
// It learns the room's background level in the first moments, counts speech as
// anything clearly above it, and finishes after a pause once you've said
// something. If nothing is said at all it gives up.
//
// Pure logic, unit-tested on the host.
#pragma once

#include <stdbool.h>

typedef enum {
    EP_LISTENING,  // keep recording
    EP_DONE,       // speech followed by a pause: stop and transcribe
    EP_NOTHING,    // no speech before the time ran out: stop, nothing to send
} endpoint_result_t;

typedef struct {
    float floor_db;    // background level
    int elapsed_ms;
    int speech_ms;     // speech heard so far
    int silence_ms;    // quiet since the last speech
    bool started;      // enough speech to count as a question
} endpoint_t;

#define EP_MIN_SPEECH_MS 250   // less than this is a cough or a knock
#define EP_END_SILENCE_MS 1100 // a pause this long ends the question
#define EP_NO_SPEECH_MS 6000   // give up if nothing is said for this long
#define EP_SPEECH_ABOVE_DB 9.0f
#define EP_SPEECH_MIN_DB -60.0f

void endpoint_init(endpoint_t *e);

// `level_dbfs` is the chunk's RMS level; `chunk_ms` its length.
endpoint_result_t endpoint_feed(endpoint_t *e, float level_dbfs, int chunk_ms);
