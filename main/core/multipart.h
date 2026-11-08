// multipart/form-data framing for uploading one file with a few text fields.
// The file bytes go between the head and the tail, so they can be streamed
// straight from memory without copying them into one big body.
#pragma once

#include <stddef.h>

typedef struct {
    const char *name;
    const char *value;
} mp_field_t;

// Writes the text fields and the file part's headers. Returns the length, or -1
// if `out` is too small.
int multipart_head(char *out, size_t outsz, const char *boundary, const mp_field_t *fields, int nfields,
                   const char *file_field, const char *filename, const char *content_type);

// Writes the closing boundary. Returns the length, or -1 if `out` is too small.
int multipart_tail(char *out, size_t outsz, const char *boundary);
