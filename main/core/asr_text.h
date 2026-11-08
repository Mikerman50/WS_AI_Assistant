// Tidies what the speech recogniser heard before it's shown and sent to Claude.
#pragma once

#include <stddef.h>

// The recogniser writes lower case with no punctuation ("whats the weather i
// mean tomorrow"). This trims and collapses spaces, capitalises the first
// letter, and writes the pronoun "i" (and i'm, i'll, i've, i'd) as "I".
// Always NUL-terminates; returns the length written.
size_t asr_tidy(const char *in, char *out, size_t outsz);
