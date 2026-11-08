#include "memory_text.h"

#include <ctype.h>
#include <string.h>

static void drop_first_line(memory_t *m)
{
    char *nl = memchr(m->text, '\n', m->len);
    size_t cut = nl ? (size_t)(nl - m->text) + 1 : m->len;
    memmove(m->text, m->text + cut, m->len - cut);
    m->len -= cut;
    m->text[m->len] = '\0';
}

// Appends one line (without its '\n'), making room if needed.
static void add_line(memory_t *m, const char *s, size_t n)
{
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\t')) n--;
    while (n > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        n--;
    }
    if (n == 0) return;
    if (n + 1 > MEMORY_MAX) n = MEMORY_MAX - 1;
    while (m->len + n + 1 > MEMORY_MAX) drop_first_line(m);
    memcpy(m->text + m->len, s, n);
    m->len += n;
    m->text[m->len++] = '\n';
    m->text[m->len] = '\0';
}

void memory_load(memory_t *m, const char *file_text)
{
    m->len = 0;
    m->text[0] = '\0';
    if (!file_text) return;
    const char *p = file_text;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        add_line(m, p, n);  // oldest lines fall out as newer ones arrive
        p += n + (e ? 1 : 0);
    }
}

// Lower case, without "- " in front or a full stop at the end, for comparing.
static size_t key(const char *s, size_t n, char *out, size_t outsz)
{
    while (n > 0 && (*s == '-' || *s == ' ')) {
        s++;
        n--;
    }
    while (n > 0 && (s[n - 1] == '.' || s[n - 1] == ' ')) n--;
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < outsz; i++) out[k++] = (char)tolower((unsigned char)s[i]);
    out[k] = '\0';
    return k;
}

bool memory_remember(memory_t *m, const char *fact)
{
    char want[256], have[256];
    if (key(fact, strlen(fact), want, sizeof want) == 0) return false;
    for (const char *p = m->text; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        key(p, n, have, sizeof have);
        if (strcmp(want, have) == 0) return false;
        p += n + (e ? 1 : 0);
    }
    char line[260];
    while (*fact == ' ') fact++;
    size_t n = strlen(fact);
    if (n > sizeof line - 3) n = sizeof line - 3;
    line[0] = '-';
    line[1] = ' ';
    memcpy(line + 2, fact, n);
    add_line(m, line, n + 2);
    return true;
}

static bool contains_ci(const char *hay, size_t n, const char *needle, size_t k)
{
    for (size_t i = 0; i + k <= n; i++) {
        size_t j = 0;
        while (j < k && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (j == k) return true;
    }
    return false;
}

int memory_forget(memory_t *m, const char *words)
{
    while (*words == ' ') words++;
    size_t k = strlen(words);
    while (k > 0 && (words[k - 1] == ' ' || words[k - 1] == '.')) k--;
    if (k < 3) return 0;
    int removed = 0;
    size_t o = 0;
    for (size_t i = 0; i < m->len;) {
        char *e = memchr(m->text + i, '\n', m->len - i);
        size_t n = e ? (size_t)(e - (m->text + i)) + 1 : m->len - i;
        if (contains_ci(m->text + i, n, words, k)) {
            removed++;
        } else {
            memmove(m->text + o, m->text + i, n);
            o += n;
        }
        i += n;
    }
    m->len = o;
    m->text[o] = '\0';
    return removed;
}

note_kind_t memory_parse_note(const char *note, const char **arg)
{
    static const struct {
        const char *word;
        note_kind_t kind;
    } k_words[] = {{"remember", NOTE_REMEMBER}, {"forget", NOTE_FORGET}};
    while (*note == ' ') note++;
    for (size_t i = 0; i < sizeof k_words / sizeof k_words[0]; i++) {
        size_t n = strlen(k_words[i].word);
        size_t j = 0;
        while (j < n && tolower((unsigned char)note[j]) == k_words[i].word[j]) j++;
        if (j == n) {
            const char *p = note + n;
            while (*p == ' ') p++;
            if (*p != ':') continue;
            p++;
            while (*p == ' ') p++;
            *arg = p;
            return k_words[i].kind;
        }
    }
    *arg = note;
    return NOTE_OTHER;
}
