#include "tts_text.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utf8.h"

typedef struct {
    char *out;
    size_t size, len;
    int full;
} buf_t;

static void put(buf_t *b, const char *s)
{
    size_t n = strlen(s);
    if (b->full || b->len + n + 1 > b->size) {
        b->full = 1;
        return;
    }
    memcpy(b->out + b->len, s, n);
    b->len += n;
    b->out[b->len] = '\0';
}

static void put_bytes(buf_t *b, const char *s, size_t n)
{
    if (b->full || b->len + n + 1 > b->size) {
        b->full = 1;
        return;
    }
    memcpy(b->out + b->len, s, n);
    b->len += n;
    b->out[b->len] = '\0';
}

size_t tts_prepare(const char *text, int speed_pct, int pitch_pct, char *out, size_t outsz)
{
    if (outsz == 0) return 0;
    out[0] = '\0';
    // Reserve room for the closing tags so they can't be the part that's cut off.
    static const char close_speed[] = "</speed>", close_pitch[] = "</pitch>";
    size_t reserve = (speed_pct != 100 ? sizeof close_speed : 0) + (pitch_pct != 100 ? sizeof close_pitch : 0);
    if (outsz <= reserve + 1) return 0;
    buf_t b = {out, outsz - reserve, 0, 0};

    char tag[40];
    if (speed_pct != 100) {
        snprintf(tag, sizeof tag, "<speed level=\"%d\">", speed_pct);
        put(&b, tag);
    }
    if (pitch_pct != 100) {
        snprintf(tag, sizeof tag, "<pitch level=\"%d\">", pitch_pct);
        put(&b, tag);
    }
    if (b.full) {  // not even room for the markup
        out[0] = '\0';
        return 0;
    }

    for (const char *p = text; *p && !b.full;) {
        uint32_t cp;
        int k = utf8_next(p, &cp);
        switch (cp) {
        case '<': case '>': put(&b, " "); break;
        case '&': put(&b, " and "); break;
        case 0x2018: case 0x2019: put(&b, "'"); break;    // curly single quotes
        case 0x201C: case 0x201D: put(&b, "\""); break;   // curly double quotes
        case 0x2013: case 0x2014: put(&b, ", "); break;   // en and em dashes read as a pause
        case 0x2026: put(&b, "..."); break;
        case '\r': case '\n': case '\t': put(&b, " "); break;
        default:
            if (cp >= 0x20) put_bytes(&b, p, (size_t)k);
            break;
        }
        p += k;
    }
    if (b.full) {  // cut on a character boundary
        b.len = utf8_truncate(out, b.len);
        out[b.len] = '\0';
    }

    b.size = outsz;  // now the closing tags, using the reserved room
    b.full = 0;
    if (pitch_pct != 100) put(&b, close_pitch);
    if (speed_pct != 100) put(&b, close_speed);
    return b.len;
}
