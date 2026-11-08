// Renders the face in each state to PBM images using the real firmware drawing code.
//   make -C test/host preview   (writes test/host/build/*.pbm)
#include <stdio.h>
#include <string.h>

#include "captions.h"
#include "face.h"
#include "gfx.h"

static void save(const gfx_t *g, const char *dir, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P4\n%d %d\n", GFX_W, GFX_H);
    fwrite(g->bits, 1, sizeof g->bits, f);  // PBM: 1 = black, MSB first, same as the canvas
    fclose(f);
}

static int measure_caption(const char *s, int nbytes, void *ctx)
{
    (void)ctx;
    return gfx_text_width_n(&font_caption, s, nbytes);
}

static void set_caption(face_state_t *s, const char *text, int page)
{
    cap_layout_t L;
    captions_layout(&L, text, FACE_CAPTION_MAX_W, FACE_CAPTION_LINES, measure_caption, NULL);
    if (page >= L.npages) page = L.npages - 1;
    s->caption_lines = L.page[page].nlines;
    for (int i = 0; i < s->caption_lines; i++) snprintf(s->caption[i], FACE_TEXT_MAX, "%s", L.page[page].line[i]);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    gfx_t g;
    face_state_t s;

    face_state_init(&s);
    s.mode = MODE_LISTENING;
    s.level = 0.75f;  // nothing heard yet: the band says how to finish
    face_render(&g, &s);
    save(&g, dir, "1_listening");

    face_state_init(&s);
    s.mode = MODE_THINKING;
    s.expr = EXPR_THINKING;
    strcpy(s.heard, "will it rain tomorrow?");
    face_render(&g, &s);
    save(&g, dir, "2_thinking");

    face_state_init(&s);
    s.mode = MODE_SPEAKING;
    s.expr = EXPR_NEUTRAL;
    s.mouth = 0.7f;
    s.look = 0.3f;
    set_caption(&s, "Probably, yes. Rain from about two in the afternoon, so take a coat.", 0);
    face_render(&g, &s);
    save(&g, dir, "3_speaking");

    face_state_init(&s);
    s.mode = MODE_IDLE;
    strcpy(s.pinned, "Rain from 2pm, take a coat");
    face_render(&g, &s);
    save(&g, dir, "4_idle_pinned");
    // The longest status messages wrap onto two lines.
    static const char *const msgs[] = {"Model not flashed: use merged .bin", "No asr partition: flash full .bin",
                                       "No voice files: subtitles only", "Wrong data in model partition"};
    for (int i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof name, "4_idle_long%d", i);
        snprintf(s.pinned, sizeof s.pinned, "%s", msgs[i]);
        face_render(&g, &s);
        save(&g, dir, name);
    }

    face_state_init(&s);
    s.mode = MODE_ASLEEP;
    strcpy(s.pinned, "Hold PWR to wake");
    face_render(&g, &s);
    save(&g, dir, "5_asleep");

    face_state_init(&s);
    s.mode = MODE_SPEAKING;
    s.expr = EXPR_SORRY;
    s.mouth = 0.2f;
    set_caption(&s, "I couldn't reach Claude. Is the Wi-Fi up?", 0);
    face_render(&g, &s);
    save(&g, dir, "6_error");

    face_state_init(&s);
    s.mode = MODE_NOTICE;
    s.expr = EXPR_SORRY;
    set_caption(&s, "No config.json on the SD card.", 0);
    strcpy(s.pinned, "Fix it, then press BOOT");
    face_render(&g, &s);
    save(&g, dir, "7_notice");

    for (int e = 0; e < EXPR_COUNT; e++) {
        face_state_init(&s);
        s.expr = (face_expr_t)e;
        face_render(&g, &s);
        char name[32];
        snprintf(name, sizeof name, "expr_%d_%s", e, face_expr_name((face_expr_t)e));
        save(&g, dir, name);
    }
    printf("previews written to %s\n", dir);
    return 0;
}
