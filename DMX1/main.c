#include "cybsp.h"
#include "cyhal.h"

#define DMX_UNIVERSE_SIZE   513
#define DMX_START_CODE      0x00
#define DMX_BAUDRATE        250000
#define DMX_REFRESH_MS      30

#define DMX_TX_PIN          P12_0
#define PICOSPOT_ADDRESS    1

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

static cy_rslt_t dmx_uart_init(void)
{
    cy_rslt_t result;
    const cyhal_uart_cfg_t cfg = {
        .data_bits      = 8,
        .stop_bits      = 2,
        .parity         = CYHAL_UART_PARITY_NONE,
        .rx_buffer      = NULL,
        .rx_buffer_size = 0
    };

    result = cyhal_uart_init(&uart_dmx, DMX_TX_PIN,
                             CYHAL_NC_PIN_VALUE, CYHAL_NC_PIN_VALUE,
                             CYHAL_NC_PIN_VALUE, NULL, &cfg);
    if (result != CY_RSLT_SUCCESS)
        return 0xDEAD0001;  // uart_init faalt

    result = cyhal_uart_set_baud(&uart_dmx, DMX_BAUDRATE, NULL);
    if (result != CY_RSLT_SUCCESS)
        return 0xDEAD0002;  // set_baud faalt

    return CY_RSLT_SUCCESS;
}

static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;

    // BREAK
    cyhal_uart_free(&uart_dmx);
    cyhal_gpio_init(DMX_TX_PIN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 0);
    cyhal_system_delay_us(200);

    // MAB
    cyhal_gpio_write(DMX_TX_PIN, 1);
    cyhal_system_delay_us(12);

    // Geef pin vrij zodat UART hem kan overnemen
    cyhal_gpio_free(DMX_TX_PIN);

    // UART opnieuw initialiseren + debug check
    cy_rslt_t result = dmx_uart_init();
    if (result != CY_RSLT_SUCCESS)
    {
        // LED knippert = uart_init faalt na BREAK
        cyhal_gpio_toggle(CYBSP_USER_LED);
        return;
    }

    cyhal_uart_write(&uart_dmx, dmx_buffer, &len);
}

int main(void)
{
    cybsp_init();
    __enable_irq();

    // Debug LED initialiseren
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);

    dmx_init_buffer();

    cy_rslt_t init_result = dmx_uart_init();
if (init_result == 0xDEAD0001)
{
    // 1x lang knipperen = uart_init faalt
    for(;;) { cyhal_gpio_toggle(CYBSP_USER_LED); cyhal_system_delay_ms(1000); }
}
else if (init_result == 0xDEAD0002)
{
    // 3x snel knipperen = set_baud faalt
    for(;;) { 
        for(int i=0; i<6; i++) { cyhal_gpio_toggle(CYBSP_USER_LED); cyhal_system_delay_ms(200); }
        cyhal_system_delay_ms(1000);
    }
}

    uint16_t addr = PICOSPOT_ADDRESS;

    dmx_set_channel(addr + 0, 128);  // Ch1: Pan
    dmx_set_channel(addr + 1, 128);  // Ch2: Tilt
    dmx_set_channel(addr + 2, 0);    // Ch3: Pan fine
    dmx_set_channel(addr + 3, 0);    // Ch4: Tilt fine
    dmx_set_channel(addr + 4, 0);    // Ch5: Pan/Tilt speed
    dmx_set_channel(addr + 5, 0);    // Ch6: Color wheel (wit)
    dmx_set_channel(addr + 6, 0);    // Ch7: Gobo wheel (open)
    dmx_set_channel(addr + 7, 255);  // Ch8: Dimmer (full)
    dmx_set_channel(addr + 8, 0);    // Ch9: Shutter (open)

    for (;;)
    {
        dmx_send_frame();
        cyhal_system_delay_ms(DMX_REFRESH_MS);
    }
}