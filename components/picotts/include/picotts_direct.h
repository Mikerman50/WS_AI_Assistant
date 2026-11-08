// Synchronous wrapper around the SVOX Pico TTS engine: one call speaks one
// piece of text and hands the 16 kHz, 16-bit mono samples to a callback as
// they are produced, returning when the text is fully spoken.
//
// Portable C (no ESP-IDF), so the same code runs in the PC tests. The ESP-IDF
// glue (resources embedded in flash, memory in PSRAM) is in picotts_esp.h.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PICOTTS_SAMPLE_RATE 16000
#define PICOTTS_MEM_SIZE 1100000  // working memory the engine needs

// Receives generated samples. Return false to stop speaking early.
typedef bool (*picotts_sink_fn)(const int16_t *samples, size_t count, void *ctx);

// Sets up the engine. `ta` and `sg` point at the text-analysis and
// signal-generation resource files (they must stay valid); `mem` is the
// engine's working memory (at least PICOTTS_MEM_SIZE bytes). Returns false on
// failure, with the reason logged to stderr.
bool picotts_direct_init(const void *ta, const void *sg, void *mem, size_t mem_size);

// Speaks `text` (UTF-8, may contain Pico markup such as <speed level="110">).
// Returns 0 when done, 1 if the sink stopped it, or a negative Pico error.
int picotts_direct_say(const char *text, picotts_sink_fn sink, void *ctx);

void picotts_direct_shutdown(void);

// Optional: called between synthesis steps (a few milliseconds apart), so a
// cooperative scheduler can let other work run. NULL to remove.
void picotts_direct_set_yield(void (*fn)(void));

#ifdef __cplusplus
}
#endif
