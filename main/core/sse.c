#include "sse.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
// Search results and the like can make single events large, so keep buffers in PSRAM.
#define SSE_REALLOC(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define SSE_REALLOC(p, n) realloc((p), (n))
#endif

static bool grow(char **buf, size_t *cap, size_t need, size_t max)
{
    if (need <= *cap) return true;
    if (need > max) return false;
    size_t ncap = *cap ? *cap : 256;
    while (ncap < need) ncap *= 2;
    if (ncap > max) ncap = max;
    char *p = SSE_REALLOC(*buf, ncap);
    if (!p) return false;
    *buf = p;
    *cap = ncap;
    return true;
}

bool sse_init(sse_t *s, size_t max_bytes, sse_event_fn on_event, void *ctx)
{
    memset(s, 0, sizeof *s);
    s->on_event = on_event;
    s->ctx = ctx;
    s->max = max_bytes < 64 ? 64 : max_bytes;
    size_t first = s->max < 512 ? s->max : 512;
    return grow(&s->line, &s->line_cap, first, s->max) && grow(&s->data, &s->data_cap, first, s->max);
}

void sse_free(sse_t *s)
{
    free(s->line);
    free(s->data);
    s->line = s->data = NULL;
    s->line_cap = s->data_cap = 0;
}

static void dispatch(sse_t *s)
{
    if (s->has_data && !s->overflow && s->on_event) {
        s->data[s->data_len] = '\0';
        s->on_event(s->event[0] ? s->event : "message", s->data, s->ctx);
    }
    s->data_len = 0;
    s->has_data = false;
    s->overflow = false;
    s->event[0] = '\0';
}

static void process_line(sse_t *s)
{
    char *line = s->line;
    size_t len = s->line_len;
    if (len == 0) {
        dispatch(s);
        return;
    }
    if (line[0] == ':') return;  // comment / keep-alive

    char *colon = memchr(line, ':', len);
    size_t flen = colon ? (size_t)(colon - line) : len;
    const char *val = colon ? colon + 1 : line + len;
    size_t vlen = colon ? len - flen - 1 : 0;
    if (vlen > 0 && *val == ' ') {
        val++;
        vlen--;
    }

    if (flen == 5 && memcmp(line, "event", 5) == 0) {
        size_t n = vlen < sizeof s->event - 1 ? vlen : sizeof s->event - 1;
        memcpy(s->event, val, n);
        s->event[n] = '\0';
    } else if (flen == 4 && memcmp(line, "data", 4) == 0) {
        size_t need = s->data_len + (s->has_data ? 1 : 0) + vlen + 1;
        if (s->overflow || !grow(&s->data, &s->data_cap, need, s->max)) {
            s->overflow = true;
            s->has_data = true;
            return;
        }
        if (s->has_data) s->data[s->data_len++] = '\n';
        memcpy(s->data + s->data_len, val, vlen);
        s->data_len += vlen;
        s->has_data = true;
    }
    // id:, retry: and unknown fields are ignored
}

void sse_feed(sse_t *s, const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n' && s->last_cr) {  // second half of CRLF
            s->last_cr = false;
            continue;
        }
        s->last_cr = (c == '\r');
        if (c == '\r' || c == '\n') {
            process_line(s);
            s->line_len = 0;
            continue;
        }
        if (s->line_len + 1 >= s->line_cap && !grow(&s->line, &s->line_cap, s->line_len + 2, s->max)) {
            s->overflow = true;  // line too long: keep reading but drop this event
            continue;
        }
        s->line[s->line_len++] = c;
    }
}
