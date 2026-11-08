#include "asr_text.h"

#include <stdbool.h>
#include <string.h>

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// "i" on its own, or followed by an apostrophe (i'm, i'll, i've, i'd).
static bool is_pronoun_i(const char *w, size_t len)
{
    return w[0] == 'i' && (len == 1 || w[1] == '\'');
}

size_t asr_tidy(const char *in, char *out, size_t outsz)
{
    if (outsz == 0) return 0;
    size_t o = 0;
    const char *p = in;
    while (*p) {
        while (is_space(*p)) p++;
        if (!*p) break;
        const char *w = p;
        while (*p && !is_space(*p)) p++;
        size_t len = (size_t)(p - w);
        size_t need = (o ? 1 : 0) + len;
        if (o + need + 1 > outsz) break;  // whole words only
        if (o) out[o++] = ' ';
        memcpy(out + o, w, len);
        if (is_pronoun_i(w, len)) out[o] = 'I';
        o += len;
    }
    out[o] = '\0';
    if (o && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
    return o;
}
