// Minimal ESP-IDF declarations so the firmware sources can be syntax-checked with
// the host compiler (test/idf_stub/check.sh). Signatures mirror ESP-IDF 5.3-5.5;
// nothing here is ever linked or run.
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ---- esp_err.h
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#define ESP_ERROR_CHECK(x) ((void)(x))

// ---- esp_log.h
#define ESP_LOGE(tag, fmt, ...) printf("E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("I %s: " fmt "\n", tag, ##__VA_ARGS__)

// ---- esp_timer.h
int64_t esp_timer_get_time(void);

// ---- esp_heap_caps.h
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void heap_caps_free(void *ptr);
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
#define MALLOC_CAP_INTERNAL (1 << 11)

// ---- esp_partition.h
typedef enum { ESP_PARTITION_TYPE_APP = 0x00, ESP_PARTITION_TYPE_DATA = 0x01, ESP_PARTITION_TYPE_ANY = 0xff } esp_partition_type_t;
typedef enum { ESP_PARTITION_SUBTYPE_ANY = 0xff } esp_partition_subtype_t;
typedef enum { ESP_PARTITION_MMAP_DATA, ESP_PARTITION_MMAP_INST } esp_partition_mmap_memory_t;
typedef uint32_t esp_partition_mmap_handle_t;
typedef struct {
    void *flash_chip;
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    char label[17];
    bool encrypted;
    bool readonly;
} esp_partition_t;
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char *label);
esp_err_t esp_partition_mmap(const esp_partition_t *partition, size_t offset, size_t size,
                             esp_partition_mmap_memory_t memory, const void **out_ptr,
                             esp_partition_mmap_handle_t *out_handle);
void esp_partition_munmap(esp_partition_mmap_handle_t handle);

// ---- esp_idf_version.h
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5, 4, 0)

// ---- FreeRTOS
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t EventBits_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *EventGroupHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define BIT0 0x00000001
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                       TaskHandle_t *handle);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *const name, const uint32_t stack, void *const arg,
                                   UBaseType_t prio, TaskHandle_t *const handle, const BaseType_t core);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, BaseType_t clear, BaseType_t all,
                                TickType_t wait);

// ---- driver/gpio.h
typedef int gpio_num_t;
typedef enum { GPIO_MODE_DISABLE, GPIO_MODE_INPUT, GPIO_MODE_OUTPUT } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE, GPIO_PULLDOWN_ENABLE } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE } gpio_int_type_t;
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_hold_en(gpio_num_t pin);
esp_err_t gpio_hold_dis(gpio_num_t pin);

// ---- driver/spi_master.h
typedef enum { SPI1_HOST, SPI2_HOST, SPI3_HOST } spi_host_device_t;
typedef enum { SPI_DMA_DISABLED = 0, SPI_DMA_CH_AUTO = 3 } spi_common_dma_t;
#define SPI_TRANS_USE_TXDATA (1 << 3)
typedef struct {
    int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num;
    int max_transfer_sz;
    uint32_t flags;
} spi_bus_config_t;
typedef struct {
    uint8_t mode;
    int clock_speed_hz;
    int spics_io_num;
    uint32_t flags;
    int queue_size;
} spi_device_interface_config_t;
typedef struct {
    uint32_t flags;
    uint16_t cmd;
    uint64_t addr;
    size_t length;
    size_t rxlength;
    void *user;
    union {
        const void *tx_buffer;
        uint8_t tx_data[4];
    };
    union {
        void *rx_buffer;
        uint8_t rx_data[4];
    };
} spi_transaction_t;
typedef struct spi_device_t *spi_device_handle_t;
esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *cfg, spi_common_dma_t dma);
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *cfg,
                             spi_device_handle_t *handle);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *t);

// ---- driver/usb_serial_jtag.h (+ vfs)
typedef struct {
    uint32_t tx_buffer_size;
    uint32_t rx_buffer_size;
} usb_serial_jtag_driver_config_t;
#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() {.tx_buffer_size = 256, .rx_buffer_size = 256}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *cfg);
int usb_serial_jtag_read_bytes(void *buf, uint32_t length, TickType_t wait);
void usb_serial_jtag_vfs_use_driver(void);
void esp_vfs_usb_serial_jtag_use_driver(void);

// ---- esp_adc
typedef enum { ADC_UNIT_1, ADC_UNIT_2 } adc_unit_t;
typedef enum { ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3 } adc_channel_t;
typedef enum { ADC_ATTEN_DB_0, ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_6, ADC_ATTEN_DB_12 } adc_atten_t;
typedef enum { ADC_BITWIDTH_DEFAULT = 0, ADC_BITWIDTH_12 = 12 } adc_bitwidth_t;
typedef struct adc_oneshot_unit_ctx_t *adc_oneshot_unit_handle_t;
typedef struct adc_cali_scheme_t *adc_cali_handle_t;
typedef struct {
    adc_unit_t unit_id;
    int clk_src;
    int ulp_mode;
} adc_oneshot_unit_init_cfg_t;
typedef struct {
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_oneshot_chan_cfg_t;
typedef struct {
    adc_unit_t unit_id;
    adc_channel_t chan;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_cali_curve_fitting_config_t;
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg, adc_oneshot_unit_handle_t *ret);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t h, adc_channel_t ch, const adc_oneshot_chan_cfg_t *cfg);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t h, adc_channel_t ch, int *out_raw);
esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *cfg, adc_cali_handle_t *ret);
esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t h, int raw, int *voltage);

// ---- esp_event.h / esp_netif.h / esp_wifi.h
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);
extern esp_event_base_t const WIFI_EVENT;
extern esp_event_base_t const IP_EVENT;
#define ESP_EVENT_ANY_ID -1
enum { WIFI_EVENT_STA_START = 2, WIFI_EVENT_STA_DISCONNECTED = 5 };
enum { IP_EVENT_STA_GOT_IP = 0 };
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t h, void *arg);
typedef struct esp_netif_obj esp_netif_t;
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
typedef struct {
    int dummy;
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() {0}
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 } wifi_auth_mode_t;
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA } wifi_mode_t;
typedef enum { WIFI_IF_STA } wifi_interface_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM } wifi_ps_type_t;
typedef struct {
    wifi_auth_mode_t authmode;
} wifi_scan_threshold_t;
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    wifi_scan_threshold_t threshold;
} wifi_sta_config_t;
typedef union {
    wifi_sta_config_t sta;
} wifi_config_t;
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_set_config(wifi_interface_t iface, wifi_config_t *cfg);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);

// ---- nvs_flash.h
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

// ---- esp_http_client.h / esp_crt_bundle.h
typedef enum { HTTP_METHOD_GET, HTTP_METHOD_POST } esp_http_client_method_t;
typedef struct esp_http_client *esp_http_client_handle_t;
typedef struct {
    const char *url;
    esp_http_client_method_t method;
    int timeout_ms;
    int buffer_size;
    int buffer_size_tx;
    esp_err_t (*crt_bundle_attach)(void *conf);
} esp_http_client_config_t;
esp_err_t esp_crt_bundle_attach(void *conf);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len);
int esp_http_client_write(esp_http_client_handle_t c, const char *buf, int len);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
esp_err_t esp_http_client_close(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);

// ---- esp_err.h / esp_system.h extras
const char *esp_err_to_name(esp_err_t code);
void esp_restart(void) __attribute__((__noreturn__));

// ---- nvs.h
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);

// ---- driver/sdmmc_host.h, sdmmc_cmd.h, esp_vfs_fat.h
#define GPIO_NUM_NC (-1)
#define BIT(n) (1UL << (n))
typedef struct {
    uint32_t flags;
    int slot;
    int max_freq_khz;
} sdmmc_host_t;
#define SDMMC_HOST_DEFAULT() {.flags = 0, .slot = 1, .max_freq_khz = 20000}
typedef struct {
    gpio_num_t clk, cmd, d0, d1, d2, d3, d4, d5, d6, d7;
    union {
        gpio_num_t gpio_cd;
        gpio_num_t cd;
    };
    union {
        gpio_num_t gpio_wp;
        gpio_num_t wp;
    };
    uint8_t width;
    uint32_t flags;
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP BIT(0)
} sdmmc_slot_config_t;
#define SDMMC_SLOT_CONFIG_DEFAULT() {.clk = 14, .cmd = 15, .d0 = 2, .cd = -1, .wp = -1, .width = 0, .flags = 0}
typedef struct sdmmc_card_s {
    int dummy;
} sdmmc_card_t;
typedef struct {
    bool format_if_mount_failed;
    int max_files;
    size_t allocation_unit_size;
    bool disk_status_check_enable;
    bool use_one_fat;
} esp_vfs_fat_mount_config_t;
typedef esp_vfs_fat_mount_config_t esp_vfs_fat_sdmmc_mount_config_t;
esp_err_t esp_vfs_fat_sdmmc_mount(const char *base_path, const sdmmc_host_t *host_config, const void *slot_config,
                                  const esp_vfs_fat_mount_config_t *mount_config, sdmmc_card_t **out_card);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *base_path, sdmmc_card_t *card);

// ---- esp_random.h
uint32_t esp_random(void);
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_FINISHED 0x10C

// ---- driver/i2c_master.h (copied from ESP-IDF 5.5.6)
typedef int i2c_port_num_t;
typedef enum { I2C_CLK_SRC_DEFAULT = 4 } i2c_clock_source_t;
typedef struct {
    i2c_port_num_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    union {
        i2c_clock_source_t clk_source;
    };
    uint8_t glitch_ignore_cnt;
    int intr_priority;
    size_t trans_queue_depth;
    struct {
        uint32_t enable_internal_pullup : 1;
        uint32_t allow_pd : 1;
    } flags;
} i2c_master_bus_config_t;
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config, i2c_master_bus_handle_t *ret_bus_handle);

// ---- driver/i2s_std.h (ESP32-S3 variant, copied from ESP-IDF 5.5.6)
typedef enum { I2S_NUM_0 = 0, I2S_NUM_1 = 1, I2S_NUM_AUTO } i2s_port_t;
typedef enum { I2S_ROLE_MASTER, I2S_ROLE_SLAVE } i2s_role_t;
typedef enum { I2S_DATA_BIT_WIDTH_8BIT = 8, I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_DATA_BIT_WIDTH_32BIT = 32 } i2s_data_bit_width_t;
typedef enum { I2S_SLOT_BIT_WIDTH_AUTO = 0 } i2s_slot_bit_width_t;
typedef enum { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 } i2s_slot_mode_t;
typedef enum { I2S_STD_SLOT_LEFT = BIT(0), I2S_STD_SLOT_RIGHT = BIT(1), I2S_STD_SLOT_BOTH = BIT(0) | BIT(1) } i2s_std_slot_mask_t;
typedef enum { I2S_MCLK_MULTIPLE_128 = 128, I2S_MCLK_MULTIPLE_256 = 256 } i2s_mclk_multiple_t;
typedef enum { I2S_CLK_SRC_DEFAULT = 160 } i2s_clock_src_t;
typedef struct i2s_channel_obj_t *i2s_chan_handle_t;
typedef struct {
    i2s_port_t id;
    i2s_role_t role;
    uint32_t dma_desc_num;
    uint32_t dma_frame_num;
    union {
        bool auto_clear;
        bool auto_clear_after_cb;
    };
    bool auto_clear_before_cb;
    bool allow_pd;
    int intr_priority;
} i2s_chan_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(i2s_num, i2s_role) {.id = i2s_num, .role = i2s_role, .dma_desc_num = 6, \
    .dma_frame_num = 240, .auto_clear_after_cb = false, .auto_clear_before_cb = false, .allow_pd = false, \
    .intr_priority = 0}
typedef struct {
    i2s_data_bit_width_t data_bit_width;
    i2s_slot_bit_width_t slot_bit_width;
    i2s_slot_mode_t slot_mode;
    i2s_std_slot_mask_t slot_mask;
    uint32_t ws_width;
    bool ws_pol;
    bool bit_shift;
    bool left_align;
    bool big_endian;
    bool bit_order_lsb;
} i2s_std_slot_config_t;
typedef struct {
    uint32_t sample_rate_hz;
    i2s_clock_src_t clk_src;
    uint32_t ext_clk_freq_hz;
    i2s_mclk_multiple_t mclk_multiple;
    uint32_t bclk_div;
} i2s_std_clk_config_t;
typedef struct {
    gpio_num_t mclk;
    gpio_num_t bclk;
    gpio_num_t ws;
    gpio_num_t dout;
    gpio_num_t din;
    struct {
        uint32_t mclk_inv : 1;
        uint32_t bclk_inv : 1;
        uint32_t ws_inv : 1;
    } invert_flags;
} i2s_std_gpio_config_t;
typedef struct {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits_per_sample, mono_or_stereo) {.data_bit_width = bits_per_sample, \
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, .slot_mode = mono_or_stereo, .slot_mask = I2S_STD_SLOT_BOTH, \
    .ws_width = bits_per_sample, .ws_pol = false, .bit_shift = true, .left_align = true, .big_endian = false, \
    .bit_order_lsb = false}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) {.sample_rate_hz = rate, .clk_src = I2S_CLK_SRC_DEFAULT, \
    .ext_clk_freq_hz = 0, .mclk_multiple = I2S_MCLK_MULTIPLE_256, .bclk_div = 8}
esp_err_t i2s_new_channel(const i2s_chan_config_t *chan_cfg, i2s_chan_handle_t *ret_tx_handle,
                          i2s_chan_handle_t *ret_rx_handle);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t handle, const i2s_std_config_t *std_cfg);
esp_err_t i2s_channel_enable(i2s_chan_handle_t handle);
esp_err_t i2s_channel_disable(i2s_chan_handle_t handle);
