#include "cybsp.h"
#include "cyhal.h"

#define DMX_UNIVERSE_SIZE 513  // 0 = startcode, 1..512 = channels
#define DMX_START_CODE     0x00
#define DMX_BAUDRATE       250000
#define DMX_REFRESH_MS     30

static cyhal_uart_t uart_dmx;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch < 1 || ch > 512) return;
    dmx_buffer[ch] = value;
}

static void dmx_init_buffer(void)
{
    dmx_buffer[0] = DMX_START_CODE;
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

// UART init volledig in code (geen Device Configurator nodig)
static cy_rslt_t dmx_uart_init(void)
{
    cy_rslt_t result;

    const cyhal_uart_cfg_t cfg =
    {
        .data_bits      = 8,
        .stop_bits      = 2,
        .parity         = CYHAL_UART_PARITY_NONE,
        .rx_buffer      = NULL,
        .rx_buffer_size = 0
    };

    // TX = P5_1, RX/CTS/RTS onbenut → CYHAL_NC_PIN_VALUE
    result = cyhal_uart_init(&uart_dmx,
                             P5_1,
                             CYHAL_NC_PIN_VALUE,
                             CYHAL_NC_PIN_VALUE,
                             CYHAL_NC_PIN_VALUE,
                             NULL,
                             &cfg);
    if (result != CY_RSLT_SUCCESS)
        return result;

    result = cyhal_uart_set_baud(&uart_dmx, DMX_BAUDRATE, NULL);
    if (result != CY_RSLT_SUCCESS)
        return result;

    return CY_RSLT_SUCCESS;
}


// BREAK + MAB + data
static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;

    cyhal_uart_free(&uart_dmx);
    cyhal_gpio_init(P5_1, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 0);
    cyhal_system_delay_us(120);  // BREAK
    cyhal_gpio_write(P5_1, 1);
    cyhal_system_delay_us(12);   // MAB

    dmx_uart_init();

    cyhal_uart_write(&uart_dmx, dmx_buffer, &len);
}


int main(void)
{
    cybsp_init();
    __enable_irq();

    dmx_init_buffer();

    if (dmx_uart_init() != CY_RSLT_SUCCESS)
    {
        for(;;); // error
    }

    // Test: PicoSpot op adres 1, kanaal 8 (dimmer) full on
    dmx_set_channel(8, 255);

    for (;;)
    {
        dmx_send_frame();
        cyhal_system_delay_ms(DMX_REFRESH_MS);
    }
}
