#include "cy_syslib.h"
#include "cyhal.h"
#include "cybsp.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "audio.h"
#include "shared.h"

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

/* Color Wheel */
#define COLOR_WHITE         5
#define COLOR_RED           16
#define COLOR_ORANGE        27
#define COLOR_YELLOW        38
#define COLOR_GREEN         49
#define COLOR_BLUE          60
#define COLOR_CYAN          71
#define COLOR_PURPLE        82

/* Gobo Wheel */
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
 * DMX UART pins
 * P9_1 = TX (confirmed working, wired to RS485 module).
 * P9_0 = RX (declared but not physically used, DMX is TX-only).
 * NOTE: pins are intentionally swapped vs port order — P9_1 is TX
 * because the SCB mux on this kit maps TX to the odd pin on port 9.
 * ========================= */
#define DMX_TX_PIN          P9_1
#define DMX_RX_PIN          P9_0

/* =========================
 * CLI UART
 * Uses the KitProg3 USB-UART bridge (CYBSP_DEBUG_UART_TX/RX).
 * No extra wiring needed — appears as a COM port over the existing
 * USB cable. Open at 115200 8N1 in any serial terminal.
 * ========================= */
#define CLI_TX_PIN          CYBSP_DEBUG_UART_TX
#define CLI_RX_PIN          CYBSP_DEBUG_UART_RX
#define CLI_BAUDRATE        115200
#define CLI_LINE_BUF_LEN    64

/* =========================
 * UART objects + buffers
 * ========================= */
static cyhal_uart_t dmx_uart_obj;
static cyhal_uart_t cli_uart_obj;
static uint8_t dmx_buffer[DMX_UNIVERSE_SIZE];

/* =========================
 * Hardware timer for break/MAB
 * ========================= */
static cyhal_timer_t break_timer;
static volatile bool break_done = false;
static volatile bool mab_done   = false;

static void break_timer_isr(void *callback_arg, cyhal_timer_event_t event)
{
    (void)callback_arg;
    (void)event;

    if (!break_done)
    {
        cyhal_gpio_write(DMX_TX_PIN, 1);
        break_done = true;

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
        mab_done = true;
        cyhal_timer_stop(&break_timer);
    }
}

/* =========================
 * FreeRTOS handles
 * ========================= */
static QueueHandle_t    dmxCommandQueue;
static QueueHandle_t    speedEventQueue;

/* ---- shared state definitions (types in shared.h) ---- */
cli_state_t       g_state;
SemaphoreHandle_t g_state_mutex;

/* Exported to audio.c — audio task posts CLI_CMD_SET_MODE here
 * and updates g_state.bpm via g_state_mutex.                   */
QueueHandle_t     cliCmdQueue;   /* CLI/audio task -> scene task */

static const char *speed_names[] = { "SLOW", "NORMAL", "FAST" };

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

/* speed_names for CLI display */

/* =========================
 * Scenes
 * ========================= */
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
    { .pan=128,.tilt=128,.pan_fine=0,.tilt_fine=0,.move_speed=0,
      .color=COLOR_CYAN,  .gobo=GOBO_OPEN,.dimmer=250,.shutter=SHUTTER_OPEN },
    { .pan=150,.tilt=150,.pan_fine=0,.tilt_fine=0,.move_speed=0,
      .color=COLOR_RED,   .gobo=GOBO_OPEN,.dimmer=250,.shutter=SHUTTER_OPEN },
    { .pan=80, .tilt=170,.pan_fine=0,.tilt_fine=0,.move_speed=20,
      .color=COLOR_BLUE,  .gobo=GOBO_3,  .dimmer=255,.shutter=SHUTTER_OPEN },
    { .pan=200,.tilt=70, .pan_fine=0,.tilt_fine=0,.move_speed=10,
      .color=COLOR_PURPLE,.gobo=GOBO_1,  .dimmer=255,.shutter=STROBE_FAST  }
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
 * DMX buffer
 * ========================= */
static void dmx_init_buffer(void)
{
    dmx_buffer[0] = DMX_START_CODE;
    for (int i = 1; i <= 512; ++i)
        dmx_buffer[i] = 0;
}

static void dmx_set_channel(uint16_t ch, uint8_t value)
{
    if (ch < 1 || ch > 512) return;
    dmx_buffer[ch] = value;
}

/* =========================
 * DMX UART init
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

    result = cyhal_uart_init(&dmx_uart_obj,
                             DMX_TX_PIN,
                             DMX_RX_PIN,
                             NC, NC,
                             NULL,
                             &uart_config);
    if (result != CY_RSLT_SUCCESS) return result;

    result = cyhal_uart_set_baud(&dmx_uart_obj, DMX_BAUDRATE, &actual_baud);
    return result;
}

/* =========================
 * CLI UART init
 * ========================= */
static cy_rslt_t cli_uart_init(void)
{
    cy_rslt_t result;
    uint32_t  actual_baud;

    const cyhal_uart_cfg_t cli_cfg =
    {
        .data_bits      = 8,
        .stop_bits      = 1,
        .parity         = CYHAL_UART_PARITY_NONE,
        .rx_buffer      = NULL,
        .rx_buffer_size = 0
    };

    result = cyhal_uart_init(&cli_uart_obj,
                             CLI_TX_PIN, CLI_RX_PIN,
                             NC, NC,
                             NULL,
                             &cli_cfg);
    if (result != CY_RSLT_SUCCESS) return result;

    result = cyhal_uart_set_baud(&cli_uart_obj, CLI_BAUDRATE, &actual_baud);
    return result;
}

/* =========================
 * CLI printf helper
 * Sends a formatted string over the CLI UART.
 * ========================= */
void cli_print(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t len = strlen(buf);
    cyhal_uart_write(&cli_uart_obj, (uint8_t *)buf, &len);
}

/* =========================
 * CLI getchar (blocking, 1 byte)
 * ========================= */
static char cli_getchar(void)
{
    uint8_t c;
    size_t len = 1;
    while (cyhal_uart_read(&cli_uart_obj, &c, &len) != CY_RSLT_SUCCESS || len == 0)
    {
        len = 1;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return (char)c;
}

/* =========================
 * CLI: show status
 * ========================= */
static void cli_cmd_status(void)
{
    cli_state_t snap;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    snap = g_state;
    xSemaphoreGive(g_state_mutex);

    cli_print("\r\n=== PicoSpot 20 Status ===\r\n");
    cli_print("  Mode       : %s\r\n", speed_names[snap.speed]);
    if (snap.speed != SPEED_FAST)
        cli_print("  Scene      : %lu / %lu\r\n", snap.scene_index + 1, (uint32_t)SCENE_COUNT);
    else
        cli_print("  Scene      : SWEEP (auto)\r\n");
    cli_print("  Frames sent: %lu\r\n", snap.frame_count);
    if (snap.bpm > 0)
        cli_print("  BPM        : %lu\r\n", snap.bpm);
    else
        cli_print("  BPM        : --  (no beat detected yet)\r\n");
    cli_print("  DMX ch (addr %d):\r\n", PICOSPOT_ADDRESS);
    cli_print("    1 Pan          : %3d\r\n", snap.ch[0]);
    cli_print("    2 Tilt         : %3d\r\n", snap.ch[1]);
    cli_print("    3 Pan fine     : %3d\r\n", snap.ch[2]);
    cli_print("    4 Tilt fine    : %3d\r\n", snap.ch[3]);
    cli_print("    5 P/T Speed    : %3d\r\n", snap.ch[4]);
    cli_print("    6 Color Wheel  : %3d\r\n", snap.ch[5]);
    cli_print("    7 Gobo Wheel   : %3d\r\n", snap.ch[6]);
    cli_print("    8 Dimmer       : %3d\r\n", snap.ch[7]);
    cli_print("    9 Shutter/Strb : %3d\r\n", snap.ch[8]);
    cli_print("==========================\r\n");
}

/* =========================
 * CLI: show help
 * ========================= */
static void cli_cmd_help(void)
{
    cli_print("\r\nCommands:\r\n");
    cli_print("  status              - show current state\r\n");
    cli_print("  mode slow           - switch to SLOW mode  (2 s/scene)\r\n");
    cli_print("  mode normal         - switch to NORMAL mode (1 s/scene)\r\n");
    cli_print("  mode fast           - switch to FAST sweep mode\r\n");
    cli_print("  scene <0-%d>         - jump to scene (slow/normal only)\r\n", (int)(SCENE_COUNT - 1));
    cli_print("  help                - this message\r\n\r\n");
}

/* =========================
 * CLI: parse and dispatch one line
 * ========================= */
static void cli_process_line(char *line)
{
    /* Trim trailing whitespace */
    size_t l = strlen(line);
    while (l > 0 && (line[l-1] == '\r' || line[l-1] == '\n' || line[l-1] == ' '))
        line[--l] = '\0';

    if (l == 0) return;

    if (strcmp(line, "status") == 0)
    {
        cli_cmd_status();
    }
    else if (strcmp(line, "help") == 0)
    {
        cli_cmd_help();
    }
    else if (strncmp(line, "mode ", 5) == 0)
    {
        const char *arg = line + 5;
        cli_task_cmd_t cmd;
        cmd.type = CLI_CMD_SET_MODE;

        if (strcmp(arg, "slow") == 0)
            cmd.value = SPEED_SLOW;
        else if (strcmp(arg, "normal") == 0)
            cmd.value = SPEED_NORMAL;
        else if (strcmp(arg, "fast") == 0)
            cmd.value = SPEED_FAST;
        else
        {
            cli_print("  Unknown mode '%s'. Use: slow | normal | fast\r\n", arg);
            return;
        }

        xQueueSend(cliCmdQueue, &cmd, pdMS_TO_TICKS(100));
        cli_print("  Mode -> %s\r\n", speed_names[cmd.value]);
    }
    else if (strncmp(line, "scene ", 6) == 0)
    {
        int idx = atoi(line + 6);
        if (idx < 0 || idx >= (int)SCENE_COUNT)
        {
            cli_print("  Scene index out of range (0-%d)\r\n", (int)(SCENE_COUNT - 1));
            return;
        }
        cli_task_cmd_t cmd = { .type = CLI_CMD_SET_SCENE, .value = (uint32_t)idx };
        xQueueSend(cliCmdQueue, &cmd, pdMS_TO_TICKS(100));
        cli_print("  Scene -> %d\r\n", idx);
    }
    else
    {
        cli_print("  Unknown command '%s'. Type 'help'.\r\n", line);
    }
}

/* =========================
 * Hardware timer init
 * ========================= */
static void dmx_timer_init(void)
{
    cy_rslt_t result;

    result = cyhal_timer_init(&break_timer, NC, NULL);
    if (result != CY_RSLT_SUCCESS) handle_error();

    const cyhal_timer_cfg_t init_cfg = {
        .compare_value = 0,
        .period        = DMX_BREAK_US - 1,
        .direction     = CYHAL_TIMER_DIR_UP,
        .is_compare    = false,
        .is_continuous = false,
        .value         = 0
    };
    cyhal_timer_configure(&break_timer, &init_cfg);
    cyhal_timer_register_callback(&break_timer, break_timer_isr, NULL);
    cyhal_timer_enable_event(&break_timer,
                             CYHAL_TIMER_IRQ_TERMINAL_COUNT, 5, true);
}

/* =========================
 * Break + MAB
 * ========================= */
static void dmx_send_break_mab(void)
{
    cyhal_uart_free(&dmx_uart_obj);
    cyhal_gpio_init(DMX_TX_PIN,
                    CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG, 0);

    break_done = false;
    mab_done   = false;

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

    while (!mab_done) {}

    cyhal_gpio_free(DMX_TX_PIN);
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
    cyhal_uart_write(&dmx_uart_obj, dmx_buffer, &len);
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
                             CYHAL_GPIO_DRIVE_PULLUP, 1);
    if (result != CY_RSLT_SUCCESS) handle_error();

    button_cb_data.callback     = button_isr;
    button_cb_data.callback_arg = NULL;
    cyhal_gpio_register_callback(CYBSP_USER_BTN, &button_cb_data);
    cyhal_gpio_enable_event(CYBSP_USER_BTN, CYHAL_GPIO_IRQ_FALL, 5, true);
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

            /* Update shared state for CLI */
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            memcpy(g_state.ch, current_cmd.channel_values, PICOSPOT_CHANNELS);
            xSemaphoreGive(g_state_mutex);
        }

        dmx_send_frame();

        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_state.frame_count++;
        xSemaphoreGive(g_state_mutex);

        vTaskDelay(pdMS_TO_TICKS(DMX_REFRESH_MS));
    }
}

/* =========================
 * Sweep effect (SPEED_FAST)
 * ========================= */
#define SWEEP_PAN_STEP      4
#define SWEEP_TILT_MIN      90
#define SWEEP_TILT_MAX      165
#define SWEEP_TILT_STEP     1
#define SWEEP_STEP_MS       40
#define SWEEP_COLOR_COUNT   6
#define SWEEP_COLOR_TICKS   10

static const uint8_t sweep_colors[] = {
    COLOR_RED, COLOR_BLUE, COLOR_PURPLE,
    COLOR_CYAN, COLOR_GREEN, COLOR_ORANGE
};

typedef struct
{
    int16_t pan;
    int8_t  pan_dir;
    int16_t tilt;
    int8_t  tilt_dir;
    uint8_t color_idx;
    uint8_t color_tick;
} sweep_state_t;

static void sweep_state_init(sweep_state_t *s)
{
    s->pan = 0; s->pan_dir = 1;
    s->tilt = SWEEP_TILT_MIN; s->tilt_dir = 1;
    s->color_idx = 0; s->color_tick = 0;
}

static void sweep_state_tick(sweep_state_t *s, dmx_command_t *cmd)
{
    s->pan += s->pan_dir * SWEEP_PAN_STEP;
    if (s->pan >= 255) { s->pan = 255; s->pan_dir = -1; }
    if (s->pan <= 0)   { s->pan = 0;   s->pan_dir =  1; }

    s->tilt += s->tilt_dir * SWEEP_TILT_STEP;
    if (s->tilt >= SWEEP_TILT_MAX) { s->tilt = SWEEP_TILT_MAX; s->tilt_dir = -1; }
    if (s->tilt <= SWEEP_TILT_MIN) { s->tilt = SWEEP_TILT_MIN; s->tilt_dir =  1; }

    s->color_tick++;
    if (s->color_tick >= SWEEP_COLOR_TICKS)
    {
        s->color_tick = 0;
        s->color_idx = (s->color_idx + 1) % SWEEP_COLOR_COUNT;
    }

    cmd->address = PICOSPOT_ADDRESS;
    cmd->channel_values[0] = (uint8_t)s->pan;
    cmd->channel_values[1] = (uint8_t)s->tilt;
    cmd->channel_values[2] = 0;
    cmd->channel_values[3] = 0;
    cmd->channel_values[4] = 0;
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

    dmx_command_t  cmd;
    uint32_t       scene_index = 0;
    speed_state_t  speed       = SPEED_SLOW;
    uint8_t        btn_event;
    cli_task_cmd_t cli_cmd;
    sweep_state_t  sweep;
    sweep_state_init(&sweep);

    build_scene_command(&base_scene, &cmd);
    xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);

    for (;;)
    {
        /* --- Physical button: cycle to next mode --- */
        if (xQueueReceive(speedEventQueue, &btn_event, 0) == pdPASS)
        {
            speed_state_t prev = speed;
            speed = (speed_state_t)((speed + 1) % SPEED_STATE_COUNT);

            if (prev == SPEED_FAST) scene_index = 0;
            if (speed == SPEED_FAST) sweep_state_init(&sweep);

            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_state.speed = (uint8_t)speed;
            g_state.scene_index = scene_index;
            xSemaphoreGive(g_state_mutex);

            cli_print("\r\n[BTN] Mode -> %s\r\n> ", speed_names[speed]);
        }

        /* --- CLI command queue --- */
        if (xQueueReceive(cliCmdQueue, &cli_cmd, 0) == pdPASS)
        {
            if (cli_cmd.type == CLI_CMD_SET_MODE)
            {
                speed_state_t prev = speed;
                speed = (speed_state_t)cli_cmd.value;

                if (prev == SPEED_FAST) scene_index = 0;
                if (speed == SPEED_FAST) sweep_state_init(&sweep);

                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                g_state.speed = (uint8_t)speed;
                g_state.scene_index = scene_index;
                xSemaphoreGive(g_state_mutex);
            }
            else if (cli_cmd.type == CLI_CMD_SET_SCENE)
            {
                if (speed != SPEED_FAST)
                {
                    scene_index = cli_cmd.value;

                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    g_state.scene_index = scene_index;
                    xSemaphoreGive(g_state_mutex);

                    /* Send immediately */
                    build_scene_command(&scenes[scene_index], &cmd);
                    xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);
                }
                else
                {
                    cli_print("  (scene select ignored in FAST mode)\r\n> ");
                }
            }
        }

        /* --- Run current mode --- */
        if (speed == SPEED_FAST)
        {
            sweep_state_tick(&sweep, &cmd);
            xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(SWEEP_STEP_MS));
        }
        else
        {
            uint32_t delay_ms = (speed == SPEED_NORMAL) ? 1000 : 2000;

            build_scene_command(&scenes[scene_index], &cmd);
            xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);

            scene_index = (scene_index + 1) % SCENE_COUNT;

            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_state.scene_index = scene_index;
            xSemaphoreGive(g_state_mutex);

            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
}

/* =========================
 * CLI Task
 * ========================= */
static void cli_task(void *pvParameters)
{
    (void) pvParameters;

    char line_buf[CLI_LINE_BUF_LEN];
    int  line_pos = 0;

    /* Banner */
    cli_print("\r\n");
    cli_print("  *** PicoSpot 20 DMX Controller ***\r\n");
    cli_print("  PSOC6 / ModusToolbox  |  FreeRTOS\r\n");
    cli_print("  Type 'help' for commands.\r\n\r\n");
    cli_print("> ");

    for (;;)
    {
        char c = cli_getchar();

        /* Echo the character */
        size_t one = 1;
        cyhal_uart_write(&cli_uart_obj, (uint8_t *)&c, &one);

        if (c == '\r' || c == '\n')
        {
            if (line_pos > 0)
            {
                line_buf[line_pos] = '\0';
                cli_print("\r\n");
                cli_process_line(line_buf);
                line_pos = 0;
            }
            cli_print("> ");
        }
        else if (c == '\b' || c == 0x7F) /* Backspace */
        {
            if (line_pos > 0)
            {
                line_pos--;
                /* Erase character on terminal */
                cli_print("\b \b");
            }
        }
        else if (line_pos < CLI_LINE_BUF_LEN - 1)
        {
            line_buf[line_pos++] = c;
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
    if (result != CY_RSLT_SUCCESS) handle_error();

    __enable_irq();

    if (dmx_uart_init() != CY_RSLT_SUCCESS) handle_error();
    if (cli_uart_init() != CY_RSLT_SUCCESS) handle_error();

    dmx_timer_init();
    button_init();

    /* Shared state */
    memset(&g_state, 0, sizeof(g_state));
    g_state_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == NULL) handle_error();

    /* Queues */
    dmxCommandQueue = xQueueCreate(5, sizeof(dmx_command_t));
    if (dmxCommandQueue == NULL) handle_error();

    speedEventQueue = xQueueCreate(1, sizeof(uint8_t));
    if (speedEventQueue == NULL) handle_error();

    cliCmdQueue = xQueueCreate(1, sizeof(cli_task_cmd_t));
    if (cliCmdQueue == NULL) handle_error();

    /* Tasks */
    xTaskCreate(dmx_task,   "DMX",   1024, NULL, 2, NULL);
    xTaskCreate(scene_task, "Scene", 1024, NULL, 3, NULL);
    xTaskCreate(cli_task,   "CLI",   1024, NULL, 1, NULL);
    xTaskCreate(audio_task, "Audio", 2048, NULL, 2, NULL);

    vTaskStartScheduler();

    for (;;) {}
}