// Reads ElevenLabs' speech-to-text reply.
#pragma once

#include <stddef.h>

typedef enum {
    STT_TEXT,   // `text` holds what was said
    STT_EMPTY,  // nothing recognisable was said
    STT_ERROR,  // `err` holds a short message for the screen
} stt_result_t;

// `status` is the HTTP status code and `body` the response body (may be NULL).
// The transcript is tidied: whitespace collapsed, trimmed, cut to fit `textsz`
// on a character boundary.
stt_result_t stt_parse_response(int status, const char *body, char *text, size_t textsz, char *err, size_t errsz);
