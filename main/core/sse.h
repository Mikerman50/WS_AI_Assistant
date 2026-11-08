// Incremental Server-Sent Events parser (the format Claude's streaming API uses).
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Called once per event. `data` is NUL-terminated; multi-line data is joined with '\n'.
// Events whose data exceeded the size limit are dropped.
typedef void (*sse_event_fn)(const char *event, const char *data, void *ctx);

typedef struct {
    sse_event_fn on_event;
    void *ctx;
    size_t max;  // largest line/data accepted
    char *line;
    size_t line_len, line_cap;
    char *data;
    size_t data_len, data_cap;
    bool has_data;
    bool overflow;
    bool last_cr;
    char event[48];
} sse_t;

bool sse_init(sse_t *s, size_t max_bytes, sse_event_fn on_event, void *ctx);
void sse_feed(sse_t *s, const char *buf, size_t n);
void sse_free(sse_t *s);
