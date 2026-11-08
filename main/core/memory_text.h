// The assistant's long-term memory: a plain text file of short facts, one per
// line ("- Mike takes his tea black"), which goes into Claude's instructions.
// Claude adds to it with {remember: ...} notes in its replies and removes lines
// with {forget: ...}. Pure C so it's tested on a PC; main/memory.c keeps it on
// the SD card.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define MEMORY_MAX 6000  // bytes kept (about 1,500 tokens in every request)

typedef struct {
    char text[MEMORY_MAX + 1];  // whole lines, each ending in '\n'
    size_t len;
} memory_t;

// Loads the file's text. If it's longer than MEMORY_MAX, the newest lines (at
// the end) are kept. Blank lines are dropped and Windows line endings fixed.
void memory_load(memory_t *m, const char *file_text);

// Adds "- fact" unless the same fact (ignoring case and a final full stop) is
// already there. Makes room by dropping the oldest lines. Returns true if added.
bool memory_remember(memory_t *m, const char *fact);

// Removes every line containing `words` (ignoring case; at least 3 letters).
// Returns how many lines went.
int memory_forget(memory_t *m, const char *words);

typedef enum { NOTE_OTHER, NOTE_REMEMBER, NOTE_FORGET } note_kind_t;

// Reads a note like "remember: Mike's birthday is 3 May". `*arg` points at the
// text after the colon (trimmed of leading spaces).
note_kind_t memory_parse_note(const char *note, const char **arg);
