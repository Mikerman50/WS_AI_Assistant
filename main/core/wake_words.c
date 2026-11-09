#include "wake_words.h"

#include <ctype.h>
#include <string.h>

// Settings from each model's manifest (micro-wake-word-models, models/v2).
const wake_word_t WAKE_WORDS[WAKE_WORD_COUNT] = {
    {"Alexa", "alexa", 0.90f, 5, 22348},
    {"Hey Jarvis", "hey_jarvis", 0.97f, 5, 22860},
    {"Hey Mycroft", "hey_mycroft", 0.95f, 5, 23628},
    {"Okay Nabu", "okay_nabu", 0.97f, 5, 26080},
};

// Lower case letters only.
static void squash(const char *in, char *out, size_t outsz)
{
    size_t n = 0;
    for (; in && *in && n + 1 < outsz; in++)
        if (isalpha((unsigned char)*in)) out[n++] = (char)tolower((unsigned char)*in);
    out[n] = '\0';
}

bool wake_word_is_off(const char *name)
{
    char s[32];
    squash(name, s, sizeof s);
    return !s[0] || !strcmp(s, "off") || !strcmp(s, "none") || !strcmp(s, "no");
}

const wake_word_t *wake_word_find(const char *name)
{
    char s[32];
    squash(name, s, sizeof s);
    if (!strcmp(s, "oknabu")) return &WAKE_WORDS[3];
    for (int i = 0; i < WAKE_WORD_COUNT; i++) {
        char w[32];
        squash(WAKE_WORDS[i].name, w, sizeof w);
        if (!strcmp(s, w)) return &WAKE_WORDS[i];
    }
    return NULL;
}

int wake_sens_from_name(const char *name)
{
    char s[16];
    squash(name, s, sizeof s);
    if (!strcmp(s, "low")) return WAKE_SENS_LOW;
    if (!strcmp(s, "normal") || !strcmp(s, "medium")) return WAKE_SENS_NORMAL;
    if (!strcmp(s, "high")) return WAKE_SENS_HIGH;
    return -1;
}

float wake_word_cutoff(const wake_word_t *w, wake_sens_t s)
{
    float c = w->cutoff;
    if (s == WAKE_SENS_HIGH) c -= 1.0f - c;           // 0.97 -> 0.94, 0.90 -> 0.80
    if (s == WAKE_SENS_LOW) c += (1.0f - c) * 0.5f;           // 0.97 -> 0.985, 0.90 -> 0.95
    return c;
}
