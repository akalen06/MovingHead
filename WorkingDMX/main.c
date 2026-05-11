#include "cy_syslib.h"
#include "cyhal.h"
#include "cybsp.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* =========================
 * DMX parameters
 * ========================= */
#define DMX_UNIVERSE_SIZE   513
#define DMX_START_CODE      0x00
#define DMX_BAUDRATE        250000
#define DMX_REFRESH_MS      20
#define DMX_BREAK_US        200
#define DMX_MAB_US          12

#define PICOSPOT_ADDRESS    1
#define PICOSPOT_CHANNELS   9

/* =========================
 * PicoSpot 20 LED 9ch mode
 * OFL:
 * 1 Pan
 * 2 Tilt
 * 3 Pan fine
 * 4 Tilt fine
 * 5 Pan/Tilt Speed
 * 6 Color Wheel
 * 7 Gobo Wheel
 * 8 Dimmer
 * 9 Shutter/Strobe
 * ========================= */

/* Color Wheel examples */
#define COLOR_WHITE         5
#define COLOR_RED           16
#define COLOR_ORANGE        27
#define COLOR_YELLOW        38
#define COLOR_GREEN         49
#define COLOR_BLUE          60
#define COLOR_CYAN          71
#define COLOR_PURPLE        82

/* Gobo Wheel examples */
#define GOBO_OPEN           0
#define GOBO_1              20
#define GOBO_2              36
#define GOBO_3              52
#define GOBO_4              68
#define GOBO_5              84
#define GOBO_6              100
#define GOBO_7              116

/* Shutter / Strobe */
#define SHUTTER_OPEN        0
#define STROBE_SLOW         40
#define STROBE_MEDIUM       120
#define STROBE_FAST         220

/* =========================
 * UART object + buffer
 * ========================= */
static cyhal_uart_t uart_obj;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

/* =========================
 * Hardware timer voor break/MAB
 * Vervangt de destructieve UART-free/re-init + busy-wait aanpak.
 * ========================= */
static cyhal_timer_t break_timer;
static volatile bool break_done = false;
static volatile bool mab_done   = false;

/* Timer callback: stuurt de GPIO-state machine verder */
static void break_timer_isr(void *callback_arg, cyhal_timer_event_t event)
{
    (void)callback_arg;
    (void)event;

    if (!break_done)
    {
        /* BREAK voorbij: lijn hoog (MAB) */
        cyhal_gpio_write(CYBSP_DEBUG_UART_TX, 1);
        break_done = true;

        /* Herstart timer voor MAB-duur.
         * Frequentie is al 1 MHz (ingesteld in dmx_timer_init),
         * dus enkel herconfigureren en starten volstaat. */
        cyhal_timer_stop(&break_timer);

        const cyhal_timer_cfg_t mab_cfg = {
            .compare_value = 0,
            .period        = DMX_MAB_US - 1,
            .direction     = CYHAL_TIMER_DIR_UP,
            .is_compare    = false,
            .is_continuous = false,
            .value         = 0
        };
        cyhal_timer_configure(&break_timer, &mab_cfg);
        cyhal_timer_start(&break_timer);
    }
    else
    {
        /* MAB voorbij */
        mab_done = true;
        cyhal_timer_stop(&break_timer);
    }
}

/* =========================
 * FreeRTOS
 * ========================= */
static QueueHandle_t dmxCommandQueue;

/*
 * FIX: speed-state wordt niet langer gedeeld via een volatile variabele
 * tussen ISR en task.  De ISR stuurt nu een bericht via een aparte
 * single-element queue, zodat er geen race condition mogelijk is.
 */
static QueueHandle_t speedEventQueue;

/* =========================
 * DMX command struct
 * ========================= */
typedef struct
{
    uint16_t address;
    uint8_t channel_values[PICOSPOT_CHANNELS];
} dmx_command_t;

/* =========================
 * Scene struct
 * ========================= */
typedef struct
{
    uint8_t pan;
    uint8_t tilt;
    uint8_t pan_fine;
    uint8_t tilt_fine;
    uint8_t move_speed;
    uint8_t color;
    uint8_t gobo;
    uint8_t dimmer;
    uint8_t shutter;
} scene_t;

/* =========================
 * Speed state machine
 * ========================= */
typedef enum
{
    SPEED_SLOW = 0,
    SPEED_NORMAL,
    SPEED_FAST,
    SPEED_STATE_COUNT
} speed_state_t;

/* =========================
 * Example scenes
 * ========================= */

/* Startscene: wit, naar boven, rustig */
static const scene_t base_scene =
{
    .pan        = 128,
    .tilt       = 128,
    .pan_fine   = 0,
    .tilt_fine  = 0,
    .move_speed = 128,
    .color      = COLOR_WHITE,
    .gobo       = GOBO_OPEN,
    .dimmer     = 255,
    .shutter    = SHUTTER_OPEN
};

static const scene_t scenes[] =
{
    /* Scene 1: center, cyan */
    {
        .pan = 128, .tilt = 128,
        .pan_fine = 0, .tilt_fine = 0,
        .move_speed = 0,
        .color = COLOR_CYAN,
        .gobo = GOBO_OPEN,
        .dimmer = 250,
        .shutter = SHUTTER_OPEN
    },
    /* Scene 2: slightly off-center, red */
    {
        .pan = 150, .tilt = 150,
        .pan_fine = 0, .tilt_fine = 0,
        .move_speed = 0,
        .color = COLOR_RED,
        .gobo = GOBO_OPEN,
        .dimmer = 250,
        .shutter = SHUTTER_OPEN
    },
    /* Scene 3: blue + gobo */
    {
        .pan = 80, .tilt = 170,
        .pan_fine = 0, .tilt_fine = 0,
        .move_speed = 20,
        .color = COLOR_BLUE,
        .gobo = GOBO_3,
        .dimmer = 255,
        .shutter = SHUTTER_OPEN
    },
    /* Scene 4: purple + fast strobe */
    {
        .pan = 200, .tilt = 70,
        .pan_fine = 0, .tilt_fine = 0,
        .move_speed = 10,
        .color = COLOR_PURPLE,
        .gobo = GOBO_1,
        .dimmer = 255,
        .shutter = STROBE_FAST
    }
};

#define SCENE_COUNT   (sizeof(scenes) / sizeof(scenes[0]))

/* =========================
 * Error handler
 * ========================= */
static void handle_error(void)
{
    CY_ASSERT(0);
    for(;;) {}
}

/* =========================
 * DMX buffer init
 * ========================= */
static void dmx_init_buffer(void)
{
    dmx_buffer[0] = DMX_START_CODE;
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

/* =========================
 * Set channel
 * ========================= */
static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch < 1 || ch > 512)
        return;
    dmx_buffer[ch] = value;
}

/* =========================
 * UART init
 * ========================= */
static cy_rslt_t dmx_uart_init(void)
{
    cy_rslt_t result;
    uint32_t actual_baud;

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

    result = cyhal_uart_set_baud(&uart_obj, DMX_BAUDRATE, &actual_baud);
    if (result != CY_RSLT_SUCCESS)
        return result;

    return CY_RSLT_SUCCESS;
}

/* =========================
 * Hardware timer init voor break/MAB
 * ========================= */
static void dmx_timer_init(void)
{
    cy_rslt_t result;

    /* Initialiseer timer op 1 MHz zodat elke tick = 1 µs.
     * Die frequentie geldt voor zowel de BREAK- als de MAB-fase. */
    result = cyhal_timer_init(&break_timer, NC, NULL);
    if (result != CY_RSLT_SUCCESS)
        handle_error();

    /* Stel basisfrequentie in op 1 MHz (1 tick = 1 µs) */
    const cyhal_timer_cfg_t init_cfg = {
        .compare_value = 0,
        .period        = DMX_BREAK_US - 1,   /* wordt overschreven voor gebruik */
        .direction     = CYHAL_TIMER_DIR_UP,
        .is_compare    = false,
        .is_continuous = false,
        .value         = 0
    };
    cyhal_timer_configure(&break_timer, &init_cfg);

    cyhal_timer_register_callback(&break_timer, break_timer_isr, NULL);
    cyhal_timer_enable_event(&break_timer,
                             CYHAL_TIMER_IRQ_TERMINAL_COUNT,
                             5,
                             true);
}

/* =========================
 * BREAK + MAB via hardware timer
 * ========================= */
static void dmx_send_break_mab(void)
{
    /* Geef UART vrij zodat we de TX-pin als GPIO kunnen gebruiken */
    cyhal_uart_free(&uart_obj);

    cyhal_gpio_init(CYBSP_DEBUG_UART_TX,
                    CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG,
                    0); /* direct laag = begin BREAK */

    /* Reset state flags */
    break_done = false;
    mab_done   = false;

    /* Configureer timer voor BREAK-duur (DMX_BREAK_US µs bij 1 MHz) */
    const cyhal_timer_cfg_t break_cfg = {
        .compare_value = 0,
        .period        = DMX_BREAK_US - 1,
        .direction     = CYHAL_TIMER_DIR_UP,
        .is_compare    = false,
        .is_continuous = false,
        .value         = 0
    };
    cyhal_timer_configure(&break_timer, &break_cfg);
    cyhal_timer_start(&break_timer);

    /* Wacht tot MAB klaar is */
    while (!mab_done) {}

    /* Geef GPIO vrij en herstel UART */
    cyhal_gpio_free(CYBSP_DEBUG_UART_TX);

    if (dmx_uart_init() != CY_RSLT_SUCCESS)
        handle_error();
}

/* =========================
 * Send DMX frame
 * ========================= */
static void dmx_send_frame(void)
{
    size_t len = DMX_UNIVERSE_SIZE;

    dmx_send_break_mab();
    cyhal_uart_write(&uart_obj, dmx_buffer, &len);
}

/* =========================
 * Apply 9ch fixture command
 * ========================= */
static void dmx_apply_command(const dmx_command_t *cmd)
{
    for (int i = 1; i <= 512; i++)
        dmx_buffer[i] = 0;

    for (int i = 0; i < PICOSPOT_CHANNELS; i++)
        dmx_set_channel(cmd->address + i, cmd->channel_values[i]);
}

/* =========================
 * Build command from scene
 * ========================= */
static void build_scene_command(const scene_t *scene, dmx_command_t *cmd)
{
    cmd->address = PICOSPOT_ADDRESS;

    cmd->channel_values[0] = scene->pan;
    cmd->channel_values[1] = scene->tilt;
    cmd->channel_values[2] = scene->pan_fine;
    cmd->channel_values[3] = scene->tilt_fine;
    cmd->channel_values[4] = scene->move_speed;
    cmd->channel_values[5] = scene->color;
    cmd->channel_values[6] = scene->gobo;
    cmd->channel_values[7] = scene->dimmer;
    cmd->channel_values[8] = scene->shutter;
}

/* =========================
 * Button ISR
 * ========================= */
static cyhal_gpio_callback_data_t button_cb_data;

static void button_isr(void *callback_arg, cyhal_gpio_event_t event)
{
    (void)callback_arg;
    (void)event;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t dummy = 1;

    xQueueSendFromISR(speedEventQueue, &dummy, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* =========================
 * Button init
 * ========================= */
static void button_init(void)
{
    cy_rslt_t result;

    result = cyhal_gpio_init(CYBSP_USER_BTN,
                             CYHAL_GPIO_DIR_INPUT,
                             CYHAL_GPIO_DRIVE_PULLUP,
                             1);
    if (result != CY_RSLT_SUCCESS)
        handle_error();

    button_cb_data.callback     = button_isr;
    button_cb_data.callback_arg = NULL;
    cyhal_gpio_register_callback(CYBSP_USER_BTN, &button_cb_data);

    cyhal_gpio_enable_event(CYBSP_USER_BTN,
                            CYHAL_GPIO_IRQ_FALL,
                            5,
                            true);
}

/* =========================
 * DMX Task
 * ========================= */
static void dmx_task(void *pvParameters)
{
    (void) pvParameters;

    dmx_command_t current_cmd = {0};
    dmx_command_t new_cmd;

    dmx_init_buffer();

    build_scene_command(&base_scene, &current_cmd);
    dmx_apply_command(&current_cmd);

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

/* =========================
 * Sweep-effect (SPEED_FAST)
 *
 * Aangepast:
 * - kleinere panstap
 * - iets langere tick
 *
 * Daardoor krijgt de fixture net genoeg tijd om effectief
 * van 0 naar 255 en terug te bewegen, i.p.v. constant achter
 * de referentie aan te lopen.
 * ========================= */
#define SWEEP_PAN_STEP    4
#define SWEEP_TILT_MIN    90
#define SWEEP_TILT_MAX    165
#define SWEEP_TILT_STEP   1
#define SWEEP_STEP_MS     40

/* Sweep-state: bijgehouden tussen ticks */
typedef struct
{
    int16_t pan;
    int8_t  pan_dir;   /* +1 of -1 */
    int16_t tilt;
    int8_t  tilt_dir;  /* +1 of -1 */
    uint8_t color_idx;
    uint8_t color_tick; /* wissel kleur elke N ticks */
} sweep_state_t;

static const uint8_t sweep_colors[] = {
    COLOR_RED, COLOR_BLUE, COLOR_PURPLE,
    COLOR_CYAN, COLOR_GREEN, COLOR_ORANGE
};
#define SWEEP_COLOR_COUNT  6
#define SWEEP_COLOR_TICKS  10   /* nieuwe kleur elke 10 ticks */

static void sweep_state_init(sweep_state_t *s)
{
    s->pan        = 0;
    s->pan_dir    = 1;
    s->tilt       = SWEEP_TILT_MIN;
    s->tilt_dir   = 1;
    s->color_idx  = 0;
    s->color_tick = 0;
}

static void sweep_state_tick(sweep_state_t *s, dmx_command_t *cmd)
{
    /* Pan: trager heen en weer zodat de fixture volledig kan volgen */
    s->pan += s->pan_dir * SWEEP_PAN_STEP;
    if (s->pan >= 255) { s->pan = 255; s->pan_dir = -1; }
    if (s->pan <= 0)   { s->pan = 0;   s->pan_dir =  1; }

    /* Tilt: swingt traag als een slinger */
    s->tilt += s->tilt_dir * SWEEP_TILT_STEP;
    if (s->tilt >= SWEEP_TILT_MAX) { s->tilt = SWEEP_TILT_MAX; s->tilt_dir = -1; }
    if (s->tilt <= SWEEP_TILT_MIN) { s->tilt = SWEEP_TILT_MIN; s->tilt_dir =  1; }

    /* Kleur: wisselt elke SWEEP_COLOR_TICKS ticks */
    s->color_tick++;
    if (s->color_tick >= SWEEP_COLOR_TICKS)
    {
        s->color_tick = 0;
        s->color_idx  = (s->color_idx + 1) % SWEEP_COLOR_COUNT;
    }

    cmd->address = PICOSPOT_ADDRESS;
    cmd->channel_values[0] = (uint8_t)s->pan;
    cmd->channel_values[1] = (uint8_t)s->tilt;
    cmd->channel_values[2] = 0;
    cmd->channel_values[3] = 0;
    cmd->channel_values[4] = 0;    /* pan/tilt movement fast */
    cmd->channel_values[5] = sweep_colors[s->color_idx];
    cmd->channel_values[6] = GOBO_OPEN;
    cmd->channel_values[7] = 255;
    cmd->channel_values[8] = SHUTTER_OPEN;
}

/* =========================
 * Scene Task
 * ========================= */
static void scene_task(void *pvParameters)
{
    (void) pvParameters;

    dmx_command_t cmd;
    uint32_t      scene_index = 0;
    speed_state_t speed       = SPEED_SLOW;
    uint8_t       btn_event;
    sweep_state_t sweep;
    sweep_state_init(&sweep);

    /* Stuur base scene als startpunt */
    build_scene_command(&base_scene, &cmd);
    xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);

    for (;;)
    {
        /* Verwerk eventuele knopdruk (niet-blokkerend) */
        if (xQueueReceive(speedEventQueue, &btn_event, 0) == pdPASS)
        {
            speed_state_t prev = speed;
            speed = (speed_state_t)(speed + 1);
            if (speed >= SPEED_STATE_COUNT)
                speed = SPEED_SLOW;

            if (prev == SPEED_FAST)
                scene_index = 0;

            if (speed == SPEED_FAST)
                sweep_state_init(&sweep);
        }

        if (speed == SPEED_FAST)
        {
            /* Pan beweegt nu trager zodat 0..255 echt haalbaar is */
            sweep_state_tick(&sweep, &cmd);
            xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(SWEEP_STEP_MS));
        }
        else
        {
            uint32_t delay_ms;
            switch (speed)
            {
                case SPEED_SLOW:   delay_ms = 2000; break;
                case SPEED_NORMAL: delay_ms = 1000; break;
                default:           delay_ms = 2000; break;
            }

            build_scene_command(&scenes[scene_index], &cmd);
            xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);

            scene_index++;
            if (scene_index >= SCENE_COUNT)
                scene_index = 0;

            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
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

    if (dmx_uart_init() != CY_RSLT_SUCCESS)
        handle_error();

    dmx_timer_init();
    button_init();

    dmxCommandQueue = xQueueCreate(5, sizeof(dmx_command_t));
    if (dmxCommandQueue == NULL)
        handle_error();

    speedEventQueue = xQueueCreate(1, sizeof(uint8_t));
    if (speedEventQueue == NULL)
        handle_error();

    xTaskCreate(dmx_task,   "DMX Task",   1024, NULL, 2, NULL);
    xTaskCreate(scene_task, "Scene Task", 1024, NULL, 3, NULL);

    vTaskStartScheduler();

    for (;;) {}
}