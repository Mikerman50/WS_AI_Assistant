#pragma once

#include <stddef.h>
#include <stdint.h>

// Decodes the code point at `s` (which must not point at the terminating NUL).
// Returns the number of bytes used (1-4). Malformed input yields U+FFFD and 1 byte.
int utf8_next(const char *s, uint32_t *cp);

// Encodes `cp` into `out` (at least 4 bytes). Returns the byte count.
int utf8_put(char *out, uint32_t cp);

// Number of code points in a NUL-terminated string.
int utf8_count(const char *s);

// Largest length <= n that doesn't cut a multi-byte character in half
// (`s` must have at least n bytes).
size_t utf8_truncate(const char *s, size_t n);
