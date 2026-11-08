// Host unit tests for the portable core: cc builds them, no ESP-IDF needed.
//   make -C test/host test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "captions.h"
#include "config_parse.h"
#include "face.h"
#include "gfx.h"
#include "sse.h"
#include "text_stream.h"
#include "utf8.h"

static int g_fail, g_pass;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) g_pass++;                                                  \
        else {                                                               \
            g_fail++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_STR(a, b)                                                                     \
    do {                                                                                    \
        const char *_a = (a), *_b = (b);                                                    \
        if (strcmp(_a, _b) == 0) g_pass++;                                                  \
        else {                                                                              \
            g_fail++;                                                                       \
            fprintf(stderr, "FAIL %s:%d:\n  got      \"%s\"\n  expected \"%s\"\n", __FILE__, \
                    __LINE__, _a, _b);                                                      \
        }                                                                                   \
    } while (0)

// ---------------------------------------------------------------- SSE

typedef struct {
    int n;
    char event[8][48];
    char data[8][512];
} sse_log_t;

static void on_sse(const char *event, const char *data, void *ctx)
{
    sse_log_t *log = ctx;
    if (log->n >= 8) return;
    snprintf(log->event[log->n], sizeof log->event[0], "%s", event);
    snprintf(log->data[log->n], sizeof log->data[0], "%s", data);
    log->n++;
}

static void feed_chunked(sse_t *s, const char *text, size_t chunk)
{
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i += chunk) sse_feed(s, text + i, (len - i) < chunk ? (len - i) : chunk);
}

static void test_sse(void)
{
    const char *stream =
        "event: message_start\r\n"
        "data: {\"type\":\"message_start\"}\r\n"
        "\r\n"
        ": keep-alive comment\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Hi\"}}\n"
        "\n"
        "event: ping\n"
        "data: {\"type\": \"ping\"}\n"
        "\n"
        "data: line one\n"
        "data: line two\n"
        "\n";
    for (size_t chunk = 1; chunk <= 7; chunk += 3) {
        sse_log_t log = {0};
        sse_t s;
        CHECK(sse_init(&s, 4096, on_sse, &log));
        feed_chunked(&s, stream, chunk);
        sse_free(&s);
        CHECK(log.n == 4);
        CHECK_STR(log.event[0], "message_start");
        CHECK_STR(log.data[0], "{\"type\":\"message_start\"}");
        CHECK_STR(log.event[1], "content_block_delta");
        CHECK(strstr(log.data[1], "\"text\":\"Hi\"") != NULL);
        CHECK_STR(log.event[2], "ping");
        CHECK_STR(log.event[3], "message");
        CHECK_STR(log.data[3], "line one\nline two");
    }

    // An event bigger than the limit is dropped, later ones still arrive.
    sse_log_t log = {0};
    sse_t s;
    CHECK(sse_init(&s, 64, on_sse, &log));
    sse_feed(&s, "event: big\ndata: ", 17);
    for (int i = 0; i < 200; i++) sse_feed(&s, "x", 1);
    const char *rest = "\n\nevent: small\ndata: ok\n\n";
    sse_feed(&s, rest, strlen(rest));
    sse_free(&s);
    CHECK(log.n == 1);
    CHECK_STR(log.event[0], "small");
    CHECK_STR(log.data[0], "ok");
}

// ---------------------------------------------------------------- text stream

typedef struct {
    int n;
    char s[32][256];
    char cue[32][24];
} ts_log_t;

static void on_sentence(const char *sentence, const char *cue, void *ctx)
{
    ts_log_t *log = ctx;
    if (log->n >= 32) return;
    snprintf(log->s[log->n], sizeof log->s[0], "%s", sentence);
    snprintf(log->cue[log->n], sizeof log->cue[0], "%s", cue);
    log->n++;
}

static ts_log_t run_ts(const char *text, size_t chunk)
{
    ts_log_t log;
    memset(&log, 0, sizeof log);
    text_stream_t t;
    ts_init(&t, on_sentence, &log);
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i += chunk) ts_feed(&t, text + i, (len - i) < chunk ? (len - i) : chunk);
    ts_finish(&t);
    return log;
}

static void test_text_stream(void)
{
    for (size_t chunk = 1; chunk <= 9; chunk += 4) {
        ts_log_t a = run_ts("[amused] Ha, that's a good one! Penguins can't fly, but they swim brilliantly.", chunk);
        CHECK(a.n == 2);
        CHECK_STR(a.s[0], "Ha, that's a good one!");
        CHECK_STR(a.cue[0], "amused");
        CHECK_STR(a.s[1], "Penguins can't fly, but they swim brilliantly.");
        CHECK_STR(a.cue[1], "");

        ts_log_t b = run_ts("Ask Dr. Smith about it. He knows the 3.5 version well.", chunk);
        CHECK(b.n == 2);
        CHECK_STR(b.s[0], "Ask Dr. Smith about it.");
        CHECK_STR(b.s[1], "He knows the 3.5 version well.");

        ts_log_t c = run_ts("**Sure** thing \xF0\x9F\x8E\x89! Here is what I found.", chunk);
        CHECK(c.n == 1);
        CHECK_STR(c.s[0], "Sure thing! Here is what I found.");  // first part too short to stand alone

        ts_log_t d = run_ts("Well, [surprised] I didn't expect that. [happy]Great news though!", chunk);
        CHECK(d.n == 2);
        CHECK_STR(d.s[0], "Well, I didn't expect that.");
        CHECK_STR(d.cue[0], "surprised");
        CHECK_STR(d.s[1], "Great news though!");
        CHECK_STR(d.cue[1], "happy");

        ts_log_t e = run_ts("Use the [1] option, it is better.", chunk);
        CHECK(e.n == 1);
        CHECK_STR(e.s[0], "Use the [1] option, it is better.");

        ts_log_t f = run_ts("Caf\xC3\xA9 au lait costs \xE2\x82\xAC" "3 \xE2\x80\x94 not bad. Shall I find one?", chunk);
        CHECK(f.n == 2);
        CHECK_STR(f.s[0], "Caf\xC3\xA9 au lait costs \xE2\x82\xAC" "3 \xE2\x80\x94 not bad.");
        CHECK_STR(f.s[1], "Shall I find one?");

        ts_log_t g = run_ts("First line without a full stop\nSecond line.", chunk);
        CHECK(g.n == 2);
        CHECK_STR(g.s[0], "First line without a full stop");
    }

    // Long runs are cut at a comma so speech can start before Claude finishes.
    char longtext[600] = "";
    for (int i = 0; i < 12; i++) strcat(longtext, "this clause keeps going and going, ");
    strcat(longtext, "and finally ends.");
    ts_log_t h = run_ts(longtext, 5);
    CHECK(h.n >= 3);
    for (int i = 0; i < h.n; i++) CHECK(strlen(h.s[i]) <= 181);
    CHECK(h.s[0][strlen(h.s[0]) - 1] == ',');
}

// ---------------------------------------------------------------- captions

static int measure_caption(const char *s, int nbytes, void *ctx)
{
    (void)ctx;
    return gfx_text_width_n(&font_caption, s, nbytes);
}

static void test_captions(void)
{
    const char *text = "Probably, yes. There's rain from about two in the afternoon, so take a coat and maybe an umbrella too.";
    cap_layout_t L;
    int pages = captions_layout(&L, text, FACE_CAPTION_MAX_W, 3, measure_caption, NULL);
    CHECK(pages >= 1);
    CHECK(L.ncp == utf8_count(text));
    int words_seen = 0;
    for (int p = 0; p < L.npages; p++) {
        CHECK(L.page[p].nlines >= 1 && L.page[p].nlines <= 3);
        for (int i = 0; i < L.page[p].nlines; i++) {
            CHECK(gfx_text_width(&font_caption, L.page[p].line[i]) <= FACE_CAPTION_MAX_W);
            if (strstr(L.page[p].line[i], "umbrella")) words_seen++;
        }
        if (p > 0) CHECK(L.page[p].first_cp == L.page[p - 1].last_cp + 2);  // one space between pages
    }
    CHECK(words_seen == 1);
    CHECK(L.page[L.npages - 1].last_cp == L.ncp - 1);

    uint32_t starts[256];
    uint32_t total = captions_estimate(text, 15.0f, starts, 256);
    CHECK(total > 6000 && total < 10000);
    captions_apply_timing(&L, starts, NULL, L.ncp, total);
    CHECK(L.page[0].start_ms == 0);
    CHECK(captions_page_at(&L, 0) == 0);
    CHECK(captions_page_at(&L, total) == L.npages - 1);
    for (int p = 1; p < L.npages; p++) {
        CHECK(L.page[p].start_ms > L.page[p - 1].start_ms);
        CHECK(captions_page_at(&L, L.page[p].start_ms) == p);
        CHECK(captions_page_at(&L, L.page[p].start_ms - 1) == p - 1);
    }
    CHECK(captions_progress(&L.page[0], 0) == 0.0f);
    CHECK(captions_progress(&L.page[0], L.page[0].end_ms) == 1.0f);

    // A word wider than a line is split instead of overflowing.
    cap_layout_t W;
    captions_layout(&W, "Supercalifragilisticexpialidocious-and-then-some", FACE_CAPTION_MAX_W, 3, measure_caption, NULL);
    CHECK(W.npages == 1 && W.page[0].nlines >= 2);
    for (int i = 0; i < W.page[0].nlines; i++)
        CHECK(gfx_text_width(&font_caption, W.page[0].line[i]) <= FACE_CAPTION_MAX_W);

    cap_layout_t E;
    CHECK(captions_layout(&E, "   ", FACE_CAPTION_MAX_W, 3, measure_caption, NULL) == 0);
}

// ---------------------------------------------------------------- graphics

static void test_gfx(void)
{
    gfx_t g;
    gfx_clear(&g);
    gfx_fill_rect(&g, -5, -5, 10, 10, true);
    CHECK(gfx_get(&g, 0, 0) && gfx_get(&g, 4, 4) && !gfx_get(&g, 5, 5));
    gfx_fill_rect(&g, 10, 10, 0, 5, true);
    CHECK(!gfx_get(&g, 10, 10));

    char out[64];
    gfx_fit_text(&font_small, "short", 180, out, sizeof out);
    CHECK_STR(out, "short");
    gfx_fit_text(&font_small, "you: this question is far too long to fit across the status strip", 180, out, sizeof out);
    CHECK(gfx_text_width(&font_small, out) <= 180);
    CHECK(strstr(out, "\xE2\x80\xA6") != NULL);

    CHECK(face_expr_from_name("Amused") == EXPR_AMUSED);
    CHECK(face_expr_from_name("sad") == EXPR_SORRY);
    CHECK(face_expr_from_name("banana") == EXPR_COUNT);

    // Every mode renders without touching memory outside the canvas (ASan checks this).
    face_state_t s;
    face_state_init(&s);
    for (int m = MODE_IDLE; m <= MODE_NOTICE; m++) {
        for (int e = 0; e < EXPR_COUNT; e++) {
            s.mode = (face_mode_t)m;
            s.expr = (face_expr_t)e;
            strcpy(s.heard, "what time is it?");
            strcpy(s.pinned, "Bins: green, Thursday");
            strcpy(s.caption[0], "Hello there");
            s.caption_lines = 1;
            s.mouth = 1.0f;
            s.look = 1.0f;
            s.level = 1.0f;
            face_render(&g, &s);
        }
    }
}

// ---------------------------------------------------------------- config.json

static char *read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = calloc(1, 65536);
    size_t n = fread(buf, 1, 65535, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static bool parse(const char *json, app_config_t *c, char *err)
{
    return config_parse(json, c, err, 160);
}

static void test_config(void)
{
    app_config_t c;
    char err[160];

    // The sample shipped in sdcard/ parses, but asks for real values.
    char *sample = read_text_file("../../sdcard/config.json");
    CHECK(sample != NULL);
    if (sample) {
        CHECK(!parse(sample, &c, err));
        CHECK_STR(err, "Replace the example values in config.json.");
        CHECK(c.claude_max_tokens == 300 && c.display_busy_active_high && c.speech_pace_cps == 15);
        free(sample);
    }

    // Minimal file: everything else falls back to defaults.
    CHECK(parse("{\"wifi\":{\"ssid\":\"Home\",\"password\":\"correcthorse\"},"
                "\"claude\":{\"api_key\":\"sk-ant-abc\"}}", &c, err));
    CHECK_STR(c.wifi_ssid, "Home");
    CHECK_STR(c.wifi_password, "correcthorse");
    CHECK_STR(c.claude_api_key, "sk-ant-abc");
    CHECK_STR(c.claude_model, "claude-haiku-5-5");
    CHECK(c.claude_max_tokens == 300 && !c.claude_web_search);
    CHECK(c.display_rotation == 0 && c.display_busy_active_high && c.speech_pace_cps == 15);
    CHECK_STR(c.claude_context, "");

    // Every field set.
    const char *full =
        "{\n"
        "  \"wifi\": {\"ssid\": \"Caf\xC3\xA9 net\", \"password\": \"\"},\n"
        "  \"claude\": {\"api_key\": \"sk-ant-xyz\", \"model\": \"claude-sonnet-5-5\", \"max_tokens\": 500,\n"
        "             \"web_search\": true, \"context\": \"Our cat is called Biscuit.\"},\n"
        "  \"elevenlabs\": {\"api_key\": \"el-key\", \"voice_id\": \"v123\"},\n"
        "  \"display\": {\"rotation\": 2, \"busy_active_high\": false},\n"
        "  \"speech\": {\"pace_chars_per_sec\": 18},\n"
        "  \"something_new\": {\"ignored\": 1}\n"
        "}\n";
    CHECK(parse(full, &c, err));
    CHECK_STR(c.wifi_ssid, "Caf\xC3\xA9 net");
    CHECK_STR(c.wifi_password, "");
    CHECK_STR(c.claude_model, "claude-sonnet-5-5");
    CHECK(c.claude_max_tokens == 500 && c.claude_web_search);
    CHECK_STR(c.claude_context, "Our cat is called Biscuit.");
    CHECK_STR(c.elevenlabs_api_key, "el-key");
    CHECK_STR(c.elevenlabs_voice_id, "v123");
    CHECK(c.display_rotation == 2 && !c.display_busy_active_high && c.speech_pace_cps == 18);

    // Comments and a Windows byte-order mark are tolerated.
    CHECK(parse("\xEF\xBB\xBF{ // my settings\n \"wifi\": {\"ssid\": \"Home\"}, /* key */\n"
                " \"claude\": {\"api_key\": \"sk-ant-1\"} }", &c, err));
    CHECK_STR(c.wifi_ssid, "Home");

    // Problems produce short messages that fit on the screen.
    struct {
        const char *json, *msg;
    } bad[] = {
        {"", "config.json is empty."},
        {"[1, 2]", "config.json must start with { and end with }."},
        {"{\n\"wifi\": {\"ssid\": \"Home\"},\n\"claude\": {\"api_key\": \"k\",}\n}", "config.json has a mistake on line 3."},
        {"{\"wifi\": {\"ssid\": \"Home\"}}", "claude.api_key is missing from config.json."},
        {"{\"claude\": {\"api_key\": \"k\"}}", "wifi.ssid is missing from config.json."},
        {"{\"wifi\": \"Home\", \"claude\": {\"api_key\": \"k\"}}", "\"wifi\" must be a { } section in config.json."},
        {"{\"wifi\": {\"ssid\": 42}, \"claude\": {\"api_key\": \"k\"}}", "wifi.ssid must be text in quotes."},
        {"{\"wifi\": {\"ssid\": \"\"}, \"claude\": {\"api_key\": \"k\"}}", "wifi.ssid is empty in config.json."},
        {"{\"wifi\": {\"ssid\": \"0123456789012345678901234567890123\"}, \"claude\": {\"api_key\": \"k\"}}",
         "wifi.ssid is too long."},
        {"{\"wifi\": {\"ssid\": \"Home\", \"password\": \"short\"}, \"claude\": {\"api_key\": \"k\"}}",
         "wifi.password must be at least 8 characters."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"max_tokens\": \"300\"}}",
         "claude.max_tokens must be a number."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"max_tokens\": 2.5}}",
         "claude.max_tokens must be 16 to 8192."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\"}, \"display\": {\"rotation\": 5}}",
         "display.rotation must be 0 to 3."},
        {"{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"web_search\": \"yes\"}}",
         "claude.web_search must be true or false."},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!parse(bad[i].json, &c, err));
        CHECK_STR(err, bad[i].msg);
        CHECK(gfx_text_width(&font_caption, err) <= 3 * FACE_CAPTION_MAX_W);  // fits the notice lines
    }

    // An empty model falls back to the default; an over-long context is refused.
    CHECK(parse("{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"model\": \"\"}}", &c, err));
    CHECK_STR(c.claude_model, "claude-haiku-5-5");
    char *big = malloc(APP_CONTEXT_MAX + 200);
    strcpy(big, "{\"wifi\": {\"ssid\": \"Home\"}, \"claude\": {\"api_key\": \"k\", \"context\": \"");
    size_t at = strlen(big);
    memset(big + at, 'x', APP_CONTEXT_MAX + 1);
    strcpy(big + at + APP_CONTEXT_MAX + 1, "\"}}");
    CHECK(!parse(big, &c, err));
    CHECK_STR(err, "claude.context is too long.");
    free(big);
}

int main(void)
{
    test_sse();
    test_text_stream();
    test_captions();
    test_gfx();
    test_config();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
