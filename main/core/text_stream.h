// Turns Claude's streamed reply into speakable sentences.
//
// Feed it text deltas as they arrive. It strips face cues like "[amused]" out of
// the text and hands each finished sentence over together with the cue that
// came before it, so the face changes when that sentence starts playing.
// It also drops markdown symbols and emoji, which neither the voice nor the
// e-ink font should see, and takes out notes in braces, like
// "{remember: Mike takes his tea black}", which are passed to `on_note` instead
// of being spoken.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TS_BUF 400

// `cue` is "" when no cue preceded the sentence.
typedef void (*ts_sentence_fn)(const char *sentence, const char *cue, void *ctx);
// `note` is the text between the braces, trimmed.
typedef void (*ts_note_fn)(const char *note, void *ctx);

#define TS_NOTE_MAX 240

typedef struct {
    ts_sentence_fn on_sentence;
    ts_note_fn on_note;  // optional; set after ts_init
    void *ctx;
    int min_len;  // sentences shorter than this (bytes) are joined with the next one
    int max_len;  // longer runs are cut at a comma or space so speech can start sooner

    // internal state
    char buf[TS_BUF];
    int len;
    char cue[24];      // cue being read
    int cue_len;
    bool in_cue;
    char next_cue[24]; // cue to attach to the sentence being built
    bool pending_end;  // saw . ! ? and waiting for what follows
    char u8[4];        // partial UTF-8 sequence split across deltas
    int u8_have, u8_need;
    char note[TS_NOTE_MAX];  // note being read
    int note_len;
    bool in_note;
} text_stream_t;

void ts_init(text_stream_t *t, ts_sentence_fn on_sentence, void *ctx);
void ts_feed(text_stream_t *t, const char *data, size_t n);
void ts_finish(text_stream_t *t);  // flushes whatever is left
