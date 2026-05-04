#include "capsense_task.h"
#include "app_types.h"
#include "cybsp.h"
#include "cyhal.h"
#include "cycfg.h"
#include "cycfg_capsense.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"

#define CAPSENSE_INTR_PRIORITY  (7u)
#define THRESHOLD_MIN           (500u)
#define THRESHOLD_MAX           (3000u)

extern QueueHandle_t controlQueue;

/* Semaphore is veiliger dan volatile bool:
 * geen compiler-cache risico, geen memory barrier nodig */
static SemaphoreHandle_t capsense_scan_sem;

static void capsense_isr(void)
{
    Cy_CapSense_InterruptHandler(CYBSP_CSD_HW, &cy_capsense_context);
}

static void capsense_callback(cy_stc_active_scan_sns_t *ptrActiveScan)
{
    (void)ptrActiveScan;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(capsense_scan_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static uint32_t initialize_capsense(void)
{
    uint32_t status = CYRET_SUCCESS;

    status = Cy_CapSense_Init(&cy_capsense_context);
    if (status != CYRET_SUCCESS)
        return status;

    cyhal_system_set_isr(csd_interrupt_IRQn,
                         csd_interrupt_IRQn,
                         CAPSENSE_INTR_PRIORITY,
                         &capsense_isr);

    NVIC_ClearPendingIRQ(csd_interrupt_IRQn);
    NVIC_EnableIRQ(csd_interrupt_IRQn);

    status = Cy_CapSense_Enable(&cy_capsense_context);
    if (status != CYRET_SUCCESS)
        return status;

    status = Cy_CapSense_RegisterCallback(CY_CAPSENSE_END_OF_SCAN_E,
                                          capsense_callback,
                                          &cy_capsense_context);
    return status;
}

static void process_touch(control_state_t *ctrl)
{
    uint32_t button0_status;
    uint32_t button1_status;
    cy_stc_capsense_touch_t *slider_touch_info;
    uint16_t slider_pos = 0;
    uint8_t slider_touch_status = 0;

    static uint32_t button0_status_prev = 0;
    static uint32_t button1_status_prev = 0;
    static uint16_t slider_pos_prev = 0;

    button0_status = Cy_CapSense_IsSensorActive(
        CY_CAPSENSE_BUTTON0_WDGT_ID,
        CY_CAPSENSE_BUTTON0_SNS0_ID,
        &cy_capsense_context);

    button1_status = Cy_CapSense_IsSensorActive(
        CY_CAPSENSE_BUTTON1_WDGT_ID,
        CY_CAPSENSE_BUTTON1_SNS0_ID,
        &cy_capsense_context);

    slider_touch_info = Cy_CapSense_GetTouchInfo(
        CY_CAPSENSE_LINEARSLIDER0_WDGT_ID,
        &cy_capsense_context);

    if (slider_touch_info != NULL)
    {
        slider_touch_status = slider_touch_info->numPosition;
        if (slider_touch_info->ptrPosition != NULL)
            slider_pos = slider_touch_info->ptrPosition->x;
    }

    if ((button0_status != 0u) && (button0_status_prev == 0u))
    {
        ctrl->active_mode = EQ_MODE_RUSTIG;
        ctrl->blackout = false;

        /* LED confirmatie */
        cyhal_gpio_write(CYBSP_USER_LED, 0);
    }

    if ((button1_status != 0u) && (button1_status_prev == 0u))
    {
        ctrl->active_mode = EQ_MODE_AGRESSIEF;
        ctrl->blackout = false;

        /* LED confirmatie */
        cyhal_gpio_write(CYBSP_USER_LED, 1);
    }

    if ((slider_touch_status != 0u) && (slider_pos != slider_pos_prev))
    {
        uint32_t xres =
            cy_capsense_context.ptrWdConfig[CY_CAPSENSE_LINEARSLIDER0_WDGT_ID].xResolution;

        if (xres != 0u)
        {
            ctrl->volume_threshold = THRESHOLD_MIN +
                (uint16_t)(((uint32_t)(THRESHOLD_MAX - THRESHOLD_MIN) * slider_pos) / xres);
        }
    }

    button0_status_prev = button0_status;
    button1_status_prev = button1_status;
    slider_pos_prev = slider_pos;
}

void capsense_task(void *pvParameters)
{
    (void)pvParameters;

    /* Semaphore aanmaken vóór CapSense init,
     * zodat de callback hem meteen kan gebruiken */
    capsense_scan_sem = xSemaphoreCreateBinary();
    if (capsense_scan_sem == NULL)
    {
        CY_ASSERT(0);
    }

    control_state_t ctrl =
    {
        .active_mode = EQ_MODE_RUSTIG,
        .volume_threshold = 1500u,
        .blackout = false
    };

    if (initialize_capsense() != CYRET_SUCCESS)
    {
        CY_ASSERT(0);
    }

    Cy_CapSense_ScanAllWidgets(&cy_capsense_context);

    for (;;)
    {
        /* Wacht tot de ISR/callback de semaphore geeft */
        if (xSemaphoreTake(capsense_scan_sem, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);
            process_touch(&ctrl);
            xQueueOverwrite(controlQueue, &ctrl);
            Cy_CapSense_ScanAllWidgets(&cy_capsense_context);
        }
        /* Geen vTaskDelay nodig: xSemaphoreTake blokkeert al */
    }
}