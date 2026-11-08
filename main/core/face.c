#include "face.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#define CX 100        // face centre
#define EYE_DX 29     // eye centres at CX +/- EYE_DX
#define EYE_W 30
#define CAPTION_BASE 190  // baseline of the bottom subtitle line
#define CAPTION_STEP 22   // baseline-to-baseline distance
#define NOTICE_BASE 160   // bottom notice line, above the 30 px band

static const char *const k_names[EXPR_COUNT] = {
    "neutral", "happy", "amused", "thinking", "curious", "surprised", "sorry", "sleepy",
};

static const struct {
    const char *alias;
    face_expr_t expr;
} k_aliases[] = {
    {"smile", EXPR_HAPPY},      {"glad", EXPR_HAPPY},         {"excited", EXPR_HAPPY},
    {"laugh", EXPR_AMUSED},     {"playful", EXPR_AMUSED},     {"wink", EXPR_AMUSED},
    {"pondering", EXPR_THINKING}, {"hmm", EXPR_THINKING},     {"confused", EXPR_CURIOUS},
    {"interested", EXPR_CURIOUS}, {"shocked", EXPR_SURPRISED}, {"wow", EXPR_SURPRISED},
    {"sad", EXPR_SORRY},        {"apologetic", EXPR_SORRY},   {"tired", EXPR_SLEEPY},
    {"calm", EXPR_NEUTRAL},
};

void face_state_init(face_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->mode = MODE_IDLE;
    s->expr = EXPR_NEUTRAL;
    s->look = 0.5f;
}

face_expr_t face_expr_from_name(const char *name)
{
    char low[24];
    size_t n = 0;
    for (; name[n] && n < sizeof low - 1; n++) low[n] = (char)tolower((unsigned char)name[n]);
    low[n] = '\0';
    for (int i = 0; i < EXPR_COUNT; i++)
        if (strcmp(low, k_names[i]) == 0) return (face_expr_t)i;
    for (size_t i = 0; i < sizeof k_aliases / sizeof k_aliases[0]; i++)
        if (strcmp(low, k_aliases[i].alias) == 0) return k_aliases[i].expr;
    return EXPR_COUNT;
}

const char *face_expr_name(face_expr_t e)
{
    return e < EXPR_COUNT ? k_names[e] : "?";
}

static void solid_eye(gfx_t *g, int cx, int cy, int w, int h, int look)
{
    gfx_fill_round_rect(g, cx - w / 2 + look, cy - h / 2, w, h, 13, true);
    gfx_disc(g, cx + 6 + look + look / 2, cy - h / 2 + 10, 4, false);  // catch-light
}

static void draw_eye(gfx_t *g, face_expr_t e, int cx, int cy, bool left, int look)
{
    switch (e) {
    case EXPR_HAPPY:
        gfx_stroke_quad(g, cx - 14, cy + 6, cx, cy - 16, cx + 14, cy + 6, 5, true);
        break;
    case EXPR_AMUSED:
        solid_eye(g, cx, cy, EYE_W, 42, look);
        gfx_fill_ellipse(g, cx + look, cy + 25, 24, 13, false);  // cheek pushes the eye into a crescent
        break;
    case EXPR_THINKING:
        solid_eye(g, cx + 6, cy - 8, EYE_W, 36, look);
        break;
    case EXPR_CURIOUS:
        if (left) solid_eye(g, cx, cy, EYE_W + 2, 46, look);
        else solid_eye(g, cx, cy + 4, EYE_W - 2, 30, look);
        break;
    case EXPR_SURPRISED:
        solid_eye(g, cx, cy, EYE_W + 2, 50, look);
        break;
    case EXPR_SORRY:
        solid_eye(g, cx, cy, EYE_W, 40, look);
        if (left) gfx_fill_triangle(g, cx - 19, cy - 22, cx + 19, cy - 22, cx - 19, cy - 6, false);
        else gfx_fill_triangle(g, cx - 19, cy - 22, cx + 19, cy - 22, cx + 19, cy - 6, false);
        break;
    case EXPR_SLEEPY:
        gfx_fill_round_rect(g, cx - EYE_W / 2 + look, cy - 2, EYE_W, 16, 7, true);
        gfx_stroke_line(g, cx - 17, cy - 4, cx + 17, cy - 4, 3, true);
        break;
    case EXPR_NEUTRAL:
    default:
        solid_eye(g, cx, cy, EYE_W, 42, look);
        break;
    }
}

static void ring(gfx_t *g, int cx, int cy, int rx, int ry, int t)
{
    gfx_fill_ellipse(g, cx, cy, rx, ry, true);
    gfx_fill_ellipse(g, cx, cy, rx - t, ry - t, false);
}

static void draw_mouth(gfx_t *g, face_expr_t e, int my)
{
    switch (e) {
    case EXPR_HAPPY:
    case EXPR_AMUSED:
        gfx_stroke_quad(g, CX - 22, my - 4, CX, my + 18, CX + 22, my - 4, 5, true);
        break;
    case EXPR_THINKING:
        gfx_stroke_line(g, CX + 2, my + 2, CX + 18, my - 2, 4, true);
        break;
    case EXPR_CURIOUS:
        ring(g, CX, my, 7, 8, 4);
        break;
    case EXPR_SURPRISED:
        ring(g, CX, my + 2, 10, 13, 4);
        break;
    case EXPR_SORRY:
        gfx_stroke_quad(g, CX - 14, my + 6, CX, my - 6, CX + 14, my + 6, 4, true);
        break;
    case EXPR_SLEEPY:
        gfx_stroke_line(g, CX - 6, my, CX + 6, my, 3, true);
        break;
    case EXPR_NEUTRAL:
    default:
        gfx_stroke_quad(g, CX - 12, my, CX, my + 8, CX + 12, my, 4, true);
        break;
    }
}

static void band(gfx_t *g, int y, int h, const char *text, int baseline)
{
    char fit[FACE_TEXT_MAX + 8];
    gfx_fill_rect(g, 0, y, GFX_W, h, true);
    gfx_fit_text(&font_small, text, GFX_W - 12, fit, sizeof fit);
    int w = gfx_text_width(&font_small, fit);
    gfx_text(g, &font_small, (GFX_W - w) / 2, baseline, fit, false);
}

// The bottom band: one line when the text fits, otherwise two (split at the
// last space that lets the first line fit), so a message is shown whole.
static void bottom_band(gfx_t *g, const char *text)
{
    const int max_w = GFX_W - 12;
    if (gfx_text_width(&font_small, text) <= max_w) {
        band(g, GFX_H - 30, 30, text, GFX_H - 10);
        return;
    }
    char first[FACE_TEXT_MAX];
    int split = -1;
    for (int i = 0; text[i] && i < FACE_TEXT_MAX - 1; i++)
        if (text[i] == ' ' && gfx_text_width_n(&font_small, text, i) <= max_w) split = i;
    if (split < 0) {  // one long word: shrink it to one line with an ellipsis
        band(g, GFX_H - 30, 30, text, GFX_H - 10);
        return;
    }
    memcpy(first, text, (size_t)split);
    first[split] = '\0';
    const int step = 18;  // baseline to baseline
    gfx_fill_rect(g, 0, GFX_H - 30 - step, GFX_W, 30 + step, true);
    int w = gfx_text_width(&font_small, first);
    gfx_text(g, &font_small, (GFX_W - w) / 2, GFX_H - 10 - step, first, false);
    char rest[FACE_TEXT_MAX + 8];
    gfx_fit_text(&font_small, text + split + 1, max_w, rest, sizeof rest);
    w = gfx_text_width(&font_small, rest);
    gfx_text(g, &font_small, (GFX_W - w) / 2, GFX_H - 10, rest, false);
}

void face_render(gfx_t *g, const face_state_t *s)
{
    gfx_clear(g);

    const bool speaking = s->mode == MODE_SPEAKING;
    const bool notice = s->mode == MODE_NOTICE;
    // Top band: what it heard, or while listening with nothing heard yet, how to finish.
    const bool strip = s->mode == MODE_LISTENING || (s->heard[0] && s->mode == MODE_THINKING);
    const int ecy = (speaking || notice) ? 52 : (strip ? 80 : 72);
    const int my = speaking ? ecy + 54 : ecy + 50;

    float look = s->look < 0.0f ? 0.0f : (s->look > 1.0f ? 1.0f : s->look);
    // Reading along while speaking moves the eyes a little; a glance while idle, more.
    int lx = speaking ? (int)lroundf((look - 0.5f) * 8.0f)
                      : (s->mode == MODE_IDLE ? (int)lroundf((look - 0.5f) * 28.0f) : 0);

    if (s->mode == MODE_ASLEEP) {
        gfx_stroke_quad(g, CX - EYE_DX - 15, ecy + 6, CX - EYE_DX, ecy + 18, CX - EYE_DX + 15, ecy + 6, 5, true);
        gfx_stroke_quad(g, CX + EYE_DX - 15, ecy + 6, CX + EYE_DX, ecy + 18, CX + EYE_DX + 15, ecy + 6, 5, true);
        gfx_stroke_quad(g, CX - 10, my - 2, CX, my + 4, CX + 10, my - 2, 4, true);
        gfx_text(g, &font_caption, 150, 50, "z", true);
        gfx_text(g, &font_small, 165, 34, "z", true);
    } else {
        if (s->blink && s->mode == MODE_IDLE) {  // shut for a moment: two gentle arcs
            gfx_stroke_quad(g, CX - EYE_DX - 15 + lx, ecy + 4, CX - EYE_DX + lx, ecy + 12, CX - EYE_DX + 15 + lx,
                            ecy + 4, 5, true);
            gfx_stroke_quad(g, CX + EYE_DX - 15 + lx, ecy + 4, CX + EYE_DX + lx, ecy + 12, CX + EYE_DX + 15 + lx,
                            ecy + 4, 5, true);
        } else {
            draw_eye(g, s->expr, CX - EYE_DX, ecy, true, lx);
            draw_eye(g, s->expr, CX + EYE_DX, ecy, false, lx);
        }
        if (s->mode == MODE_IDLE && s->prop[0]) gfx_text(g, &font_caption, 158, 44, s->prop, true);
        if (speaking) {
            float m = s->mouth < 0.0f ? 0.0f : (s->mouth > 1.0f ? 1.0f : s->mouth);
            gfx_fill_ellipse(g, CX, my, 18, 2 + (int)lroundf(m * 12.0f), true);
        } else if (s->mode == MODE_THINKING) {
            for (int i = -1; i <= 1; i++) gfx_disc(g, CX + i * 14, my, 4, true);
        } else if (s->mode == MODE_LISTENING) {
            ring(g, CX, my, 7, 8, 4);
        } else if (!notice) {  // a notice's text sits where the mouth would be
            draw_mouth(g, s->expr, my);
        }
    }

    if (strip) {
        char line[FACE_TEXT_MAX + 8];
        if (s->heard[0]) {
            strcpy(line, "you: ");
            strncat(line, s->heard, sizeof line - strlen(line) - 1);
        } else {
            strcpy(line, "Tap BOOT when done");
        }
        band(g, 0, 28, line, 19);
    }

    if (s->mode == MODE_LISTENING) {
        static const float pattern[5] = {0.45f, 0.8f, 1.0f, 0.7f, 0.35f};
        float lv = s->level < 0.0f ? 0.0f : (s->level > 1.0f ? 1.0f : s->level);
        for (int i = 0; i < 5; i++) {
            int h = 4 + (int)lroundf(lv * 30.0f * pattern[i]);
            gfx_fill_round_rect(g, CX - 27 + i * 12, 184 - h, 6, h, 2, true);
        }
    }

    if ((speaking || notice) && s->caption_lines > 0) {
        int n = s->caption_lines > FACE_CAPTION_LINES ? FACE_CAPTION_LINES : s->caption_lines;
        int base = notice ? NOTICE_BASE : CAPTION_BASE;  // a notice leaves room for the band
        for (int i = 0; i < n; i++) {
            int baseline = base - (n - 1 - i) * CAPTION_STEP;
            int w = gfx_text_width(&font_caption, s->caption[i]);
            gfx_text_halo(g, &font_caption, (GFX_W - w) / 2, baseline, s->caption[i], 3);
        }
    }

    if ((s->mode == MODE_IDLE || s->mode == MODE_ASLEEP || notice) && s->pinned[0])
        bottom_band(g, s->pinned);
}
