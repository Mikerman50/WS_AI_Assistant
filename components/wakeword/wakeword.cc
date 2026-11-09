#include "wakeword.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "frontend.h"
#include "frontend_util.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

// The front end settings every microWakeWord model was trained with (the same
// as ESPHome's micro_wake_word preprocessor_settings.h).
#define FEATURES 40
#define SAMPLE_RATE 16000
#define VAR_ARENA_BYTES 2048
#define MAX_WINDOW 16
#define COOL_OFF_SLICES 100  // 1 s of 10 ms slices before a detection can count

struct ww {
    ww_alloc_fn alloc;
    ww_free_fn free_fn;
    FrontendState fs;
    bool fs_ok;
    uint8_t *model;
    uint8_t *arena;
    size_t arena_size;
    uint8_t *var_arena;
    tflite::MicroMutableOpResolver<20> *ops;
    tflite::MicroInterpreter *interp;
    alignas(16) uint8_t ops_mem[sizeof(tflite::MicroMutableOpResolver<20>)];
    alignas(16) uint8_t interp_mem[sizeof(tflite::MicroInterpreter)];
    int stride, step;
    uint8_t probs[MAX_WINDOW];
    int window, last;
    int ignore;
    uint8_t cutoff;
    uint8_t peak;
};

static bool add_ops(tflite::MicroMutableOpResolver<20> *r)
{
    return r->AddCallOnce() == kTfLiteOk && r->AddVarHandle() == kTfLiteOk && r->AddReshape() == kTfLiteOk &&
           r->AddReadVariable() == kTfLiteOk && r->AddStridedSlice() == kTfLiteOk &&
           r->AddConcatenation() == kTfLiteOk && r->AddAssignVariable() == kTfLiteOk && r->AddConv2D() == kTfLiteOk &&
           r->AddMul() == kTfLiteOk && r->AddAdd() == kTfLiteOk && r->AddMean() == kTfLiteOk &&
           r->AddFullyConnected() == kTfLiteOk && r->AddLogistic() == kTfLiteOk && r->AddQuantize() == kTfLiteOk &&
           r->AddDepthwiseConv2D() == kTfLiteOk && r->AddAveragePool2D() == kTfLiteOk &&
           r->AddMaxPool2D() == kTfLiteOk && r->AddPad() == kTfLiteOk && r->AddPack() == kTfLiteOk &&
           r->AddSplitV() == kTfLiteOk;
}

// The interpreter, with its streaming state variables kept in their own small
// arena. False if `arena_size` is too small.
static bool make_interpreter(ww_t *w, size_t arena_size)
{
    tflite::MicroAllocator *ma = tflite::MicroAllocator::Create(w->var_arena, VAR_ARENA_BYTES);
    tflite::MicroResourceVariables *mrv = ma ? tflite::MicroResourceVariables::Create(ma, 20) : nullptr;
    if (!mrv) return false;
    // TensorFlow Lite Micro's classes can't be deleted (it builds without a
    // heap), so they're made in place in our own memory and taken down by hand.
    w->interp = new (w->interp_mem)
        tflite::MicroInterpreter(tflite::GetModel(w->model), *w->ops, w->arena, arena_size, mrv);
    if (w->interp->AllocateTensors() != kTfLiteOk) {
        w->interp->~MicroInterpreter();
        w->interp = nullptr;
        return false;
    }
    return true;
}

static void fail(char *err, size_t errsz, const char *msg)
{
    if (err && errsz) snprintf(err, errsz, "%s", msg);
}

ww_t *ww_create(const uint8_t *model, size_t model_len, const ww_params_t *p, ww_alloc_fn alloc, ww_free_fn free_fn,
                char *err, size_t errsz)
{
    fail(err, errsz, "");
    if (!model || model_len < 16 || !p || p->window < 1 || p->window > MAX_WINDOW) {
        fail(err, errsz, "bad wake word settings");
        return nullptr;
    }
    ww_t *w = static_cast<ww_t *>(alloc(sizeof(ww_t), false));  // 16-byte aligned, as the members want
    if (!w) {
        fail(err, errsz, "out of memory");
        return nullptr;
    }
    memset(static_cast<void *>(w), 0, sizeof *w);
    w->alloc = alloc;
    w->free_fn = free_fn;
    w->window = p->window;
    float c = p->cutoff < 0.0f ? 0.0f : p->cutoff > 1.0f ? 1.0f : p->cutoff;
    w->cutoff = static_cast<uint8_t>(c * 255.0f + 0.5f);

    FrontendConfig fc;
    FrontendFillConfigWithDefaults(&fc);
    fc.window.size_ms = 30;
    fc.window.step_size_ms = 10;
    fc.filterbank.num_channels = FEATURES;
    fc.filterbank.lower_band_limit = 125.0f;
    fc.filterbank.upper_band_limit = 7500.0f;
    fc.noise_reduction.smoothing_bits = 10;
    fc.noise_reduction.even_smoothing = 0.025f;
    fc.noise_reduction.odd_smoothing = 0.06f;
    fc.noise_reduction.min_signal_remaining = 0.05f;
    fc.pcan_gain_control.enable_pcan = 1;
    fc.pcan_gain_control.strength = 0.95f;
    fc.pcan_gain_control.offset = 80.0f;
    fc.pcan_gain_control.gain_bits = 21;
    fc.log_scale.enable_log = 1;
    fc.log_scale.scale_shift = 6;
    w->fs_ok = FrontendPopulateState(&fc, &w->fs, SAMPLE_RATE) != 0;

    // A copy, 16-byte aligned as the flatbuffer reader wants.
    w->model = static_cast<uint8_t *>(alloc(model_len, false));
    w->var_arena = static_cast<uint8_t *>(alloc(VAR_ARENA_BYTES, true));
    w->ops = new (w->ops_mem) tflite::MicroMutableOpResolver<20>();
    if (!w->fs_ok || !w->model || !w->var_arena || !add_ops(w->ops)) {
        fail(err, errsz, "out of memory");
        ww_destroy(w);
        return nullptr;
    }
    memcpy(w->model, model, model_len);
    if (tflite::GetModel(w->model)->version() != TFLITE_SCHEMA_VERSION) {
        fail(err, errsz, "wake word model has the wrong format");
        ww_destroy(w);
        return nullptr;
    }

    // The size the model's manifest gives, then more: the optimised kernels on
    // the chip can need more scratch memory than the ones it was measured with.
    const size_t base = p->arena_bytes ? p->arena_bytes : 24 * 1024;
    const size_t tries[] = {(base + 15) & ~(size_t)15, (base * 3 / 2 + 15) & ~(size_t)15,
                            (base * 2 + 15) & ~(size_t)15};
    for (size_t t : tries) {
        w->arena = static_cast<uint8_t *>(alloc(t, true));
        if (!w->arena) continue;
        w->arena_size = t;
        if (make_interpreter(w, t)) break;
        free_fn(w->arena);
        w->arena = nullptr;
        w->arena_size = 0;
    }
    if (!w->interp) {
        fail(err, errsz, "wake word model didn't fit in memory");
        ww_destroy(w);
        return nullptr;
    }

    TfLiteTensor *in = w->interp->input(0);
    TfLiteTensor *out = w->interp->output(0);
    if (in->dims->size != 3 || in->dims->data[0] != 1 || in->dims->data[2] != FEATURES || in->type != kTfLiteInt8 ||
        out->dims->size != 2 || out->dims->data[0] != 1 || out->dims->data[1] != 1 || out->type != kTfLiteUInt8 ||
        in->dims->data[1] < 1) {
        fail(err, errsz, "not a streaming wake word model");
        ww_destroy(w);
        return nullptr;
    }
    w->stride = in->dims->data[1];
    ww_reset(w);
    return w;
}

void ww_reset(ww_t *w)
{
    memset(w->probs, 0, sizeof w->probs);
    w->ignore = -COOL_OFF_SLICES;
    if (w->fs_ok) FrontendReset(&w->fs);
}

// One 10 ms slice of features through the model. True on a detection.
static bool slice(ww_t *w, const int8_t *features)
{
    TfLiteTensor *in = w->interp->input(0);
    memcpy(tflite::GetTensorData<int8_t>(in) + FEATURES * w->step, features, FEATURES);
    if (++w->step >= w->stride) {
        w->step = 0;
        if (w->interp->Invoke() != kTfLiteOk) return false;
        uint8_t prob = w->interp->output(0)->data.uint8[0];
        if (prob > w->peak) w->peak = prob;
        w->last = (w->last + 1) % w->window;
        w->probs[w->last] = prob;
    }
    // Cool off after a detection (or a reset) until the score has dropped.
    if (w->probs[w->last] < w->cutoff && w->ignore < 0) w->ignore++;
    if (w->ignore < 0) return false;

    unsigned sum = 0;
    for (int i = 0; i < w->window; i++) sum += w->probs[i];
    if (sum <= (unsigned)w->cutoff * (unsigned)w->window) return false;
    memset(w->probs, 0, sizeof w->probs);
    w->ignore = -COOL_OFF_SLICES;
    return true;
}

bool ww_feed(ww_t *w, const int16_t *pcm, size_t n)
{
    bool heard = false;
    while (n > 0) {
        size_t used = 0;
        FrontendOutput o = FrontendProcessSamples(&w->fs, pcm, n, &used);
        if (used == 0) break;
        pcm += used;
        n -= used;
        if (o.size != FEATURES) continue;
        int8_t f[FEATURES];
        for (int i = 0; i < FEATURES; i++) {
            // Matches the int8 output of TensorFlow's audio front end op, which
            // the models were trained on: the 16-bit log energies (about 0 to
            // 670) scaled by 256 / 666 and shifted to -128..127.
            int32_t v = ((int32_t)o.values[i] * 256 + 333) / 666 - 128;
            f[i] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
        }
        if (slice(w, f)) heard = true;
    }
    return heard;
}

float ww_take_peak(ww_t *w)
{
    float p = w->peak / 255.0f;
    w->peak = 0;
    return p;
}

size_t ww_arena_bytes(const ww_t *w)
{
    return w->interp ? w->interp->arena_used_bytes() : 0;
}

void ww_destroy(ww_t *w)
{
    if (!w) return;
    if (w->interp) w->interp->~MicroInterpreter();
    if (w->ops) w->ops->~MicroMutableOpResolver();
    if (w->fs_ok) FrontendFreeStateContents(&w->fs);
    if (w->arena) w->free_fn(w->arena);
    if (w->var_arena) w->free_fn(w->var_arena);
    if (w->model) w->free_fn(w->model);
    w->free_fn(w);
}
