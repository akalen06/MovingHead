#include "cybsp.h"
#include "cyhal.h"
#include "cycfg.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "app_types.h"
#include "capsense_task.h"
#include "lighting_task.h"
#include "dmx_task.h"

QueueHandle_t controlQueue = NULL;

static void handle_error(void)
{
    __disable_irq();
    CY_ASSERT(0);
    for(;;) {}
}

int main(void)
{
    cy_rslt_t result;

    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
        handle_error();

    __enable_irq();
    init_cycfg_all();

    /* User LED is active LOW op deze kit */
    cyhal_gpio_init(CYBSP_USER_LED,
                    CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG,
                    1);

    dmxCommandQueue = xQueueCreate(5, sizeof(dmx_command_t));
    controlQueue    = xQueueCreate(1, sizeof(control_state_t));

    if ((dmxCommandQueue == NULL) || (controlQueue == NULL))
        handle_error();

    control_state_t initial_ctrl =
    {
        .active_mode = EQ_MODE_RUSTIG,
        .volume_threshold = 1500u,
        .blackout = false
    };

    xQueueOverwrite(controlQueue, &initial_ctrl);

    xTaskCreate(dmx_task,      "DMX",      1024, NULL, 4, NULL);
    xTaskCreate(capsense_task, "CapSense", 2048, NULL, 3, NULL);
    xTaskCreate(lighting_task, "Lighting", 1024, NULL, 2, NULL);

    vTaskStartScheduler();

    for (;;)
    {
    }
}