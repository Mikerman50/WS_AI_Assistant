#include "captions.h"

#include <stdbool.h>
#include <string.h>

#include "utf8.h"

typedef struct {
    cap_layout_t *out;
    int max_w, max_lines;
    cap_measure_fn measure;
    void *ctx;
    cap_page_t *pg;  // page being filled
    int line_len;    // bytes in its last line
} lay_t;

static bool is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
}

static char *cur_line(lay_t *L)
{
    return L->pg->line[L->pg->nlines - 1];
}

static bool new_line(lay_t *L, int first_cp)
{
    if (!L->pg || L->pg->nlines >= L->max_lines) {
        if (L->out->npages >= CAP_MAX_PAGES) return false;
        L->pg = &L->out->page[L->out->npages++];
        L->pg->nlines = 0;
        L->pg->first_cp = first_cp;
    }
    L->pg->nlines++;
    L->line_len = 0;
    cur_line(L)[0] = '\0';
    return true;
}

static bool fits(lay_t *L, const char *s, int n)
{
    return L->measure(s, n, L->ctx) <= L->max_w;
}

// Places one word (bytes w[0..wlen), code points [c0, c1)).
static void place_word(lay_t *L, const char *w, int wlen, int c0, int c1)
{
    if (L->pg && L->line_len > 0 && L->line_len + 1 + wlen < CAP_LINE_BYTES) {
        char *line = cur_line(L);
        line[L->line_len] = ' ';
        memcpy(line + L->line_len + 1, w, (size_t)wlen);
        int len = L->line_len + 1 + wlen;
        if (fits(L, line, len)) {
            L->line_len = len;
            line[len] = '\0';
            L->pg->last_cp = c1 - 1;
            return;
        }
        line[L->line_len] = '\0';
    }

    // Start a new line; split the word across lines if it is wider than a line.
    const char *p = w;
    int remaining = wlen, c = c0;
    while (remaining > 0) {
        if (!new_line(L, c)) return;
        int take = 0, take_cp = 0;
        while (take < remaining) {
            uint32_t cp;
            int k = utf8_next(p + take, &cp);
            if (take + k >= CAP_LINE_BYTES) break;
            if (take > 0 && !fits(L, p, take + k)) break;
            take += k;
            take_cp++;
        }
        memcpy(cur_line(L), p, (size_t)take);
        cur_line(L)[take] = '\0';
        L->line_len = take;
        L->pg->last_cp = c + take_cp - 1;
        p += take;
        remaining -= take;
        c += take_cp;
    }
}

int captions_layout(cap_layout_t *out, const char *text, int max_w, int max_lines,
                    cap_measure_fn measure, void *ctx)
{
    memset(out, 0, sizeof *out);
    if (max_lines < 1) max_lines = 1;
    if (max_lines > CAP_MAX_LINES) max_lines = CAP_MAX_LINES;
    lay_t L = {.out = out, .max_w = max_w, .max_lines = max_lines, .measure = measure, .ctx = ctx};

    const char *p = text;
    int cpi = 0;
    while (*p) {
        uint32_t cp;
        int k = utf8_next(p, &cp);
        if (is_space(cp)) {
            p += k;
            cpi++;
            continue;
        }
        const char *w0 = p;
        int c0 = cpi;
        while (*p) {
            k = utf8_next(p, &cp);
            if (is_space(cp)) break;
            p += k;
            cpi++;
        }
        place_word(&L, w0, (int)(p - w0), c0, cpi);
    }
    out->ncp = cpi;
    return out->npages;
}

uint32_t captions_estimate(const char *text, float cps, uint32_t *starts, int max)
{
    if (cps < 1.0f) cps = 14.0f;
    const float per = 1000.0f / cps;
    float t = 0.0f;
    int i = 0;
    const char *p = text;
    while (*p) {
        uint32_t cp;
        p += utf8_next(p, &cp);
        if (i < max) starts[i] = (uint32_t)t;
        i++;
        t += per;
        if (cp == ',' || cp == ';' || cp == ':' || cp == 0x2013 || cp == 0x2014) t += 200.0f;
        else if (cp == '.' || cp == '!' || cp == '?' || cp == 0x2026) t += 350.0f;
    }
    return (uint32_t)t;
}

void captions_apply_timing(cap_layout_t *L, const uint32_t *starts, const uint32_t *ends,
                           int n, uint32_t total_ms)
{
    for (int i = 0; i < L->npages; i++) {
        cap_page_t *p = &L->page[i];
        if (n <= 0) {
            p->start_ms = 0;
            p->end_ms = total_ms;
            continue;
        }
        int a = p->first_cp, b = p->last_cp;
        if (a < 0) a = 0;
        if (a >= n) a = n - 1;
        if (b >= n) b = n - 1;
        if (b < a) b = a;
        p->start_ms = starts[a];
        p->end_ms = ends ? ends[b] : (b + 1 < n ? starts[b + 1] : total_ms);
        if (p->end_ms < p->start_ms) p->end_ms = p->start_ms;
    }
    if (L->npages > 0) L->page[0].start_ms = 0;  // first page appears with the first sound
}

int captions_page_at(const cap_layout_t *L, uint32_t t_ms)
{
    int idx = 0;
    for (int i = 0; i < L->npages; i++)
        if (L->page[i].start_ms <= t_ms) idx = i;
    return idx;
}

float captions_progress(const cap_page_t *p, uint32_t t_ms)
{
    if (p->end_ms <= p->start_ms || t_ms >= p->end_ms) return 1.0f;
    if (t_ms <= p->start_ms) return 0.0f;
    return (float)(t_ms - p->start_ms) / (float)(p->end_ms - p->start_ms);
}
