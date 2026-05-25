#include "cyhal.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

#include "audio.h"   /* pulls in shared.h → cli_state_t, speed_state_t,
                        cli_task_cmd_t, g_state, g_state_mutex, cliCmdQueue */

/* =================================================
 * audio.c  —  PDM beat detection + BPM → mode mapping
 *
 * On every detected beat:
 *   1. Measure interval since last beat → compute BPM
 *   2. Average BPM over a short window (smoothing)
 *   3. Map to SPEED_SLOW / SPEED_NORMAL / SPEED_FAST
 *   4. Post CLI_CMD_SET_MODE to cliCmdQueue
 *   5. Update g_state.bpm for the CLI status command
 * ================================================= */

/* =========================
 * Audio config
 * ========================= */
#define FRAME_SIZE          256
#define SAMPLE_RATE_HZ      16000
#define DECIMATION_RATE     64
#define AUDIO_SYS_CLOCK_HZ  24576000

/* Beat detector tuning */
#define REFRACTORY_MS       200     /* minimum ms between beats        */
#define THRESHOLD_FACTOR    1.5f    /* envelope must exceed avg * this  */

/* BPM estimator */
#define BPM_WINDOW          4       /* number of intervals to average   */
#define BPM_TIMEOUT_MS      3000    /* reset BPM if no beat for 3 s     */

/* BPM thresholds for mode selection */
#define BPM_SLOW_MAX        80
#define BPM_NORMAL_MAX      120
/* above NORMAL_MAX → FAST */

/* PDM pins — CY8CKIT-028-TFT shield
 * CYBSP_A4 = P10_4 = PDM CLK
 * CYBSP_A5 = P10_5 = PDM DATA */
#define PDM_PIN_DATA        P10_5
#define PDM_PIN_CLK         P10_4

/* =========================
 * PDM objects
 * ========================= */
static cyhal_pdm_pcm_t pdm_pcm;
static cyhal_clock_t   audio_clock;
static cyhal_clock_t   pll_clock;

/* =========================
 * Signal processing state
 * ========================= */
static float envelope   = 0.0f;
static float avg_energy = 100000.0f;   /* non-zero start prevents false
                                          triggers during warm-up        */

static const float alpha_env = 0.1f;
static const float alpha_avg = 0.01f;

/* =========================
 * BPM estimator state
 * ========================= */
static uint32_t beat_times[BPM_WINDOW]; /* ring buffer of tick timestamps */
static uint8_t  beat_head  = 0;
static uint8_t  beat_count = 0;

/* =========================
 * Warm-up counter
 * ========================= */
#define WARMUP_FRAMES   50      /* ~0.8 s at 16 kHz / 256 samples */
static uint32_t warm_up_frames = 0;

/* =========================
 * Sync
 * ========================= */
static SemaphoreHandle_t pdmSemaphore;

/* =========================
 * Audio buffer
 * ========================= */
static int16_t audio_frame[FRAME_SIZE];

/* =========================
 * PDM config
 * ========================= */
static const cyhal_pdm_pcm_cfg_t pdm_cfg =
{
    .sample_rate     = SAMPLE_RATE_HZ,
    .decimation_rate = DECIMATION_RATE,
    .mode            = CYHAL_PDM_PCM_MODE_LEFT, /* mono mic, L/R pin → GND */
    .word_length     = 16,
    .left_gain       = 10,                      /* +10 dB                  */
    .right_gain      = 10,
};

/* =========================
 * RGB LED helpers
 * (active-low on this kit)
 * ========================= */
static void leds_init(void)
{
    cyhal_gpio_init(CYBSP_LED_RGB_RED,   CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
    cyhal_gpio_init(CYBSP_LED_RGB_GREEN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
    cyhal_gpio_init(CYBSP_LED_RGB_BLUE,  CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    cyhal_gpio_write(CYBSP_LED_RGB_RED,   r);
    cyhal_gpio_write(CYBSP_LED_RGB_GREEN, g);
    cyhal_gpio_write(CYBSP_LED_RGB_BLUE,  b);
}

/* =========================
 * PDM ISR
 * ========================= */
static void pdm_isr(void *arg, cyhal_pdm_pcm_event_t event)
{
    (void)arg;
    (void)event;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(pdmSemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* =========================
 * Clock init
 * ========================= */
static void clock_init(void)
{
    cyhal_clock_reserve(&pll_clock, &CYHAL_CLOCK_PLL[0]);
    cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    cyhal_clock_set_enabled(&pll_clock, true, true);

    cyhal_clock_reserve(&audio_clock, &CYHAL_CLOCK_HF[1]);
    cyhal_clock_set_source(&audio_clock, &pll_clock);
    cyhal_clock_set_enabled(&audio_clock, true, true);
}

/* =========================
 * BPM estimation
 *
 * Stores the last BPM_WINDOW beat timestamps in a ring buffer.
 * BPM = 60000 / average_interval_ms across the window.
 * Returns 0 if fewer than 2 beats have been seen yet.
 * ========================= */
static uint32_t estimate_bpm(uint32_t now_ms)
{
    /* Store this beat */
    beat_times[beat_head] = now_ms;
    beat_head = (beat_head + 1) % BPM_WINDOW;
    if (beat_count < BPM_WINDOW)
        beat_count++;

    if (beat_count < 2)
        return 0; /* not enough data yet */

    /* Sum intervals across the filled window */
    uint32_t total_ms = 0;
    uint8_t  samples  = beat_count - 1;

    /* Walk backwards through the ring buffer */
    for (uint8_t i = 0; i < samples; i++)
    {
        uint8_t newer = (beat_head - 1 - i + BPM_WINDOW) % BPM_WINDOW;
        uint8_t older = (newer     - 1     + BPM_WINDOW) % BPM_WINDOW;
        total_ms += beat_times[newer] - beat_times[older];
    }

    uint32_t avg_interval_ms = total_ms / samples;
    if (avg_interval_ms == 0) return 0;

    return 60000UL / avg_interval_ms;
}

/* =========================
 * Map BPM → speed mode
 * ========================= */
static speed_state_t bpm_to_speed(uint32_t bpm)
{
    if (bpm < BPM_SLOW_MAX)   return SPEED_SLOW;
    if (bpm < BPM_NORMAL_MAX) return SPEED_NORMAL;
    return SPEED_FAST;
}

/* =========================
 * Post mode change to scene task
 * ========================= */
static void post_mode(speed_state_t speed)
{
    cli_task_cmd_t cmd = {
        .type  = CLI_CMD_SET_MODE,
        .value = (uint32_t)speed
    };
    xQueueOverwrite(cliCmdQueue, &cmd);
}

/* =========================
 * Audio task
 * ========================= */
void audio_task(void *arg)
{
    (void)arg;

    pdmSemaphore = xSemaphoreCreateBinary();

    clock_init();
    leds_init();

    /* Startup blink so you know audio task is alive */
    set_rgb(0,1,1); vTaskDelay(pdMS_TO_TICKS(300));
    set_rgb(1,0,1); vTaskDelay(pdMS_TO_TICKS(300));
    set_rgb(1,1,0); vTaskDelay(pdMS_TO_TICKS(300));
    set_rgb(1,1,1); /* all off */

    /* --- PDM init with error reporting --- */
    cy_rslt_t res;

    res = cyhal_pdm_pcm_init(&pdm_pcm, PDM_PIN_DATA, PDM_PIN_CLK,
                             &audio_clock, &pdm_cfg);
    cli_print("\r\n[AUDIO] pdm_init:       0x%08lX%s\r\n",
              (uint32_t)res, res == CY_RSLT_SUCCESS ? " OK" : " FAIL");

    cyhal_pdm_pcm_register_callback(&pdm_pcm, pdm_isr, NULL);
    cyhal_pdm_pcm_enable_event(&pdm_pcm,
        CYHAL_PDM_PCM_ASYNC_COMPLETE,
        CYHAL_ISR_PRIORITY_DEFAULT,
        true);

    res = cyhal_pdm_pcm_start(&pdm_pcm);
    cli_print("[AUDIO] pdm_start:      0x%08lX%s\r\n",
              (uint32_t)res, res == CY_RSLT_SUCCESS ? " OK" : " FAIL");

    res = cyhal_pdm_pcm_read_async(&pdm_pcm, audio_frame, FRAME_SIZE);
    cli_print("[AUDIO] pdm_read_async: 0x%08lX%s\r\n",
              (uint32_t)res, res == CY_RSLT_SUCCESS ? " OK" : " FAIL");

    uint32_t last_beat_tick   = 0;
    uint32_t last_bpm         = 0;
    uint32_t beat_total       = 0;
    uint32_t last_status_tick = 0;

    for (;;)
    {
        if (xSemaphoreTake(pdmSemaphore, portMAX_DELAY) != pdTRUE)
            continue;

        /* --- Energy calculation --- */
        float energy = 0.0f;
        for (uint32_t i = 0; i < FRAME_SIZE; i++)
        {
            float s = (float)audio_frame[i];
            energy += s * s;
        }
        energy /= FRAME_SIZE;

        envelope   = alpha_env * energy + (1.0f - alpha_env) * envelope;
        avg_energy = alpha_avg * energy + (1.0f - alpha_avg) * avg_energy;

        /* Skip beat detection during warm-up so avg_energy can settle */
        warm_up_frames++;
        if (warm_up_frames < WARMUP_FRAMES)
        {
            cyhal_pdm_pcm_read_async(&pdm_pcm, audio_frame, FRAME_SIZE);
            continue;
        }

        float threshold = avg_energy * THRESHOLD_FACTOR;

        uint32_t now = xTaskGetTickCount();

        /* --- Beat detection --- */
        bool beat_this_frame = false;
        if ((envelope > threshold) &&
            (now - last_beat_tick > pdMS_TO_TICKS(REFRACTORY_MS)))
        {
            last_beat_tick   = now;
            beat_this_frame  = true;
            beat_total++;

            uint32_t now_ms = now * portTICK_PERIOD_MS;
            uint32_t bpm    = estimate_bpm(now_ms);

            if (bpm > 0)
            {
                last_bpm = bpm;

                /* Update shared state for CLI status */
                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                g_state.bpm = bpm;
                xSemaphoreGive(g_state_mutex);

                /* Drive scene task mode */
                speed_state_t speed = bpm_to_speed(bpm);
                post_mode(speed);

                /* Debug LED: color tracks mode */
                switch (speed)
                {
                    case SPEED_SLOW:   set_rgb(0,1,1); break; /* red   */
                    case SPEED_NORMAL: set_rgb(1,0,1); break; /* green */
                    case SPEED_FAST:   set_rgb(1,1,0); break; /* blue  */
                    default: break;
                }

                /* Live status line on every beat */
                cli_print("\r[BEAT] BPM: %3lu | Mode: %-6s | Beats: %4lu | Env: %6.0f  ",
                          last_bpm,
                          last_bpm < BPM_SLOW_MAX   ? "SLOW" :
                          last_bpm < BPM_NORMAL_MAX ? "NORMAL" : "FAST",
                          beat_total,
                          envelope);
            }
        }

        /* Reset BPM display if no beat for BPM_TIMEOUT_MS */
        if (last_bpm > 0 &&
            (now - last_beat_tick > pdMS_TO_TICKS(BPM_TIMEOUT_MS)))
        {
            last_bpm   = 0;
            beat_count = 0;
            beat_head  = 0;
            memset(beat_times, 0, sizeof(beat_times));

            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_state.bpm = 0;
            xSemaphoreGive(g_state_mutex);

            cli_print("\r[AUDIO] listening...  env: %6.0f  threshold: %6.0f          ",
                      envelope, threshold);
        }

        /* Heartbeat status every 500 ms when no beat is firing */
        if (!beat_this_frame &&
            (now - last_status_tick > pdMS_TO_TICKS(500)))
        {
            last_status_tick = now;
            if (last_bpm == 0)
            {
                cli_print("\r[AUDIO] listening...  env: %6.0f  threshold: %6.0f          ",
                          envelope, threshold);
            }
        }

        cyhal_pdm_pcm_read_async(&pdm_pcm, audio_frame, FRAME_SIZE);
    }
}