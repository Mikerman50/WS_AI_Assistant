#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "epd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gfx.h"

static const char *TAG = "ui";

#define FULL_REFRESH_EVERY 120  // partial refreshes before a cleaning full refresh (each reply also ends with one)
#define PANEL_OFF_AFTER_MS 3000 // switch the charge pump off after this long without changes
#define EV_IDLE BIT0

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static TaskHandle_t s_task;
static face_state_t s_state;
static bool s_dirty, s_full;
static gfx_t s_canvas;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock_changed(void)
{
    s_dirty = true;
    xEventGroupClearBits(s_events, EV_IDLE);
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

static void copy_text(char *dst, const char *src)
{
    snprintf(dst, FACE_TEXT_MAX, "%s", src ? src : "");
}

void ui_set_mode(face_mode_t mode)
{
    lock();
    s_state.mode = mode;
    s_state.blink = false;  // idle antics stop whenever the mode changes
    s_state.prop[0] = '\0';
    if (mode != MODE_SPEAKING) {
        s_state.mouth = 0.0f;
        s_state.look = 0.5f;
        s_state.caption_lines = 0;
    }
    if (mode != MODE_LISTENING) s_state.level = 0.0f;
    unlock_changed();
}

face_mode_t ui_get_mode(void)
{
    lock();
    face_mode_t m = s_state.mode;
    xSemaphoreGive(s_lock);
    return m;
}

void ui_set_idle_pose(bool blink, float look, face_expr_t expr, const char *prop)
{
    lock();
    if (s_state.mode != MODE_IDLE || expr >= EXPR_COUNT ||
        (s_state.blink == blink && s_state.look == look && s_state.expr == expr &&
         strcmp(s_state.prop, prop ? prop : "") == 0)) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_state.blink = blink;
    s_state.look = look;
    s_state.expr = expr;
    snprintf(s_state.prop, sizeof s_state.prop, "%s", prop ? prop : "");
    unlock_changed();
}

void ui_set_expr(face_expr_t expr)
{
    if (expr >= EXPR_COUNT) return;
    lock();
    if (s_state.expr == expr) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_state.expr = expr;
    unlock_changed();
}

void ui_set_heard(const char *text)
{
    lock();
    copy_text(s_state.heard, text);
    unlock_changed();
}

void ui_set_caption(const cap_page_t *page)
{
    lock();
    s_state.caption_lines = page->nlines < FACE_CAPTION_LINES ? page->nlines : FACE_CAPTION_LINES;
    for (int i = 0; i < s_state.caption_lines; i++) copy_text(s_state.caption[i], page->line[i]);
    unlock_changed();
}

void ui_clear_caption(void)
{
    lock();
    s_state.caption_lines = 0;
    unlock_changed();
}

void ui_set_speaking(float mouth, float look)
{
    // Quantise so tiny changes don't cost a refresh.
    float m = roundf(mouth * 3.0f) / 3.0f;
    float l = roundf(look * 4.0f) / 4.0f;
    lock();
    if (m == s_state.mouth && l == s_state.look) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_state.mouth = m;
    s_state.look = l;
    unlock_changed();
}

void ui_set_level(float level)
{
    float q = roundf(level * 5.0f) / 5.0f;
    lock();
    if (q == s_state.level) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_state.level = q;
    unlock_changed();
}

void ui_set_pinned(const char *text)
{
    lock();
    copy_text(s_state.pinned, text);
    unlock_changed();
}

void ui_full_refresh(void)
{
    lock();
    s_full = true;
    unlock_changed();
}

static int measure_caption(const char *s, int nbytes, void *ctx)
{
    (void)ctx;
    return gfx_text_width_n(&font_caption, s, nbytes);
}

void ui_show_notice(face_expr_t expr, const char *message, const char *footer)
{
    static cap_layout_t layout;  // only the first page is shown
    captions_layout(&layout, message, FACE_CAPTION_MAX_W, FACE_CAPTION_LINES, measure_caption, NULL);
    lock();
    s_state.mode = MODE_NOTICE;
    if (expr < EXPR_COUNT) s_state.expr = expr;
    s_state.mouth = 0.0f;
    s_state.look = 0.5f;
    s_state.caption_lines = 0;
    if (layout.npages > 0) {
        s_state.caption_lines = layout.page[0].nlines;
        for (int i = 0; i < s_state.caption_lines; i++) copy_text(s_state.caption[i], layout.page[0].line[i]);
    }
    copy_text(s_state.pinned, footer);
    s_full = true;
    unlock_changed();
}

bool ui_wait_idle(int timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, EV_IDLE, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & EV_IDLE) != 0;
}

static void ui_task(void *arg)
{
    (void)arg;
    int partials = 0;
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PANEL_OFF_AFTER_MS)) == 0) {
            epd_power_off();  // quiet for a while; the image stays without power
            continue;
        }
        face_state_t st;
        bool full;
        lock();
        st = s_state;
        full = s_full;
        s_full = false;
        s_dirty = false;
        xSemaphoreGive(s_lock);

        face_render(&s_canvas, &st);
        if (full || partials >= FULL_REFRESH_EVERY) {
            epd_show_full(&s_canvas);
            partials = 0;
        } else {
            epd_show_partial(&s_canvas);
            partials++;
        }

        lock();
        if (!s_dirty) xEventGroupSetBits(s_events, EV_IDLE);
        xSemaphoreGive(s_lock);
    }
}

void ui_init(int rotation, bool busy_active_high)
{
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    face_state_init(&s_state);
    epd_init(rotation, busy_active_high);
    xTaskCreate(ui_task, "ui", 6144, NULL, 4, &s_task);
    ESP_LOGI(TAG, "display ready");
    ui_full_refresh();  // first frame must be a full refresh
}
