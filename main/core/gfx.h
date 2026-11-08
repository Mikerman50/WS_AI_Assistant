// 200x200 1-bit drawing for the e-paper face. Pure C, no ESP-IDF dependencies.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

#define GFX_W 200
#define GFX_H 200
#define GFX_STRIDE (GFX_W / 8)

typedef struct {
    uint8_t bits[GFX_STRIDE * GFX_H];  // 1 = ink (black), 0 = paper (white); MSB = leftmost
} gfx_t;

void gfx_clear(gfx_t *g);
void gfx_pixel(gfx_t *g, int x, int y, bool ink);
bool gfx_get(const gfx_t *g, int x, int y);
void gfx_hspan(gfx_t *g, int x0, int x1, int y, bool ink);
void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, bool ink);
void gfx_fill_round_rect(gfx_t *g, int x, int y, int w, int h, int r, bool ink);
void gfx_fill_ellipse(gfx_t *g, int cx, int cy, int rx, int ry, bool ink);
void gfx_disc(gfx_t *g, int cx, int cy, int r, bool ink);
void gfx_fill_triangle(gfx_t *g, int x0, int y0, int x1, int y1, int x2, int y2, bool ink);
void gfx_stroke_line(gfx_t *g, int x0, int y0, int x1, int y1, int thick, bool ink);
// Quadratic Bezier from (x0,y0) to (x1,y1) pulled towards (cx,cy).
void gfx_stroke_quad(gfx_t *g, int x0, int y0, int cx, int cy, int x1, int y1, int thick, bool ink);

// Text is UTF-8. Missing glyphs are drawn as '?'.
int gfx_text_width(const font_t *f, const char *s);
int gfx_text_width_n(const font_t *f, const char *s, int nbytes);
void gfx_text(gfx_t *g, const font_t *f, int x, int baseline, const char *s, bool ink);
// Black text with a paper-coloured outline `halo` pixels wide, readable over anything.
void gfx_text_halo(gfx_t *g, const font_t *f, int x, int baseline, const char *s, int halo);
// Copies `s` into `out`, shortened with an ellipsis if needed so it fits in `max_w` pixels.
void gfx_fit_text(const font_t *f, const char *s, int max_w, char *out, int outsz);
