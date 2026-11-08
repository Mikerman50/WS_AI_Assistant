// The conversation loop: question -> thinking face -> Claude -> speaker.
#pragma once

#include <stdbool.h>

void assistant_init(void);

// Queues a question (copied). Questions arriving while busy wait their turn.
void assistant_ask(const char *question);

bool assistant_busy(void);
