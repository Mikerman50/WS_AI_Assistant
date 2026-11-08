// ESP-IDF glue for picotts_direct.h: the British English voice data is
// embedded in the firmware, and the engine's working memory goes in PSRAM.
#pragma once

#include <stdbool.h>

#include "picotts_direct.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the engine (about 1.1 MB of PSRAM). Safe to call more than once.
// Then speak with picotts_direct_say() from one task at a time. The engine
// needs only about 1 KB of that task's stack; it works in its own memory.
bool picotts_esp_init(void);

#ifdef __cplusplus
}
#endif
