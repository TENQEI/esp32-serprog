#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_vfs_dev.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "serprog.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////// Please update the following configuration according to your HardWare spec /////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "driver/gpio.h"
#include <stdatomic.h>
#include "readonly_policy.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || defined(CONFIG_ESP32_SERPROG_WIFI_ENABLED)
#error This reader requires ESP32-S3, native USB Serial/JTAG, and Wi-Fi disabled.
#endif
#define EEPROM_HOST SPI2_HOST
#define PIN_NUM_CS   10
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_MISO 13
#define ARM_PIN GPIO_NUM_0

/* The BOOT button must first be released, then held for >=2 s after boot.
 * Only this task grants permission; USB commands cannot arm the reader. */
static atomic_bool reader_armed;
static void arm_task(void *arg)
{
    (void)arg;
    while (!gpio_get_level(ARM_PIN)) vTaskDelay(pdMS_TO_TICKS(20));
    unsigned held = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        held = gpio_get_level(ARM_PIN) ? 0 : held + 1;
        if (held >= 100) {
            atomic_store(&reader_armed, true);
            vTaskDelete(NULL);
        }
    }
}

#ifdef CONFIG_ESP32_SERPROG_WIFI_ENABLED
    #include "wifi.h"

    #define USE_WIFI

    #define WIFI_SSID     CONFIG_ESP32_SERPROG_WIFI_SSID
    #define WIFI_PASSWORD CONFIG_ESP32_SERPROG_WIFI_PASSWORD
    #define WIFI_HOSTNAME CONFIG_ESP32_SERPROG_WIFI_HOSTNAME
    #define WIFI_TCP_PORT CONFIG_ESP32_SERPROG_WIFI_TCP_PORT

#elif defined CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    #include "driver/usb_serial_jtag.h"
    #include "driver/usb_serial_jtag_vfs.h"

    #define USE_SERIAL_JTAG

#elif defined CONFIG_ESP_CONSOLE_UART
    #include "driver/uart.h"
    #include "driver/uart_vfs.h"

    #define USE_HW_UART

    #define HW_UART_NUM CONFIG_ESP_CONSOLE_UART_NUM

    //use already allocated (default) pins
    #define HW_UART_PIN_TX  UART_PIN_NO_CHANGE
    #define HW_UART_PIN_RX  UART_PIN_NO_CHANGE
    #define HW_UART_PIN_CTS UART_PIN_NO_CHANGE
    #define HW_UART_PIN_RTS UART_PIN_NO_CHANGE

    #define HW_UART_BAUD_RATE 921600
    #define HW_UART_FLOW_CONTROL UART_HW_FLOWCTRL_DISABLE

    #define HW_UART_SERBUF_SIZE 4096

#elif defined CONFIG_ESP_CONSOLE_USB_CDC
    #define USE_USB_CDC

#else
    #error no serial option selected

#endif

static const char TAG[] = "serprog";

#define BUS_SPI         (1 << 3)
#define S_SUPPORTED_BUS   BUS_SPI
#define S_CMD_MAP ( \
  (1 << S_CMD_NOP)       | \
  (1 << S_CMD_Q_IFACE)   | \
  (1 << S_CMD_Q_CMDMAP)  | \
  (1 << S_CMD_Q_PGMNAME) | \
  (1 << S_CMD_Q_SERBUF)  | \
  (1 << S_CMD_Q_BUSTYPE) | \
  (1 << S_CMD_SYNCNOP)   | \
  (1 << S_CMD_O_SPIOP)   | \
  (1 << S_CMD_S_BUSTYPE) | \
  (1 << S_CMD_S_SPI_FREQ) | \
  (1 << S_CMD_Q_WRNMAXLEN) | \
  (1 << S_CMD_S_PIN_STATE) | \
  (1 << S_CMD_Q_RDNMAXLEN) \
)

#ifdef USE_WIFI
    #define serprog_getchar()                         wifi_getchar()
    #define serprog_putchar(_character)               wifi_putchar(_character)
    #define serprog_read(_dst, _elementSize, _count)  wifi_read(_dst, _elementSize, _count)
    #define serprog_write(_src, _elementSize, _count) wifi_write(_src, _elementSize, _count)
    #define serprog_flush()                           wifi_flush()
#else
    #define serprog_getchar()                         getchar()
    #define serprog_putchar(_character)               putchar(_character)
    #define serprog_read(_dst, _elementSize, _count)  fread(_dst, _elementSize, _count, stdin)
    #define serprog_write(_src, _elementSize, _count) fwrite(_src, _elementSize, _count, stdout)
    #define serprog_flush()                           fflush(stdout)
#endif

static inline uint32_t serprog_getu24(void) 
{
    uint32_t c1 = serprog_getchar();
    uint32_t c2 = serprog_getchar();
    uint32_t c3 = serprog_getchar();
    return c1 | (c2<<8) | (c3<<16);
}

static inline uint32_t serprog_getu32(void) 
{
    uint32_t c1 = serprog_getchar();
    uint32_t c2 = serprog_getchar();
    uint32_t c3 = serprog_getchar();
    uint32_t c4 = serprog_getchar();
    return c1 | (c2<<8) | (c3<<16) | (c4<<24);
}

static inline void serprog_putu32(uint32_t d) 
{
    char buf[4];
    memcpy(buf, &d, 4);
    serprog_putchar(buf[0]);
    serprog_putchar(buf[1]);
    serprog_putchar(buf[2]);
    serprog_putchar(buf[3]);
}

#define SPI_BUF_SIZE 16384

DMA_ATTR unsigned char write_buffer[SPI_BUF_SIZE];
DMA_ATTR unsigned char read_buffer[SPI_BUF_SIZE];

uint8_t syncnop[] = { S_NAK, S_ACK };

static spi_device_handle_t spi_device;
static bool bus_initialized;
static uint32_t requested_freq = 500000;

static bool output_enabled;
static void pins_idle(void)
{
    /* gpio_reset_pin() enables pull-ups on this target; do not use it here. */
    gpio_config_t idle = {
        .pin_bit_mask = (1ULL << PIN_NUM_CS) | (1ULL << PIN_NUM_MOSI) |
                        (1ULL << PIN_NUM_CLK) | (1ULL << PIN_NUM_MISO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&idle));
}
static void spi_stop(void)
{
    if (spi_device) {
        ESP_ERROR_CHECK(spi_bus_remove_device(spi_device));
        spi_device = NULL;
    }
    if (bus_initialized) {
        ESP_ERROR_CHECK(spi_bus_free(EEPROM_HOST));
        bus_initialized = false;
    }
    pins_idle();
}

static uint32_t serprog_spi_init(uint32_t freq) 
{
    esp_err_t ret;

    if (!atomic_load(&reader_armed)) return 0;
    spi_stop();
    freq = reader_clamp_frequency(freq, CONFIG_READER_MAX_SPI_HZ);

    //initialize SPI host
    spi_bus_config_t buscfg = 
    {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_BUF_SIZE,
    };
    
    ret = spi_bus_initialize(EEPROM_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        goto cleanup;
    }

    bus_initialized = true;
    spi_device_interface_config_t devcfg = 
    {
        .clock_source = SPI_CLK_SRC_APB,
        .clock_speed_hz = freq,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 2,
        .flags = 0,
        .input_delay_ns = 0
    };

    //Attach the EEPROM to the SPI bus
    ret = spi_bus_add_device(EEPROM_HOST, &devcfg, &spi_device);
    if (ret != ESP_OK)
    {
        goto cleanup;
    }

    int freq_khz;

    ret = spi_device_get_actual_freq(spi_device, &freq_khz);
    if (ret != ESP_OK)
    {
        goto cleanup;
    }

    if ((uint32_t)freq_khz * 1000 > freq) goto cleanup;
    return freq_khz * 1000;

    //release spi if error
cleanup:
    spi_stop();
    return 0;
}

static void process(int command) 
{
    switch(command) 
    {
        case S_CMD_NOP:
            serprog_putchar(S_ACK);
            break;
        case S_CMD_Q_IFACE:
            serprog_putchar(S_ACK);
            serprog_putchar(0x01);
            serprog_putchar(0x00);
            break;
        case S_CMD_Q_CMDMAP:
            serprog_putchar(S_ACK);
            serprog_putu32(S_CMD_MAP);

            for(int i = 0; i < 32 - sizeof(uint32_t); ++i) 
            {
                serprog_putchar(0);
            }
            break;
        case S_CMD_Q_PGMNAME:
            serprog_putchar(S_ACK);
            serprog_write("s3-nor-readonly\0", 1, 16);
            break;
        case S_CMD_Q_SERBUF:
            serprog_putchar(S_ACK);

#if defined HW_UART_SERBUF_SIZE && (HW_UART_FLOW_CONTROL == UART_HW_FLOWCTRL_DISABLE)
            serprog_putchar(HW_UART_SERBUF_SIZE & 0xFF);
            serprog_putchar((HW_UART_SERBUF_SIZE >> 8) & 0xFF);
#else
            //serprog protocol: 
            // If the programmer has a guaranteed working flow control,
		    // it should return a big bogus value - eg 0xFFFF
            serprog_putchar(0xFF);
            serprog_putchar(0xFF);
#endif

            break;
        case S_CMD_Q_RDNMAXLEN:
        case S_CMD_Q_WRNMAXLEN:
            serprog_putchar(S_ACK);
            serprog_putchar((SPI_BUF_SIZE / 2) & 0xFF);
            serprog_putchar(((SPI_BUF_SIZE / 2) >> 8) & 0xFF);
            serprog_putchar(((SPI_BUF_SIZE / 2) >> 16) & 0xFF);
            break;
        case S_CMD_Q_BUSTYPE:
            serprog_putchar(S_ACK);
            serprog_putchar(S_SUPPORTED_BUS);
            break;
        case S_CMD_SYNCNOP:
            serprog_write(syncnop, 1, 2);
            break;
        case S_CMD_S_BUSTYPE:
            {
                int bustype = serprog_getchar();
                if((bustype | S_SUPPORTED_BUS) == S_SUPPORTED_BUS) 
                {
                    serprog_putchar(S_ACK);
                } else 
                {
                    serprog_putchar(S_NAK);
                }
            }
            break;
        case S_CMD_O_SPIOP:
            {
                uint32_t wlen = serprog_getu24();
                uint32_t rlen = serprog_getu24();

                /* Drain every declared TX byte, even a rejected oversized frame.
                 * Otherwise payload bytes could become new serprog commands. */
                bool fits = wlen <= SPI_BUF_SIZE / 2 && rlen <= SPI_BUF_SIZE / 2;
                for (uint32_t i = 0; i < wlen; ++i) {
                    int byte = serprog_getchar();
                    if (byte == EOF) { spi_stop(); return; }
                    if (fits) write_buffer[i] = (uint8_t)byte;
                }
                if (!fits || !output_enabled || !atomic_load(&reader_armed) ||
                    !reader_read_allowed(write_buffer, wlen, rlen)) {
                    serprog_putchar(S_NAK);
                    break;
                }
                if (!spi_device && !serprog_spi_init(requested_freq)) {
                    serprog_putchar(S_NAK);
                    break;
                }

                memset(write_buffer + wlen, 0, SPI_BUF_SIZE - wlen);

                esp_err_t err;

                err = spi_device_acquire_bus(spi_device, portMAX_DELAY);
                if (err != ESP_OK) 
                {
                    serprog_putchar(S_NAK);
                    break;
                }

                spi_transaction_t trans = 
                {
                    .flags = 0,
                    .length = (wlen + rlen) * 8,
                    .tx_buffer = write_buffer,
                    .rxlength = (wlen + rlen) * 8,
                    .rx_buffer = read_buffer
                };

                err = spi_device_polling_transmit(spi_device, &trans);
                if (err != ESP_OK)
                {
                    spi_device_release_bus(spi_device);
                    serprog_putchar(S_NAK);
                    break;
                }

                spi_device_release_bus(spi_device);

                serprog_putchar(S_ACK);

                if (rlen > 0) 
                {
                    serprog_write(read_buffer + wlen, 1, rlen);
                }
            }
            break;
        case S_CMD_S_PIN_STATE: {
            int state = serprog_getchar();
            if (state == 0) {
                output_enabled = false;
                spi_stop();
                serprog_putchar(S_ACK);
            } else if (state == 1 && atomic_load(&reader_armed)) {
                output_enabled = true;
                serprog_putchar(S_ACK);
            } else {
                serprog_putchar(S_NAK);
            }
            break;
        }
        case S_CMD_S_SPI_FREQ:
            {
                uint32_t freq = serprog_getu32();
                if (reader_clamp_frequency(freq, CONFIG_READER_MAX_SPI_HZ) > 0) 
                {
                    requested_freq = reader_clamp_frequency(freq, CONFIG_READER_MAX_SPI_HZ);
                    /* Frequency negotiation alone never enables SPI outputs. */
                    if (spi_device) {
                        uint32_t actual = serprog_spi_init(requested_freq);
                        if (!actual) { serprog_putchar(S_NAK); break; }
                        serprog_putchar(S_ACK);
                        serprog_putu32(actual);
                    } else {
                        serprog_putchar(S_ACK);
                        serprog_putu32(requested_freq);
                    }
                } 
                else 
                {
                    serprog_putchar(S_NAK);
                }
            }
            break;
        default:
            serprog_putchar(S_NAK);
    }

    serprog_flush();
}

void app_main(void)
{
    esp_err_t ret;

#ifdef USE_WIFI
    ESP_LOGI(TAG, "initializing serprog-wifi");
    ret = wifi_init(WIFI_SSID, WIFI_PASSWORD, WIFI_HOSTNAME, WIFI_TCP_PORT);
    ESP_ERROR_CHECK(ret);

#else
    ESP_LOGI(TAG, "disabling built-in logging for serial");
    esp_log_level_set("*", ESP_LOG_NONE);
    fflush(stdout);

    //configure UART for blocking mode

    //disable input buffering
    setvbuf(stdin, NULL, _IONBF, 0);

    #ifdef USE_SERIAL_JTAG
    //configure builtin serial/jtag
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = (usb_serial_jtag_driver_config_t){4096, 4096};

    ret = usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    ESP_ERROR_CHECK(ret);

    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    
    usb_serial_jtag_vfs_use_driver();

    #elif defined USE_HW_UART
    //configure HW uart
    uart_config_t uart_config = 
    {
        .baud_rate = HW_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = HW_UART_FLOW_CONTROL
    };

    ret = uart_param_config(HW_UART_NUM, &uart_config);
    ESP_ERROR_CHECK(ret);

    ret = uart_set_pin(HW_UART_NUM, HW_UART_PIN_TX, HW_UART_PIN_RX, HW_UART_PIN_RTS, HW_UART_PIN_CTS);
    ESP_ERROR_CHECK(ret);

    ret = uart_driver_install(HW_UART_NUM, HW_UART_SERBUF_SIZE, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    ESP_ERROR_CHECK(ret);

    uart_vfs_dev_port_set_rx_line_endings (HW_UART_NUM, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_port_set_tx_line_endings (HW_UART_NUM, ESP_LINE_ENDINGS_LF);

    uart_vfs_dev_use_driver(HW_UART_NUM);

    #elif USE_USB_CDC
        #error not implemented
    #endif
#endif

    pins_idle();
    gpio_config_t arm_config = {
        .pin_bit_mask = 1ULL << ARM_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&arm_config));
    if (xTaskCreate(arm_task, "physical_arm", 2048, NULL, 2, NULL) != pdPASS)
        abort();

    for (;;) 
    {
        int command = serprog_getchar();

        if (command == EOF) { spi_stop(); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        process(command);
    }
}
