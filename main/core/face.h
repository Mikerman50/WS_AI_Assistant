// The assistant's face: eyes, mouth, subtitles and status strips on a 200x200 1-bit canvas.
#pragma once

#include <stdbool.h>

#include "gfx.h"

typedef enum {
    EXPR_NEUTRAL,
    EXPR_HAPPY,
    EXPR_AMUSED,
    EXPR_THINKING,
    EXPR_CURIOUS,
    EXPR_SURPRISED,
    EXPR_SORRY,
    EXPR_SLEEPY,
    EXPR_COUNT
} face_expr_t;

typedef enum {
    MODE_IDLE,       // awake, waiting; shows the pinned answer
    MODE_LISTENING,  // shows what it heard and a mic level meter
    MODE_THINKING,   // waiting for Claude
    MODE_SPEAKING,   // mouth moves, subtitles overlaid
    MODE_ASLEEP,     // closed eyes; what the screen keeps while powered off
    MODE_NOTICE      // a message (caption lines) plus an instruction in the bottom band
} face_mode_t;

#define FACE_CAPTION_LINES 3
#define FACE_TEXT_MAX 96

typedef struct {
    face_mode_t mode;
    face_expr_t expr;
    float mouth;  // 0..1 mouth opening while speaking
    float look;   // 0..1 where the eyes look while reading along (0.5 = centre)
    float level;  // 0..1 mic level while listening
    bool hands_free;  // listening started by the wake word: it stops by itself
    char heard[FACE_TEXT_MAX];  // top strip while listening/thinking (empty = none)
    char caption[FACE_CAPTION_LINES][FACE_TEXT_MAX];
    int caption_lines;
    char pinned[FACE_TEXT_MAX];  // bottom band while idle/asleep (empty = none)
    bool blink;   // idle: eyes shut for a moment
    char prop[8]; // idle: a little mark beside the head, like "?" or "z" ("" = none)
} face_state_t;

void face_state_init(face_state_t *s);

// "amused" -> EXPR_AMUSED. Accepts a few synonyms; unknown names return EXPR_COUNT.
face_expr_t face_expr_from_name(const char *name);
const char *face_expr_name(face_expr_t e);

void face_render(gfx_t *g, const face_state_t *s);

// Layout numbers other modules need.
#define FACE_CAPTION_MAX_W 188  // widest subtitle line, pixels
