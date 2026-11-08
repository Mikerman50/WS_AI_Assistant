// Subtitle pages for one spoken sentence, and their timing.
//
// A sentence is word-wrapped into pages of up to N lines. Each page gets a start
// and end time from per-character timings, either real ones from the TTS service
// (ElevenLabs returns a start time for every character it speaks) or estimates
// at a speaking pace. While audio plays, captions_page_at() says which page to show.
#pragma once

#include <stdint.h>

#define CAP_MAX_LINES 3
#define CAP_LINE_BYTES 96
#define CAP_MAX_PAGES 16

// Returns the pixel width of the first `nbytes` bytes of `s`.
typedef int (*cap_measure_fn)(const char *s, int nbytes, void *ctx);

typedef struct {
    char line[CAP_MAX_LINES][CAP_LINE_BYTES];
    int nlines;
    int first_cp, last_cp;       // code points covered (inclusive), indices into the sentence
    uint32_t start_ms, end_ms;   // relative to the start of the sentence's audio
} cap_page_t;

typedef struct {
    cap_page_t page[CAP_MAX_PAGES];
    int npages;
    int ncp;  // code points in the sentence
} cap_layout_t;

// Word-wraps `text` into pages. Returns the number of pages (0 for empty text).
int captions_layout(cap_layout_t *out, const char *text, int max_w, int max_lines,
                    cap_measure_fn measure, void *ctx);

// Estimates per-code-point start times for `text` spoken at `cps` characters per
// second, with pauses after punctuation. Writes up to `max` entries to `starts`
// and returns the total duration in milliseconds.
uint32_t captions_estimate(const char *text, float cps, uint32_t *starts, int max);

// Sets each page's start/end from per-code-point times. `ends` may be NULL, in
// which case a page ends where the next character starts (or at `total_ms`).
void captions_apply_timing(cap_layout_t *L, const uint32_t *starts, const uint32_t *ends,
                           int n, uint32_t total_ms);

// Index of the page to show `t_ms` after the sentence's audio started.
int captions_page_at(const cap_layout_t *L, uint32_t t_ms);

// How far through page `p` speech has got at `t_ms`, 0..1 (drives the reading eyes).
float captions_progress(const cap_page_t *p, uint32_t t_ms);
