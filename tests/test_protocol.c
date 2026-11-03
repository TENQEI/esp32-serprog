/* Compile the real production dispatch against GPIO/SPI spies. */
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
static uint8_t input[20000], output[20000];
static size_t input_size, input_pos, output_size;
static int test_getchar(void) { return input_pos < input_size ? input[input_pos++] : EOF; }
static int test_putchar(int c) { assert(output_size < sizeof(output)); output[output_size++] = c; return c; }
static size_t test_write(const void *data, size_t size, size_t count, FILE *stream) {
    (void)stream;
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size * count; ++i) test_putchar(bytes[i]);
    return count;
}
static int test_flush(FILE *stream) { (void)stream; return 0; }
#define getchar test_getchar
#define putchar test_putchar
#define fwrite test_write
#define fflush test_flush
#include "../src/serprog/main/main.c"
#undef getchar
#undef putchar
#undef fwrite
#undef fflush

static void feed(const uint8_t *bytes, size_t size) {
    memcpy(input, bytes, size); input_size = size; input_pos = output_size = 0;
}
static void spi_frame(const uint8_t *tx, unsigned w, unsigned r) {
    uint8_t frame[20000] = {w & 255, (w >> 8) & 255, w >> 16,
                            r & 255, (r >> 8) & 255, r >> 16};
    memcpy(frame + 6, tx, w);
    feed(frame, w + 6);
    process(S_CMD_O_SPIOP);
}
int main(void) {
    const uint8_t read_cmd[] = {3, 0, 0, 0};
    const uint8_t enable[] = {1}, disable[] = {0};
    const uint8_t high_freq[] = {0, 18, 122, 0}; /* 8 MHz request */
    feed(high_freq, 4); process(S_CMD_S_SPI_FREQ);
    assert(!mock_initializations && requested_freq == 500000);
    feed(enable, 1); process(S_CMD_S_PIN_STATE);
    assert(output_size == 1 && output[0] == S_NAK && !output_enabled);
    spi_frame(read_cmd, 4, 1);
    assert(!mock_initializations && output[0] == S_NAK);
    if (!setjmp(mock_task_done)) arm_task(NULL);
    assert(atomic_load(&reader_armed) && mock_gpio_calls == 104 && mock_delays == 103);
    spi_frame(read_cmd, 4, 1); /* arm alone still does not enable outputs */
    assert(!mock_initializations && output[0] == S_NAK);
    feed(enable, 1); process(S_CMD_S_PIN_STATE);
    assert(output[0] == S_ACK && !mock_initializations);

    for (unsigned op = 0; op < 256; ++op) {
        if (op == 3 || op == 0xb || op == 0x5a || op == 0x9f ||
            op == 5 || op == 0x35 || op == 0x15) continue;
        uint8_t cmd[] = {op, 0, 0, 0};
        spi_frame(cmd, 4, 1);
        assert(output[0] == S_NAK && !mock_initializations && !mock_transfers);
    }
    uint8_t oversized[8193];
    memset(oversized, S_CMD_NOP, sizeof(oversized));
    spi_frame(oversized, sizeof(oversized), 1);
    assert(output[0] == S_NAK && input_pos == input_size && !mock_transfers);
    uint8_t truncated[] = {4, 0, 0, 1, 0, 0, 3};
    feed(truncated, sizeof(truncated)); process(S_CMD_O_SPIOP);
    assert(!mock_transfers && !mock_initializations);
    spi_frame(read_cmd, 4, 8192);
    assert(mock_initializations == 1 && mock_transfers == 1);
    assert(output_size == 8193 && output[0] == S_ACK && output[8192] == 0xa5);
    uint8_t bad_read[] = {3, 255, 255, 255};
    spi_frame(bad_read, 4, 2);
    assert(output[0] == S_NAK && mock_transfers == 1);
    feed(disable, 1); process(S_CMD_S_PIN_STATE);
    assert(!output_enabled && !spi_device && !bus_initialized && mock_stops == 1);
    spi_frame(read_cmd, 4, 1);
    assert(output[0] == S_NAK && mock_transfers == 1);
    feed(enable, 1); process(S_CMD_S_PIN_STATE);
    mock_fail_init = true;
    spi_frame(read_cmd, 4, 1);
    assert(output[0] == S_NAK && !spi_device && !bus_initialized && mock_transfers == 1);
    puts("PASS: real dispatch lock, physical arm, allowlist, frame drain, EOF, output disable and SPI failure");
}
