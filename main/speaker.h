// Turns sentences into speech with timed subtitles and a moving mouth.
//
// Milestone 1 (speaker_paced.c) has no audio yet: subtitles are paced at
// speaking speed. Milestone 2 swaps in a version that streams ElevenLabs speech
// and drives the subtitles from its per-character timings, with this same
// interface, so nothing else changes.
#pragma once

#include <stdbool.h>

// pace_cps: how fast subtitles advance while there is no audio (characters per second).
void speaker_init(int pace_cps);

// Queues a sentence (copied). `cue` is a face cue name or "".
void speaker_say(const char *sentence, const char *cue);

// Marks the end of a reply; the face goes idle once everything has played.
void speaker_end_reply(void);

// Says a short error message with the sorry face.
void speaker_error(const char *message);

bool speaker_busy(void);
