#include "picotts_esp.h"

#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "picotts";

static void *s_mem;
static TickType_t s_last_pause;

// A long sentence keeps the engine busy for seconds. Pausing for a tick every
// 100 ms lets lower-priority work on the same core run, including the idle task
// the task watchdog checks on; it costs about 1% of the engine's speed.
static void pause_now_and_then(void)
{
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_pause >= pdMS_TO_TICKS(100)) {
        vTaskDelay(1);
        s_last_pause = xTaskGetTickCount();
    }
}

bool picotts_esp_start(const void *ta, const void *sg)
{
    if (s_mem) return true;
    if (!ta || !sg) return false;
    s_mem = heap_caps_malloc(PICOTTS_MEM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_mem) {
        ESP_LOGE(TAG, "not enough PSRAM for the voice (%u bytes free)",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        return false;
    }
    if (!picotts_direct_init(ta, sg, s_mem, PICOTTS_MEM_SIZE)) {
        heap_caps_free(s_mem);
        s_mem = NULL;
        return false;
    }
    picotts_direct_set_yield(pause_now_and_then);
    return true;
}

void picotts_esp_stop(void)
{
    if (!s_mem) return;
    picotts_direct_shutdown();
    heap_caps_free(s_mem);
    s_mem = NULL;
}
