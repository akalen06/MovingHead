#include "dmx_task.h"
#include "cy_syslib.h"
#include "cyhal.h"
#include "cybsp.h"
#include <string.h>

#define DMX_UNIVERSE_SIZE   513
#define DMX_START_CODE      0x00
#define DMX_BAUDRATE        250000
#define DMX_REFRESH_MS      20
#define DMX_BREAK_US        200
#define DMX_MAB_US          12

#define DMX_TX_PIN          P5_1
#define DMX_RX_PIN          P5_0

static cyhal_uart_t uart_obj;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

QueueHandle_t dmxCommandQueue = NULL;

static void handle_error(void)
{
    __disable_irq();
    CY_ASSERT(0);
    for(;;) {}
}

static void dmx_init_buffer(void)
{
    dmx_buffer[0] = DMX_START_CODE;
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch >= 1 && ch <= 512)
        dmx_buffer[ch] = value;
}

cy_rslt_t dmx_uart_init(void)
{
    cy_rslt_t result;
    uint32_t actual_baud;

    const cyhal_uart_cfg_t uart_config =
    {
        .data_bits = 8,
        .stop_bits = 2,
        .parity = CYHAL_UART_PARITY_NONE,
        .rx_buffer = NULL,
        .rx_buffer_size = 0
    };

    result = cyhal_uart_init(&uart_obj,
                             DMX_TX_PIN,
                             DMX_RX_PIN,
                             NC, NC,
                             NULL,
                             &uart_config);
    if (result != CY_RSLT_SUCCESS)
        return result;

    result = cyhal_uart_set_baud(&uart_obj, DMX_BAUDRATE, &actual_baud);
    return result;
}

static void dmx_send_break_mab(void)
{
    cyhal_uart_free(&uart_obj);

    cyhal_gpio_init(DMX_TX_PIN,
                    CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG,
                    1);

    cyhal_gpio_write(DMX_TX_PIN, 0);
    Cy_SysLib_DelayUs(DMX_BREAK_US);

    cyhal_gpio_write(DMX_TX_PIN, 1);
    Cy_SysLib_DelayUs(DMX_MAB_US);

    cyhal_gpio_free(DMX_TX_PIN);

    if (dmx_uart_init() != CY_RSLT_SUCCESS)
        handle_error();
}

static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;
    dmx_send_break_mab();
    cyhal_uart_write(&uart_obj, dmx_buffer, &len);
}

static void dmx_apply_command(const dmx_command_t *cmd)
{
    for (int i = 1; i <= 512; i++)
        dmx_buffer[i] = 0;

    for (int i = 0; i < PICOSPOT_CHANNELS; i++)
        dmx_set_channel(cmd->address + i, cmd->channel_values[i]);
}

void dmx_task(void *pvParameters)
{
    (void) pvParameters;

    /* UART init hier, binnen de task, zodat de scheduler al loopt
     * en er geen conflict is met CapSense init in main() */
    if (dmx_uart_init() != CY_RSLT_SUCCESS)
    {
        __disable_irq();
        CY_ASSERT(0);
        for(;;) {}
    }

    dmx_command_t current_cmd = {0};
    dmx_command_t new_cmd;

    dmx_init_buffer();

    for (;;)
    {
        if (xQueueReceive(dmxCommandQueue, &new_cmd, 0) == pdPASS)
        {
            current_cmd = new_cmd;
            dmx_apply_command(&current_cmd);
        }

        dmx_send_frame();
        vTaskDelay(pdMS_TO_TICKS(DMX_REFRESH_MS));
    }
}