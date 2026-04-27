#include "cy_syslib.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cycfg.h"          // voor gegenereerde configuratie, incl. capsense
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"

/* =========================
 * DMX parameters
 * ========================= */
#define DMX_UNIVERSE_SIZE     513
#define DMX_START_CODE        0x00
#define DMX_BAUDRATE          250000
#define DMX_REFRESH_MS        20
#define DMX_BREAK_US          200
#define DMX_MAB_US            12

#define PICOSPOT_ADDRESS      1
#define PICOSPOT_CHANNELS     9

/* =========================
 * Event flags
 * ========================= */
#define EVT_BEAT_DETECTED     (1 << 0)
#define EVT_AUDIO_UPDATED     (1 << 1)
#define EVT_MODE_CHANGED      (1 << 2)

/* =========================
 * Audio / mode enums
 * ========================= */
typedef enum
{
    EQ_MODE_RUSTIG = 0,
    EQ_MODE_AGRESSIEF,
    EQ_MODE_LEVENDIG,
    EQ_MODE_COUNT
} eq_mode_t;

typedef enum
{
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_LISTENING,
    AUDIO_STATE_BEAT,
    AUDIO_STATE_PEAK
} audio_state_t;

/* =========================
 * Data structures
 * ========================= */
typedef struct
{
    uint16_t address;
    uint8_t channel_values[PICOSPOT_CHANNELS];
} dmx_command_t;

typedef struct
{
    uint16_t bass_energy;
    uint16_t mid_energy;
    uint16_t treble_energy;
    bool beat_detected;
    uint16_t bpm_estimate;
    audio_state_t state;
} audio_features_t;

typedef struct
{
    eq_mode_t active_mode;
    uint8_t sensitivity;
    bool auto_tilt_enable;
    bool blackout;
} control_state_t;

/* =========================
 * Globals
 * ========================= */
static cyhal_uart_t uart_obj;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

static QueueHandle_t dmxCommandQueue;
static QueueHandle_t audioFeatureQueue;
static QueueHandle_t controlQueue;
static EventGroupHandle_t systemEvents;

/* Shared latest state */
static audio_features_t g_audio_features = {0};
static control_state_t  g_control_state =
{
    .active_mode = EQ_MODE_RUSTIG,
    .sensitivity = 5,
    .auto_tilt_enable = true,
    .blackout = false
};

/* =========================
 * Utility / error
 * ========================= */
static void handle_error(void)
{
    CY_ASSERT(0);
    for(;;) {}
}

/* =========================
 * DMX helpers
 * ========================= */
static void dmx_init_buffer(void)
{
    dmx_buffer[0] = DMX_START_CODE;
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

static void dmx_clear_universe(void)
{
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch < 1 || ch > 512) return;
    dmx_buffer[ch] = value;
}

static void dmx_set_fixture_9ch(uint16_t base_addr, const uint8_t values[9])
{
    for (int i = 0; i < 9; i++)
        dmx_set_channel(base_addr + i, values[i]);
}

static cy_rslt_t dmx_uart_init(void)
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
                             CYBSP_DEBUG_UART_TX,
                             CYBSP_DEBUG_UART_RX,
                             NC, NC,
                             NULL,
                             &uart_config);
    if (result != CY_RSLT_SUCCESS)
        return result;

    result = cyhal_uart_set_baud(&uart_obj, DMX_BAUDRATE, &actual_baud);
    if (result != CY_RSLT_SUCCESS)
        return result;

    return CY_RSLT_SUCCESS;
}

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

static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;
    dmx_send_break_mab();
    cyhal_uart_write(&uart_obj, dmx_buffer, &len);
}

/* =========================
 * Fixture mapping
 * Pas dit later aan aan de echte PicoSpot channel map
 * ========================= */
static void build_fixture_frame(const audio_features_t *audio,
                                const control_state_t *ctrl,
                                dmx_command_t *out_cmd)
{
    uint8_t ch[9] = {0};

    out_cmd->address = PICOSPOT_ADDRESS;

    if (ctrl->blackout)
    {
        memset(ch, 0, sizeof(ch));
        memcpy(out_cmd->channel_values, ch, sizeof(ch));
        return;
    }

    /* Placeholder mapping
     * ch0: pan
     * ch1: tilt
     * ch2: color
     * ch3: gobo
     * ch4: shutter/strobe
     * ch5: dimmer
     * ch6: speed
     * ch7: macro/function
     * ch8: reset/reserved
     */

    /* Dimmer volgt globale energie */
    uint16_t total_energy = audio->bass_energy + audio->mid_energy + audio->treble_energy;
    if (total_energy > 3000) total_energy = 3000;
    ch[5] = (uint8_t)((total_energy * 255) / 3000);

    /* Beat -> korte strobe accent */
    ch[4] = audio->beat_detected ? 220 : 0;

    /* Kleurkeuze per mode */
    switch (ctrl->active_mode)
    {
        case EQ_MODE_RUSTIG:
            ch[2] = 30;
            ch[6] = 40;
            break;

        case EQ_MODE_AGRESSIEF:
            ch[2] = 180;
            ch[6] = 180;
            break;

        case EQ_MODE_LEVENDIG:
            ch[2] = 100;
            ch[6] = 120;
            break;

        default:
            ch[2] = 0;
            ch[6] = 0;
            break;
    }

    /* Pan/Tilt placeholder op basis van spectrum */
    ch[0] = (uint8_t)((audio->bass_energy   > 1023 ? 1023 : audio->bass_energy)   / 4);
    ch[1] = (uint8_t)((audio->treble_energy > 1023 ? 1023 : audio->treble_energy) / 4);

    /* Macro kanaal als debug/scene identifier */
    ch[7] = (uint8_t)(50 + 50 * ctrl->active_mode);

    /* Reserved */
    ch[8] = 0;

    memcpy(out_cmd->channel_values, ch, sizeof(ch));
}

/* =========================
 * DMX task
 * ========================= */
static void dmx_task(void *pvParameters)
{
    (void) pvParameters;

    dmx_command_t current_cmd = {0};
    dmx_command_t new_cmd;

    dmx_init_buffer();

    for (;;)
    {
        if (xQueueReceive(dmxCommandQueue, &new_cmd, 0) == pdPASS)
        {
            current_cmd = new_cmd;

            dmx_clear_universe();
            dmx_set_fixture_9ch(current_cmd.address, current_cmd.channel_values);
        }

        dmx_send_frame();
        vTaskDelay(pdMS_TO_TICKS(DMX_REFRESH_MS));
    }
}

/* =========================
 * Audio task
 * Placeholder voor echte DSP/beatdetectie
 * ========================= */
static void audio_task(void *pvParameters)
{
    (void) pvParameters;

    audio_features_t feat = {0};
    uint16_t fake_counter = 0;

    for (;;)
    {
        fake_counter++;

        /* TODO later:
         * - samples ophalen van ADC/I2S/PDM
         * - filtering / FFT / envelope
         * - beat detectie
         * - bpm schatting
         */

        feat.bass_energy   = (fake_counter * 37) % 1024;
        feat.mid_energy    = (fake_counter * 53) % 1024;
        feat.treble_energy = (fake_counter * 71) % 1024;

        feat.beat_detected = ((fake_counter % 8) == 0);
        feat.bpm_estimate  = 120;

        if (feat.beat_detected)
            feat.state = AUDIO_STATE_BEAT;
        else
            feat.state = AUDIO_STATE_LISTENING;

        g_audio_features = feat;
        xQueueOverwrite(audioFeatureQueue, &feat);

        xEventGroupSetBits(systemEvents, EVT_AUDIO_UPDATED);
        if (feat.beat_detected)
            xEventGroupSetBits(systemEvents, EVT_BEAT_DETECTED);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* =========================
 * CAPSENSE task
 * Placeholder voor mode-selectie
 * ========================= */
static void capsense_task(void *pvParameters)
{
    (void) pvParameters;

    control_state_t ctrl = g_control_state;

    /* TODO:
     * init_cycfg_all();
     * Cy_CapSense_Init(...)
     * Cy_CapSense_Enable(...)
     * widgets scannen en verwerken
     */

    for (;;)
    {
        /* Placeholder demo:
         * roteer elke 5 seconden door modes
         * later vervangen door CapSense buttons/slider
         */
        static uint8_t demo_mode = 0;

        ctrl.active_mode = (eq_mode_t)demo_mode;
        ctrl.sensitivity = 5;
        ctrl.auto_tilt_enable = true;
        ctrl.blackout = false;

        g_control_state = ctrl;
        xQueueOverwrite(controlQueue, &ctrl);
        xEventGroupSetBits(systemEvents, EVT_MODE_CHANGED);

        demo_mode = (demo_mode + 1) % EQ_MODE_COUNT;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* =========================
 * Lighting / scene engine task
 * Combineert audio + mode -> DMX command
 * ========================= */
static void lighting_task(void *pvParameters)
{
    (void) pvParameters;

    audio_features_t audio = {0};
    control_state_t ctrl = g_control_state;
    dmx_command_t cmd;

    for (;;)
    {
        xQueuePeek(audioFeatureQueue, &audio, 0);
        xQueuePeek(controlQueue, &ctrl, 0);

        build_fixture_frame(&audio, &ctrl, &cmd);
        xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* =========================
 * Optional debug task
 * ========================= */
static void status_task(void *pvParameters)
{
    (void) pvParameters;

    for (;;)
    {
        EventBits_t bits = xEventGroupWaitBits(systemEvents,
                                               EVT_BEAT_DETECTED | EVT_AUDIO_UPDATED | EVT_MODE_CHANGED,
                                               pdTRUE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(500));

        (void)bits;
        /* TODO:
         * serial debug
         * status LED toggles
         * heartbeat monitoring
         */
    }
}

/* =========================
 * Main
 * ========================= */
int main(void)
{
    cy_rslt_t result;

    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
        handle_error();

    __enable_irq();

    /* Als je cycfg gebruikt voor capsense/peripherals */
    init_cycfg_all();

    if (dmx_uart_init() != CY_RSLT_SUCCESS)
        handle_error();

    dmxCommandQueue   = xQueueCreate(5, sizeof(dmx_command_t));
    audioFeatureQueue = xQueueCreate(1, sizeof(audio_features_t));
    controlQueue      = xQueueCreate(1, sizeof(control_state_t));
    systemEvents      = xEventGroupCreate();

    if (!dmxCommandQueue || !audioFeatureQueue || !controlQueue || !systemEvents)
        handle_error();

    /* Init default states in queues */
    xQueueOverwrite(audioFeatureQueue, &g_audio_features);
    xQueueOverwrite(controlQueue, &g_control_state);

    xTaskCreate(dmx_task,      "DMX Task",      1024, NULL, 4, NULL);
    xTaskCreate(audio_task,    "Audio Task",    1024, NULL, 3, NULL);
    xTaskCreate(capsense_task, "CapSense Task", 1024, NULL, 3, NULL);
    xTaskCreate(lighting_task, "Light Task",    1024, NULL, 2, NULL);
    xTaskCreate(status_task,   "Status Task",    512, NULL, 1, NULL);

    vTaskStartScheduler();

    for (;;)
    {
    }
}