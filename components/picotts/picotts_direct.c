// Synchronous Pico TTS wrapper (see include/picotts_direct.h). Based on the
// engine set-up in DiUS's esp_picotts.c (Apache-2.0) and the speak loop of
// SVOX's pico2wave.
#include "picotts_direct.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "picoapi.h"
#include "picoapid.h"
// after picoapi.h, which defines the types it uses
#include "esp_picorsrc.h"

static pico_System s_system;
static pico_Resource s_ta, s_sg;
static pico_Engine s_engine;
static const pico_Char s_voice[] = "PicoVoice";

// Pico's own fast exp() (Schraudolph's trick: write a scaled integer into the
// top half of a double) picks that half with an endianness check in
// picopltf.h that can misfire on other toolchains, which turns the voice into
// noise, and it is only accurate to about 4% (enough to wobble the pitch). The
// build renames it (see CMakeLists.txt) and uses this instead. Single precision
// is plenty for audio and runs on the ESP32-S3's FPU; a double-precision exp()
// would be done in software, about 32,000 times per second of speech.
picoos_double picoos_quick_exp(const picoos_double y)
{
    return (picoos_double)expf((float)y);
}

static void (*s_yield)(void);

void picotts_direct_set_yield(void (*fn)(void))
{
    s_yield = fn;
}

static void report(const char *what, int code)
{
    pico_Retstring msg;
    msg[0] = '\0';
    if (s_system) pico_getSystemStatusMessage(s_system, code, msg);
    fprintf(stderr, "picotts: %s (%d) %s\n", what, code, (const char *)msg);
}

void picotts_direct_shutdown(void)
{
    if (s_engine) {
        pico_disposeEngine(s_system, &s_engine);
        pico_releaseVoiceDefinition(s_system, s_voice);
        s_engine = NULL;
    }
    if (s_sg) esp_pico_unloadResource(s_system, &s_sg);
    if (s_ta) esp_pico_unloadResource(s_system, &s_ta);
    s_sg = s_ta = NULL;
    if (s_system) pico_terminate(&s_system);
    s_system = NULL;
}

bool picotts_direct_init(const void *ta, const void *sg, void *mem, size_t mem_size)
{
    pico_Retstring name;
    int ret;
#define CHECK(what)              \
    if (ret != PICO_OK) {        \
        report(what, ret);       \
        picotts_direct_shutdown(); \
        return false;            \
    }
    if (s_system) return true;
    ret = pico_initialize(mem, (pico_Uint32)mem_size, &s_system);
    CHECK("initialise");
    ret = esp_pico_loadResource(s_system, ta, &s_ta);
    CHECK("load text analysis resource");
    ret = esp_pico_loadResource(s_system, sg, &s_sg);
    CHECK("load signal generation resource");
    ret = pico_createVoiceDefinition(s_system, s_voice);
    CHECK("create voice");
    ret = pico_getResourceName(s_system, s_ta, name);
    CHECK("name text analysis resource");
    ret = pico_addResourceToVoiceDefinition(s_system, s_voice, (const pico_Char *)name);
    CHECK("add text analysis resource");
    ret = pico_getResourceName(s_system, s_sg, name);
    CHECK("name signal generation resource");
    ret = pico_addResourceToVoiceDefinition(s_system, s_voice, (const pico_Char *)name);
    CHECK("add signal generation resource");
    ret = pico_newEngine(s_system, s_voice, &s_engine);
    CHECK("create engine");
#undef CHECK
    return true;
}

int picotts_direct_say(const char *text, picotts_sink_fn sink, void *ctx)
{
    if (!s_engine) return PICO_ERR_NULLPTR_ACCESS;
    // The terminating NUL is part of the input: it tells Pico the text is complete.
    const pico_Char *in = (const pico_Char *)text;
    size_t left = strlen(text) + 1;
    int16_t buf[256];
    bool stopped = false;

    while (left > 0 && !stopped) {
        pico_Int16 chunk = left > 2048 ? 2048 : (pico_Int16)left;
        pico_Int16 sent = 0;
        int ret = pico_putTextUtf8(s_engine, in, chunk, &sent);
        if (ret != PICO_OK) {
            report("text input", ret);
            pico_resetEngine(s_engine, PICO_RESET_SOFT);
            return ret;
        }
        in += sent;
        left -= (size_t)sent;

        int status;
        do {
            pico_Int16 bytes = 0, type = 0;
            if (s_yield) s_yield();
            status = pico_getData(s_engine, buf, (pico_Int16)sizeof buf, &bytes, &type);
            if (bytes > 0 && !sink(buf, (size_t)bytes / 2, ctx)) {
                stopped = true;
                break;
            }
        } while (status == PICO_STEP_BUSY);
        if (!stopped && status != PICO_STEP_IDLE) {
            report("synthesis", status);
            pico_resetEngine(s_engine, PICO_RESET_FULL);
            return status < 0 ? status : -1;
        }
    }
    if (stopped) {
        pico_resetEngine(s_engine, PICO_RESET_SOFT);  // drop what's left of this text
        return 1;
    }
    return 0;
}
