#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "utf8.h"

void gfx_clear(gfx_t *g)
{
    memset(g->bits, 0, sizeof g->bits);
}

void gfx_pixel(gfx_t *g, int x, int y, bool ink)
{
    if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H) return;
    uint8_t *b = &g->bits[y * GFX_STRIDE + (x >> 3)];
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (ink) *b |= m;
    else *b &= (uint8_t)~m;
}

bool gfx_get(const gfx_t *g, int x, int y)
{
    if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H) return false;
    return (g->bits[y * GFX_STRIDE + (x >> 3)] & (0x80 >> (x & 7))) != 0;
}

void gfx_hspan(gfx_t *g, int x0, int x1, int y, bool ink)
{
    if ((unsigned)y >= GFX_H || x0 > x1) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= GFX_W) x1 = GFX_W - 1;
    for (int x = x0; x <= x1; x++) gfx_pixel(g, x, y, ink);
}

void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, bool ink)
{
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) gfx_hspan(g, x, x + w - 1, y + j, ink);
}

void gfx_fill_round_rect(gfx_t *g, int x, int y, int w, int h, int r, bool ink)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    for (int j = 0; j < h; j++) {
        float dy = 0.0f;
        if (j < r) dy = (float)r - (float)j - 0.5f;
        else if (j >= h - r) dy = (float)(j - (h - r)) + 0.5f;
        int inset = 0;
        if (dy > 0.0f) {
            float dx = sqrtf((float)(r * r) - dy * dy);
            inset = (int)lroundf((float)r - dx);
        }
        gfx_hspan(g, x + inset, x + w - 1 - inset, y + j, ink);
    }
}

void gfx_fill_ellipse(gfx_t *g, int cx, int cy, int rx, int ry, bool ink)
{
    if (rx < 0 || ry < 0) return;
    if (ry == 0) {
        gfx_hspan(g, cx - rx, cx + rx, cy, ink);
        return;
    }
    for (int j = -ry; j <= ry; j++) {
        float t = (float)j / ((float)ry + 0.5f);
        float s = 1.0f - t * t;
        if (s < 0.0f) s = 0.0f;
        int dx = (int)(((float)rx + 0.5f) * sqrtf(s));
        if (dx > rx) dx = rx;
        gfx_hspan(g, cx - dx, cx + dx, cy + j, ink);
    }
}

void gfx_disc(gfx_t *g, int cx, int cy, int r, bool ink)
{
    if (r <= 0) {
        gfx_pixel(g, cx, cy, ink);
        return;
    }
    gfx_fill_ellipse(g, cx, cy, r, r, ink);
}

static int edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

void gfx_fill_triangle(gfx_t *g, int x0, int y0, int x1, int y1, int x2, int y2, bool ink)
{
    int area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= GFX_W) maxx = GFX_W - 1;
    if (maxy >= GFX_H) maxy = GFX_H - 1;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            int w0 = edge(x1, y1, x2, y2, x, y);
            int w1 = edge(x2, y2, x0, y0, x, y);
            int w2 = edge(x0, y0, x1, y1, x, y);
            bool inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                   : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside) gfx_pixel(g, x, y, ink);
        }
    }
}

static void stamp(gfx_t *g, int x, int y, int thick, bool ink)
{
    if (thick <= 1) gfx_pixel(g, x, y, ink);
    else gfx_disc(g, x, y, thick / 2, ink);
}

void gfx_stroke_line(gfx_t *g, int x0, int y0, int x1, int y1, int thick, bool ink)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (steps == 0) {
        stamp(g, x0, y0, thick, ink);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        stamp(g, (int)lroundf((float)x0 + (float)dx * t), (int)lroundf((float)y0 + (float)dy * t), thick, ink);
    }
}

void gfx_stroke_quad(gfx_t *g, int x0, int y0, int cx, int cy, int x1, int y1, int thick, bool ink)
{
    const int n = 64;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n, u = 1.0f - t;
        float x = u * u * (float)x0 + 2.0f * u * t * (float)cx + t * t * (float)x1;
        float y = u * u * (float)y0 + 2.0f * u * t * (float)cy + t * t * (float)y1;
        stamp(g, (int)lroundf(x), (int)lroundf(y), thick, ink);
    }
}

static const glyph_t *glyph_or_q(const font_t *f, uint32_t cp)
{
    const glyph_t *gl = font_glyph(f, cp);
    return gl ? gl : font_glyph(f, '?');
}

int gfx_text_width_n(const font_t *f, const char *s, int nbytes)
{
    int w = 0;
    const char *end = s + nbytes;
    while (s < end && *s) {
        uint32_t cp;
        s += utf8_next(s, &cp);
        const glyph_t *gl = glyph_or_q(f, cp);
        if (gl) w += gl->adv;
    }
    return w;
}

int gfx_text_width(const font_t *f, const char *s)
{
    return gfx_text_width_n(f, s, (int)strlen(s));
}

typedef void (*plot_fn)(gfx_t *g, int x, int y, int arg);

static void walk_text(gfx_t *g, const font_t *f, int x, int baseline, const char *s, plot_fn plot, int arg)
{
    int pen = x;
    while (*s) {
        uint32_t cp;
        s += utf8_next(s, &cp);
        const glyph_t *gl = glyph_or_q(f, cp);
        if (!gl) continue;
        const uint8_t *bm = f->bitmap + gl->offset;
        int bpr = (gl->w + 7) / 8;
        for (int r = 0; r < gl->h; r++) {
            for (int c = 0; c < gl->w; c++) {
                if (bm[r * bpr + (c >> 3)] & (0x80 >> (c & 7)))
                    plot(g, pen + gl->xoff + c, baseline + gl->yoff + r, arg);
            }
        }
        pen += gl->adv;
    }
}

static void plot_ink(gfx_t *g, int x, int y, int ink)
{
    gfx_pixel(g, x, y, ink != 0);
}

static void plot_halo(gfx_t *g, int x, int y, int r)
{
    gfx_disc(g, x, y, r, false);
}

void gfx_text(gfx_t *g, const font_t *f, int x, int baseline, const char *s, bool ink)
{
    walk_text(g, f, x, baseline, s, plot_ink, ink ? 1 : 0);
}

void gfx_text_halo(gfx_t *g, const font_t *f, int x, int baseline, const char *s, int halo)
{
    if (halo > 0) walk_text(g, f, x, baseline, s, plot_halo, halo);
    walk_text(g, f, x, baseline, s, plot_ink, 1);
}

void gfx_fit_text(const font_t *f, const char *s, int max_w, char *out, int outsz)
{
    static const char ell[] = "\xE2\x80\xA6";  // U+2026 horizontal ellipsis
    if (outsz <= 0) return;
    int len = (int)strlen(s);
    if (len < outsz && gfx_text_width(f, s) <= max_w) {
        memcpy(out, s, (size_t)len + 1);
        return;
    }
    if (outsz < 4) {
        out[0] = '\0';
        return;
    }
    int ell_w = gfx_text_width(f, ell);
    int w = 0;
    size_t n = 0;  // bytes of `s` that fit, with room left for the ellipsis
    const char *p = s;
    while (*p) {
        uint32_t cp;
        int k = utf8_next(p, &cp);
        const glyph_t *gl = glyph_or_q(f, cp);
        int a = gl ? gl->adv : 0;
        if (w + a + ell_w > max_w || n + (size_t)k + 3 >= (size_t)outsz) break;
        w += a;
        n += (size_t)k;
        p += k;
    }
    while (n > 0 && s[n - 1] == ' ') n--;
    memcpy(out, s, n);
    memcpy(out + n, ell, 3);
    out[n + 3] = '\0';
}
