#include "endpoint.h"

#define LEARN_MS 200        // the first moments set the background level...
#define FLOOR_CAP_DB -40.0f  // ...but never above this: you may already be talking

void endpoint_init(endpoint_t *e)
{
    e->floor_db = 0.0f;
    e->elapsed_ms = 0;
    e->speech_ms = 0;
    e->silence_ms = 0;
    e->started = false;
}

endpoint_result_t endpoint_feed(endpoint_t *e, float db, int chunk_ms)
{
    if (e->elapsed_ms == 0 || (e->elapsed_ms < LEARN_MS && db < e->floor_db))
        e->floor_db = db < FLOOR_CAP_DB ? db : FLOOR_CAP_DB;
    e->elapsed_ms += chunk_ms;

    float threshold = e->floor_db + EP_SPEECH_ABOVE_DB;
    if (threshold < EP_SPEECH_MIN_DB) threshold = EP_SPEECH_MIN_DB;
    bool speech = db >= threshold;

    if (speech) {
        e->speech_ms += chunk_ms;
        e->silence_ms = 0;
        if (e->speech_ms >= EP_MIN_SPEECH_MS) e->started = true;
    } else {
        e->silence_ms += chunk_ms;
        // The background can only settle lower; a slow fall follows a room
        // getting quieter (the chime's echo dying away, say).
        if (db < e->floor_db) e->floor_db += (db - e->floor_db) * 0.2f;
        if (!e->started && e->silence_ms >= 400) e->speech_ms = 0;  // stray noises don't add up
    }

    if (e->started && e->silence_ms >= EP_END_SILENCE_MS) return EP_DONE;
    if (!e->started && e->elapsed_ms >= EP_NO_SPEECH_MS) return EP_NOTHING;
    return EP_LISTENING;
}
