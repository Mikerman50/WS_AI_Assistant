// Long-term memory kept in memory.txt on the SD card (see core/memory_text.h).
// Notes from Claude's replies are applied straight away, so the next question
// already sees them, and written to the card when the reply has finished.
#pragma once

#include <stdbool.h>

// Loads memory.txt; call while the card is mounted (or with it missing).
void memory_init(void);

// What Claude is told it remembers ("" if nothing yet).
const char *memory_text(void);

// A {note} from a reply: "remember: ..." or "forget: ...".
void memory_note(const char *note);

// Writes memory.txt if anything changed. Mounts the card for a moment.
void memory_save(void);

// Empties the memory (console /forget-memory).
void memory_clear(void);
