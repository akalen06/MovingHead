#include "cy_syslib.h"
#include "cyhal.h"
#include "cybsp.h"
#include <stdint.h>
#include <stdbool.h>

/* DMX parameters */
#define DMX_UNIVERSE_SIZE   513
#define DMX_START_CODE      0x00
#define DMX_BAUDRATE        250000
#define DMX_REFRESH_MS      20      // iets sneller, makkelijker scope
#define DMX_BREAK_US        200
#define DMX_MAB_US          12

#define PICOSPOT_ADDRESS    1       // startadres fixture

/* UART object + buffer */
static cyhal_uart_t uart_obj;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

/* Eenvoudige error handler */
static void handle_error(void)
{
    CY_ASSERT(0);
}

/* DMX buffer init */
static void dmx_init_buffer(void)
{
    dmx_buffer[0] = DMX_START_CODE;
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

/* Kanaal instellen (1..512) */
static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch < 1 || ch > 512) return;
    dmx_buffer[ch] = value;
}

/* UART init met HAL – zelfde principe als je testcode */
static cy_rslt_t dmx_uart_init(void)
{
    cy_rslt_t result;

    const cyhal_uart_cfg_t uart_config =
    {
        .data_bits      = 8,
        .stop_bits      = 2,
        .parity         = CYHAL_UART_PARITY_NONE,
        .rx_buffer      = NULL,
        .rx_buffer_size = 0
    };

    /* Let op: hier dezelfde pins gebruiken als je werkende testcode */
    result = cyhal_uart_init(&uart_obj,
                             CYBSP_DEBUG_UART_TX,
                             CYBSP_DEBUG_UART_RX,
                             NC, NC,
                             NULL,
                             &uart_config);
    if (result != CY_RSLT_SUCCESS)
        return result;

    uint32_t actual_baud;
    result = cyhal_uart_set_baud(&uart_obj, DMX_BAUDRATE, &actual_baud);
    if (result != CY_RSLT_SUCCESS)
        return result;

    return CY_RSLT_SUCCESS;
}

/* BREAK + MAB met HAL-GPIO, zo licht mogelijk */
static void dmx_send_break_mab(void)
{
    /* UART tijdelijk loslaten, maar uart_obj NIET vernietigen */
    cyhal_uart_free(&uart_obj);   // maakt de pin weer vrij

    /* TX-pin als GPIO configureren */
    cyhal_gpio_init(CYBSP_DEBUG_UART_TX,
                    CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG,
                    1);          // idle hoog

    /* BREAK: lijn laag */
    cyhal_gpio_write(CYBSP_DEBUG_UART_TX, 0);
    Cy_SysLib_DelayUs(DMX_BREAK_US);

    /* MAB: lijn hoog */
    cyhal_gpio_write(CYBSP_DEBUG_UART_TX, 1);
    Cy_SysLib_DelayUs(DMX_MAB_US);

    /* GPIO vrijgeven (optioneel) */
    cyhal_gpio_free(CYBSP_DEBUG_UART_TX);

    /* UART direct opnieuw initialiseren */
    if (dmx_uart_init() != CY_RSLT_SUCCESS)
        handle_error();
}

/* Eén volledig DMX-frame versturen */
static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;

    dmx_send_break_mab();                         // break + MAB
    cyhal_uart_write(&uart_obj, dmx_buffer, &len); // blokkerend send, 8N2
}

/* MAIN */
int main(void)
{
    cy_rslt_t result;

    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
        handle_error();

    __enable_irq();

    /* User LED init voor feedback */
    cyhal_gpio_init(CYBSP_USER_LED,
                    CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG,
                    1);

    dmx_init_buffer();

    /* Init UART (eerste keer) */
    result = dmx_uart_init();
    if (result != CY_RSLT_SUCCESS)
        handle_error();

    /* Voorbeeldkanalen voor PicoSpot (9 kanaals modus) */
    uint16_t addr = PICOSPOT_ADDRESS;
    dmx_set_channel(addr + 0, 128);   // Pan midden
    dmx_set_channel(addr + 1, 128);   // Tilt midden
    dmx_set_channel(addr + 2, 0);     // Pan fine
    dmx_set_channel(addr + 3, 0);     // Tilt fine
    dmx_set_channel(addr + 4, 0);     // Speed
    dmx_set_channel(addr + 5, 0);     // Color
    dmx_set_channel(addr + 6, 0);     // Gobo
    dmx_set_channel(addr + 7, 255);   // Dimmer full
    dmx_set_channel(addr + 8, 0);     // Shutter open

    uint8_t color_value = 0;
    int color_dir = 1;
    uint32_t frame_count = 0;

    for (;;)
    {
        dmx_send_frame();                 // stuurt continu frames, goed zichtbaar op scope

        /* Eenvoudige kleur‑animatie */
        if ((frame_count % 10) == 0)
        {
            color_value += color_dir;
            if (color_value == 0 || color_value == 255)
                color_dir = -color_dir;

            dmx_set_channel(addr + 5, color_value);
        }

        cyhal_gpio_toggle(CYBSP_USER_LED);
		Cy_SysLib_Delay(100);	

        Cy_SysLib_Delay(DMX_REFRESH_MS);
        frame_count++;
    }
}
