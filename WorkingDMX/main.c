#include "cy_syslib.h"
#include "cyhal.h"
#include "cybsp.h"
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* DMX parameters */
#define DMX_UNIVERSE_SIZE   513
#define DMX_START_CODE      0x00
#define DMX_BAUDRATE        250000
#define DMX_REFRESH_MS      20      
#define DMX_BREAK_US        200
#define DMX_MAB_US          12

#define PICOSPOT_ADDRESS    1

/* UART object + buffer */
static cyhal_uart_t uart_obj;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

/* FreeRTOS */
static QueueHandle_t dmxCommandQueue;

/* DMX command struct */
typedef struct {
    uint16_t address;
    uint8_t channel_values[9];
} dmx_command_t;

/* Error handler */
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

/* Set channel */
static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch < 1 || ch > 512) return;
    dmx_buffer[ch] = value;
}

/* UART init */
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

/* BREAK + MAB */
static void dmx_send_break_mab(void)
{
    cyhal_uart_free(&uart_obj);

    cyhal_gpio_init(CYBSP_DEBUG_UART_TX,
                    CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG,
                    1);

    cyhal_gpio_write(CYBSP_DEBUG_UART_TX, 0);
    Cy_SysLib_DelayUs(DMX_BREAK_US);

    cyhal_gpio_write(CYBSP_DEBUG_UART_TX, 1);
    Cy_SysLib_DelayUs(DMX_MAB_US);

    cyhal_gpio_free(CYBSP_DEBUG_UART_TX);

    if (dmx_uart_init() != CY_RSLT_SUCCESS)
        handle_error();
}

/* Send DMX frame */
static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;

    dmx_send_break_mab();
    cyhal_uart_write(&uart_obj, dmx_buffer, &len);
}

/* DMX Task (REAL-TIME CORE) */
static void dmx_task(void *pvParameters)
{
    (void) pvParameters;

    dmx_command_t current_cmd = {0};
    dmx_command_t new_cmd;

    dmx_init_buffer();

    for (;;)
    {
        /* Check for new command (non-blocking!) */
        if (xQueueReceive(dmxCommandQueue, &new_cmd, 0) == pdPASS)
        {
            current_cmd = new_cmd;

            /* Clear buffer */
            for (int i = 1; i <= 512; i++)
                dmx_buffer[i] = 0;

            /* Apply channels */
            for (int i = 0; i < 9; i++)
                dmx_set_channel(current_cmd.address + i,
                                current_cmd.channel_values[i]);
        }

        /* Always send frame */
        dmx_send_frame();

        vTaskDelay(pdMS_TO_TICKS(DMX_REFRESH_MS));
    }
}

/* Scene Task (replaces while-loop from code 1) */
static void scene_task(void *pvParameters)
{
    (void) pvParameters;

    dmx_command_t cmd;
    cmd.address = PICOSPOT_ADDRESS;

    for (;;)
    {
        /* Scene 1 */
        cmd.channel_values[0] = 128;
        cmd.channel_values[1] = 128;
        cmd.channel_values[2] = 0;
        cmd.channel_values[3] = 0;
        cmd.channel_values[4] = 0;
        cmd.channel_values[5] = 70;
        cmd.channel_values[6] = 0;
        cmd.channel_values[7] = 250;
        cmd.channel_values[8] = 0;

        xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000));

        /* Scene 2 */
        cmd.channel_values[0] = 150;
        cmd.channel_values[1] = 150;
        cmd.channel_values[2] = 0;
        cmd.channel_values[3] = 0;
        cmd.channel_values[4] = 0;
        cmd.channel_values[5] = 15;
        cmd.channel_values[6] = 0;
        cmd.channel_values[7] = 250;
        cmd.channel_values[8] = 0;

        xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* MAIN */
int main(void)
{
    cy_rslt_t result;

    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
        handle_error();

    __enable_irq();

    /* UART init */
    if (dmx_uart_init() != CY_RSLT_SUCCESS)
        handle_error();

    /* Create queue */
    dmxCommandQueue = xQueueCreate(5, sizeof(dmx_command_t));
    if (dmxCommandQueue == NULL)
        handle_error();

    /* Create tasks */
    xTaskCreate(dmx_task, "DMX Task", 1024, NULL, 3, NULL);
    xTaskCreate(scene_task, "Scene Task", 1024, NULL, 2, NULL);

    /* Start scheduler */
    vTaskStartScheduler();

    for (;;);
}