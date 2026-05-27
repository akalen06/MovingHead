#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/regs/uart.h"
#include "hardware/structs/uart.h"

#define UART_ID             uart1
#define UART_HW             uart1_hw
#define BAUD_RATE           250000
#define UART_TX_PIN         4
#define UART_RX_PIN         5
#define CHANNELS_TO_READ    9
#define FRAME_TIMEOUT_US    5000    // ruim genoeg binnen een DMX frame, maar kort genoeg om te resyncen

static uint8_t stable_channels[CHANNELS_TO_READ] = {0};

typedef enum {
    DMX_WAIT_BREAK = 0,
    DMX_WAIT_STARTCODE,
    DMX_READ_CHANNELS
} dmx_state_t;

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("\r\n=== Stabiele DMX Receiver (BREAK-based) ===\r\n");
    printf("Wacht op BREAK + Start Code 0x00 + %d kanalen\r\n", CHANNELS_TO_READ);

    uart_init(UART_ID, BAUD_RATE);
    uart_set_format(UART_ID, 8, 2, UART_PARITY_NONE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_fifo_enabled(UART_ID, false);   // makkelijker en stabieler voor byte-per-byte DMX parsing

    dmx_state_t state = DMX_WAIT_BREAK;
    absolute_time_t last_byte_time = get_absolute_time();

    uint8_t start_code = 0;
    uint8_t raw_channels[CHANNELS_TO_READ] = {0};
    int ch_idx = 0;
    uint32_t frame_count = 0;

    while (true) {
        if (uart_is_readable(UART_ID)) {
            last_byte_time = get_absolute_time();

            // Lees status vóór/na byte om framing error/BREAK te detecteren
            uint32_t rsr = UART_HW->rsr;
            uint8_t byte = uart_getc(UART_ID);

            bool framing_error = (rsr & UART_UARTRSR_FE_BITS) != 0;

            if (framing_error) {
                // Clear alle RX error flags
                UART_HW->rsr = 0;

                // In DMX betekent framing error in de praktijk: BREAK gedetecteerd
                state = DMX_WAIT_STARTCODE;
                ch_idx = 0;
                continue;
            }

            switch (state) {
                case DMX_WAIT_BREAK:
                    // Alles negeren tot we een BREAK zien
                    break;

                case DMX_WAIT_STARTCODE:
                    start_code = byte;

                    if (start_code == 0x00) {
                        state = DMX_READ_CHANNELS;
                        ch_idx = 0;
                    } else {
                        // Geen standaard DMX data frame, opnieuw wachten op BREAK
                        state = DMX_WAIT_BREAK;
                    }
                    break;

                case DMX_READ_CHANNELS:
                    if (ch_idx < CHANNELS_TO_READ) {
                        raw_channels[ch_idx++] = byte;
                    }

                    if (ch_idx >= CHANNELS_TO_READ) {
                        for (int i = 0; i < CHANNELS_TO_READ; i++) {
                            // Zachte filtering tegen jitter
                            stable_channels[i] = (stable_channels[i] * 3 + raw_channels[i]) / 4;
                        }

                        frame_count++;
                        printf("F%lu  CH1=%3u CH2=%3u CH3=%3u CH4=%3u CH5=%3u "
                               "CH6=%3u CH7=%3u CH8=%3u CH9=%3u\r\n",
                               frame_count,
                               stable_channels[0], stable_channels[1], stable_channels[2],
                               stable_channels[3], stable_channels[4], stable_channels[5],
                               stable_channels[6], stable_channels[7], stable_channels[8]);

                        state = DMX_WAIT_BREAK;
                        ch_idx = 0;
                    }
                    break;
            }
        }

        // midden in een frame zitten en er komt te lang niks meer binnen: resync
        if (state != DMX_WAIT_BREAK &&
            absolute_time_diff_us(last_byte_time, get_absolute_time()) > FRAME_TIMEOUT_US) {
            state = DMX_WAIT_BREAK;
            ch_idx = 0;
        }

        tight_loop_contents();
    }
}