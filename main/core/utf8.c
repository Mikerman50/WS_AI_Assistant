#include "utf8.h"

int utf8_next(const char *s, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];
    int n;
    uint32_t v;
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; }
    else { *cp = 0xFFFD; return 1; }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (p[i] & 0x3F);
    }
    *cp = v;
    return n;
}

int utf8_put(char *out, uint32_t cp)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int utf8_count(const char *s)
{
    int n = 0;
    uint32_t cp;
    while (*s) {
        s += utf8_next(s, &cp);
        n++;
    }
    return n;
}

size_t utf8_truncate(const char *s, size_t n)
{
    if (n == 0) return 0;
    // Step back over continuation bytes to the start of the last character.
    size_t i = n;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;
    if (i == 0) return 0;
    unsigned char lead = (unsigned char)s[i - 1];
    size_t need = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
    return (n - (i - 1) >= need) ? n : i - 1;
}
