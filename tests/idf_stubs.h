/* Host-only SPI/GPIO spies. No hardware access. */
#ifndef IDF_STUBS_H
#define IDF_STUBS_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG 1
#define CONFIG_READER_MAX_SPI_HZ 500000
#define DMA_ATTR
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
#define SPI_CLK_SRC_APB 0
#define GPIO_NUM_0 0
#define GPIO_MODE_INPUT 1
#define GPIO_FLOATING 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define ESP_OK 0
#define ESP_LOG_NONE 0
#define ESP_LINE_ENDINGS_LF 0
#define portMAX_DELAY 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define ESP_LOGI(tag, ...) ((void)(tag))
typedef int esp_err_t;
typedef void *spi_device_handle_t;
typedef struct {
    int miso_io_num, mosi_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz;
} spi_bus_config_t;
typedef struct {
    int clock_source, clock_speed_hz, mode, spics_io_num, queue_size, flags, input_delay_ns;
} spi_device_interface_config_t;
typedef struct {
    unsigned flags, length, rxlength;
    const void *tx_buffer;
    void *rx_buffer;
} spi_transaction_t;
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
typedef struct { int tx_buffer_size, rx_buffer_size; } usb_serial_jtag_driver_config_t;
static unsigned mock_initializations, mock_transfers, mock_stops, mock_idle;
static unsigned mock_gpio_calls, mock_delays;
static bool mock_fail_init;
static jmp_buf mock_task_done;
static int gpio_get_level(int pin) {
    assert(pin == 0);
    ++mock_gpio_calls;
    return mock_gpio_calls == 4; /* initially held, release once, then hold again */
}
static int gpio_config(const gpio_config_t *config) {
    if (config->pin_bit_mask != 1) {
        assert(config->mode == GPIO_MODE_INPUT);
        assert(!config->pull_up_en && !config->pull_down_en);
        ++mock_idle;
    }
    return ESP_OK;
}
static void vTaskDelay(int ticks) { (void)ticks; ++mock_delays; }
static void vTaskDelete(void *task) { (void)task; longjmp(mock_task_done, 1); }
static int xTaskCreate(void (*task)(void *), const char *name, int stack,
                       void *arg, int priority, void *handle) {
    (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    return pdPASS;
}
static int spi_bus_initialize(int host, const spi_bus_config_t *config, int dma) {
    assert(host == 2 && dma == 0);
    assert(config->miso_io_num == 13 && config->mosi_io_num == 11 && config->sclk_io_num == 12);
    ++mock_initializations;
    return mock_fail_init ? -1 : ESP_OK;
}
static int spi_bus_add_device(int host, const spi_device_interface_config_t *config,
                              spi_device_handle_t *handle) {
    assert(host == 2 && config->spics_io_num == 10 && config->clock_speed_hz <= 500000);
    *handle = (void *)(uintptr_t)1;
    return ESP_OK;
}
static int spi_device_get_actual_freq(spi_device_handle_t handle, int *khz) {
    assert(handle); *khz = 500; return ESP_OK;
}
static int spi_bus_remove_device(spi_device_handle_t handle) {
    assert(handle); ++mock_stops; return ESP_OK;
}
static int spi_bus_free(int host) { assert(host == 2); return ESP_OK; }
static int spi_device_acquire_bus(spi_device_handle_t handle, int wait) {
    assert(handle); (void)wait; return ESP_OK;
}
static void spi_device_release_bus(spi_device_handle_t handle) { assert(handle); }
static int spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *tx) {
    assert(handle && tx->length == tx->rxlength);
    ++mock_transfers;
    memset(tx->rx_buffer, 0xa5, tx->rxlength / 8);
    return ESP_OK;
}
static void esp_log_level_set(const char *tag, int level) { (void)tag; (void)level; }
static int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *config) {
    (void)config; return ESP_OK;
}
static void usb_serial_jtag_vfs_set_rx_line_endings(int type) { (void)type; }
static void usb_serial_jtag_vfs_set_tx_line_endings(int type) { (void)type; }
static void usb_serial_jtag_vfs_use_driver(void) {}
#endif
