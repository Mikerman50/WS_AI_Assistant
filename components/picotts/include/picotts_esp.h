// ESP-IDF glue for picotts_direct.h. The voice data (the two en-GB files) is
// loaded from the SD card by the caller and stays in memory; the engine's
// working memory (about 1.1 MB of PSRAM) is only taken while it's started, so
// it can be handed back between replies.
#pragma once

#include <stdbool.h>

#include "picotts_direct.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the engine on the given voice files (they must stay valid). Then speak
// with picotts_direct_say() from one task at a time; it needs only about 1 KB
// of that task's stack. Returns false if there's no memory or the files are
// wrong. Safe to call when already started.
bool picotts_esp_start(const void *ta, const void *sg);

// Stops the engine and frees its working memory.
void picotts_esp_stop(void);

#ifdef __cplusplus
}
#endif
