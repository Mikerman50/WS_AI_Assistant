#include "font.h"

#include <stddef.h>

const glyph_t *font_glyph(const font_t *f, uint32_t cp)
{
    int lo = 0, hi = (int)f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = f->glyphs[mid].cp;
        if (c == cp) return &f->glyphs[mid];
        if (c < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}
