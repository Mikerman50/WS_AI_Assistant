#include "text_stream.h"

#include <ctype.h>
#include <string.h>

#include "utf8.h"

void ts_init(text_stream_t *t, ts_sentence_fn on_sentence, void *ctx)
{
    memset(t, 0, sizeof *t);
    t->on_sentence = on_sentence;
    t->ctx = ctx;
    t->min_len = 12;
    t->max_len = 180;
}

static bool is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
}

static bool is_closer(uint32_t cp)
{
    return cp == '"' || cp == '\'' || cp == ')' || cp == 0x2019 || cp == 0x201D;
}

static bool is_cue_char(uint32_t cp)
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_' || cp == '-' || cp == ' ';
}

static bool is_dropped(uint32_t cp)
{
    return cp >= 0x1F000                       // emoji, pictographs, flags
           || (cp >= 0x2600 && cp <= 0x27BF)   // misc symbols, dingbats
           || cp == 0xFE0F || cp == 0x200D     // emoji presentation, zero-width joiner
           || cp == '*' || cp == '#' || cp == '`';  // markdown
}

static void emit(text_stream_t *t)
{
    while (t->len > 0 && t->buf[t->len - 1] == ' ') t->len--;
    t->buf[t->len] = '\0';
    if (t->len > 0 && t->on_sentence) {
        t->on_sentence(t->buf, t->next_cue, t->ctx);
        t->next_cue[0] = '\0';
    }
    t->len = 0;
    t->buf[0] = '\0';
    t->pending_end = false;
}

// Cuts an over-long sentence at the last comma (or space) and emits the first part.
static void force_cut(text_stream_t *t)
{
    int cut = -1;
    for (int i = t->len - 1; i > t->min_len && cut < 0; i--)
        if (t->buf[i] == ' ' && (t->buf[i - 1] == ',' || t->buf[i - 1] == ';' || t->buf[i - 1] == ':'))
            cut = i;
    for (int i = t->len - 1; i > t->min_len && cut < 0; i--)
        if (t->buf[i] == ' ') cut = i;
    if (cut < 0) {
        emit(t);
        return;
    }
    char rest[TS_BUF];
    int rl = t->len - cut - 1;
    memcpy(rest, t->buf + cut + 1, (size_t)rl);
    t->len = cut;
    emit(t);
    memcpy(t->buf, rest, (size_t)rl);
    t->len = rl;
    t->buf[rl] = '\0';
}

static bool ends_with_abbrev(const text_stream_t *t)
{
    static const char *const abbr[] = {"mr", "mrs", "ms", "dr", "prof", "st", "vs", "e.g", "i.e",
                                       "eg", "ie", "approx", "jr", "sr", "mt"};
    int e = t->len - 1;
    while (e >= 0 && (t->buf[e] == '"' || t->buf[e] == '\'' || t->buf[e] == ')')) e--;
    if (e < 0 || t->buf[e] != '.') return false;
    if (e >= 2 && t->buf[e - 1] == '.' && t->buf[e - 2] == '.') return false;  // "..." ends a sentence
    int s = e - 1;
    while (s >= 0 && t->buf[s] != ' ') s--;
    s++;
    int wl = e - s;
    if (wl <= 0) return false;
    if (wl == 1 && isalpha((unsigned char)t->buf[s])) return true;  // an initial, as in "J. Smith"
    char w[12];
    if (wl >= (int)sizeof w) return false;
    for (int i = 0; i < wl; i++) w[i] = (char)tolower((unsigned char)t->buf[s + i]);
    w[wl] = '\0';
    for (size_t i = 0; i < sizeof abbr / sizeof abbr[0]; i++)
        if (strcmp(w, abbr[i]) == 0) return true;
    return false;
}

static void append(text_stream_t *t, uint32_t cp)
{
    if (t->len + 5 >= TS_BUF) force_cut(t);
    if (t->len + 5 >= TS_BUF) emit(t);
    // "Great !" (an emoji was dropped in between) -> "Great!"
    if ((cp == '.' || cp == ',' || cp == '!' || cp == '?' || cp == ';' || cp == ':') &&
        t->len > 0 && t->buf[t->len - 1] == ' ')
        t->len--;
    t->len += utf8_put(t->buf + t->len, cp);
    t->buf[t->len] = '\0';
}

static void add_text(text_stream_t *t, uint32_t cp)
{
    const bool space = is_space(cp);

    if (t->pending_end) {
        if (space) {
            t->pending_end = false;
            if (t->len >= t->min_len && !ends_with_abbrev(t)) {
                emit(t);
                return;
            }
        } else if (!(is_closer(cp) || cp == '.' || cp == '!' || cp == '?')) {
            t->pending_end = false;  // "3.5", "example.com"
        }
    }

    if (cp == '\n') {  // a line break always ends a sentence
        if (t->len > 0) emit(t);
        return;
    }
    if (space) {
        if (t->len > 0 && t->buf[t->len - 1] != ' ') append(t, ' ');
        return;
    }
    append(t, cp);
    if (cp == '.' || cp == '!' || cp == '?' || cp == 0x2026) t->pending_end = true;
    if (t->len > t->max_len) force_cut(t);
}

static void abandon_cue(text_stream_t *t)
{
    t->in_cue = false;
    add_text(t, '[');
    for (int i = 0; i < t->cue_len; i++) add_text(t, (unsigned char)t->cue[i]);
    t->cue_len = 0;
}

static void process(text_stream_t *t, uint32_t cp)
{
    if (is_dropped(cp)) return;

    if (t->in_cue) {
        if (cp == ']') {
            int n = t->cue_len;
            while (n > 0 && t->cue[n - 1] == ' ') n--;
            int s = 0;
            while (s < n && t->cue[s] == ' ') s++;
            if (n > s) {
                memcpy(t->next_cue, t->cue + s, (size_t)(n - s));
                t->next_cue[n - s] = '\0';
            }
            t->in_cue = false;
            t->cue_len = 0;
            return;
        }
        if (is_cue_char(cp) && t->cue_len < (int)sizeof t->cue - 1) {
            t->cue[t->cue_len++] = (char)tolower((int)cp);
            return;
        }
        abandon_cue(t);  // not a cue after all; fall through and handle cp as text
    }
    if (cp == '[') {
        t->in_cue = true;
        t->cue_len = 0;
        return;
    }
    add_text(t, cp);
}

void ts_feed(text_stream_t *t, const char *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char b = (unsigned char)data[i];
        if (t->u8_need > 0) {
            if ((b & 0xC0) == 0x80) {
                t->u8[t->u8_have++] = (char)b;
                if (t->u8_have == t->u8_need) {
                    uint32_t cp;
                    utf8_next(t->u8, &cp);
                    t->u8_need = 0;
                    process(t, cp);
                }
                continue;
            }
            t->u8_need = 0;  // broken sequence: drop it and treat b as a fresh byte
        }
        if (b < 0x80) {
            process(t, b);
            continue;
        }
        int need = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
        if (need == 0) continue;  // stray continuation byte
        t->u8[0] = (char)b;
        t->u8_have = 1;
        t->u8_need = need;
    }
}

void ts_finish(text_stream_t *t)
{
    if (t->in_cue) abandon_cue(t);
    t->pending_end = false;
    t->u8_need = 0;
    if (t->len > 0) emit(t);
}
