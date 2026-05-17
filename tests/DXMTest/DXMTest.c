#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#define DMX_UART        uart0
#define DMX_TX_PIN      0
#define DMX_BAUD        250000

// DMX timing (in microseconds)
static uint32_t dmx_break_us      = 183;   // Break
static uint32_t dmx_mab_us        = 24;    // Mark After Break
static uint32_t dmx_idle_us       = 1000;  // extra idle inbetween frames

static uint32_t dmx_bit_us        = 4;     // 1/250000 s ≈ 4 µs / bit
static uint32_t dmx_code_us       = 36;    // 1 start + 8 data bits
static uint32_t dmx_stop_us       = 16;    // 2 stop bits @ ~8 µs

// Total DMX-Channels
#define DMX_CHANNELS   500

uint8_t dmx_buffer[DMX_CHANNELS + 1];   // [0] startcode, [1..N] Channels

static void dmx_uart_init(void) {
    // Init UART
    uart_init(DMX_UART, DMX_BAUD);
    // DMX512 is 8N2; Pico supports 1 or 2 stopbits via "stop_bits" argument
    uart_set_format(DMX_UART, 8, 2, UART_PARITY_NONE);
    // TX-pin on UART
    gpio_set_function(DMX_TX_PIN, GPIO_FUNC_UART);
}

// DMX channel is 1..DMX_CHANNELS
void dmx_set_channel(uint16_t channel, uint8_t value) {
    if (channel == 0 || channel > DMX_CHANNELS) return;
    dmx_buffer[channel] = value;
}

static void dmx_send_frame(void) {
    // UART of to generate break
    uart_deinit(DMX_UART);

    // Force TX as normal GPIO
    gpio_set_function(DMX_TX_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(DMX_TX_PIN, GPIO_OUT);

    // BREAK (Line low)
    gpio_put(DMX_TX_PIN, 0);
    sleep_us(dmx_break_us);

    // MARK AFTER BREAK (Line high)
    gpio_put(DMX_TX_PIN, 1);
    sleep_us(dmx_mab_us);

    // Activate UART high
    dmx_uart_init();

    // Send DMX data: Startcode + Channelvalue
    for (int i = 0; i < DMX_CHANNELS + 1; i++) {
        uart_putc_raw(DMX_UART, dmx_buffer[i]);
    }

    // Extra idle-time to determine universe‑frequency
    sleep_us(dmx_idle_us);
}

int main() {
    stdio_init_all();

    // Init DMX buffer
    dmx_buffer[0] = 0x00; // startcode
    for (int i = 1; i <= DMX_CHANNELS; i++) {
        dmx_buffer[i] = 0;
    }

    dmx_uart_init();

    while (true) {
        // Scene 1
        // dmx_set_channel(channelNr, someValue);
        dmx_set_channel(1, 128);   // pan
        dmx_set_channel(2, 128);   // tilt
        dmx_set_channel(3, 0);   // Fine pan
        dmx_set_channel(4, 0);   // Fine tilt
        dmx_set_channel(5, 0);   // motor speed
        dmx_set_channel(6, 70);     // color
        dmx_set_channel(7, 0);   // Gobo
        dmx_set_channel(8, 250);   // master dimmer
        dmx_set_channel(9, 0);   // Strobe

        dmx_send_frame();

        sleep_ms(2000);

        // Scene 2
        // dmx_set_channel(channelNr, someValue);
        dmx_set_channel(1, 150);   // pan
        dmx_set_channel(2, 150);   // tilt
        dmx_set_channel(3, 0);   // Fine pan
        dmx_set_channel(4, 0);   // Fine tilt
        dmx_set_channel(5, 0);   // motor speed
        dmx_set_channel(6, 15);     // color
        dmx_set_channel(7, 0);   // Gobo
        dmx_set_channel(8, 250);   // master dimmer
        dmx_set_channel(9, 0);   // Strobe

        dmx_send_frame();

        sleep_ms(2000);        
    }
}

