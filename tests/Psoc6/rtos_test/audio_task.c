#include "cyhal.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdlib.h>
#include <math.h>
#include "semphr.h"
#include "dmx.h"

/* === EXTERN FROM MAIN === */
extern QueueHandle_t dmxCommandQueue;


/* === AUDIO CONFIG === */
#define FRAME_SIZE          256
#define SAMPLE_RATE_HZ      16000
#define DECIMATION_RATE     64
#define AUDIO_SYS_CLOCK_HZ  24576000

#define REFRACTORY_MS       200
#define THRESHOLD_FACTOR    1.5f

#define PICOSPOT_ADDRESS    1

/* === PDM OBJECTS === */
static cyhal_pdm_pcm_t pdm_pcm;
static cyhal_clock_t audio_clock;
static cyhal_clock_t pll_clock;

/* === TIMER === */
static cyhal_timer_t timer;

/* === SIGNAL PROCESSING === */
static float envelope = 0;
static float avg_energy = 0;
static float threshold = 0;

static float alpha_env = 0.1f;
static float alpha_avg = 0.01f;

static uint32_t last_beat_time = 0;

/* === SYNC === */
static SemaphoreHandle_t pdmSemaphore;

/* === AUDIO BUFFER === */
static int16_t audio_frame[FRAME_SIZE];

/* === PDM CONFIG === */
static const cyhal_pdm_pcm_cfg_t pdm_cfg =
{
    .sample_rate     = SAMPLE_RATE_HZ,
    .decimation_rate = DECIMATION_RATE,
    .mode            = CYHAL_PDM_PCM_MODE_STEREO,
    .word_length     = 16,
    .left_gain       = 0,
    .right_gain      = 0
};

/* === ISR === */
static void pdm_isr(void *arg, cyhal_pdm_pcm_event_t event)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(pdmSemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// led inits
static void leds_init(void)
{
    cyhal_gpio_init(CYBSP_LED_RGB_RED,   CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
    cyhal_gpio_init(CYBSP_LED_RGB_GREEN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
    cyhal_gpio_init(CYBSP_LED_RGB_BLUE,  CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 1);
}

// leds rgb set
static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    cyhal_gpio_write(CYBSP_LED_RGB_RED,   r);
    cyhal_gpio_write(CYBSP_LED_RGB_GREEN, g);
    cyhal_gpio_write(CYBSP_LED_RGB_BLUE,  b);
}

/* === CLOCK INIT === */
static void clock_init(void)
{
    cyhal_clock_reserve(&pll_clock, &CYHAL_CLOCK_PLL[0]);
    cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    cyhal_clock_set_enabled(&pll_clock, true, true);

    cyhal_clock_reserve(&audio_clock, &CYHAL_CLOCK_HF[1]);
    cyhal_clock_set_source(&audio_clock, &pll_clock);
    cyhal_clock_set_enabled(&audio_clock, true, true);
}

/* === TIMER INIT === */
static void timer_init(void)
{
    cyhal_timer_cfg_t cfg = {
        .compare_value = 0,
        .period = 0xFFFFFFFF,
        .direction = CYHAL_TIMER_DIR_UP,
        .is_compare = false,
        .is_continuous = true,
        .value = 0
    };

    cyhal_timer_init(&timer, NC, NULL);
    cyhal_timer_configure(&timer, &cfg);
    cyhal_timer_set_frequency(&timer, 1000000); // 1 MHz
    cyhal_timer_start(&timer);
}

/* === SEND DMX COMMAND === */
static void send_beat(void)
{
    dmx_command_t cmd;

    cmd.address = PICOSPOT_ADDRESS;

    uint8_t color = rand() % 3;

    cmd.channel_values[0] = 255; // intensity
   

    switch(color)
    {
        case 0:
            cmd.channel_values[0] = 50;
	        cmd.channel_values[1] = 128;
	        cmd.channel_values[2] = 0;
	        cmd.channel_values[3] = 0;
	        cmd.channel_values[4] = 0;
	        cmd.channel_values[5] = 20;
	        cmd.channel_values[6] = 0;
	        cmd.channel_values[7] = 250;
	        cmd.channel_values[8] = 0;
            break;

        case 1:
            cmd.channel_values[0] = 150;
	        cmd.channel_values[1] = 255;
	        cmd.channel_values[2] = 0;
	        cmd.channel_values[3] = 0;
	        cmd.channel_values[4] = 0;
	        cmd.channel_values[5] = 50;
	        cmd.channel_values[6] = 0;
	        cmd.channel_values[7] = 250;
	        cmd.channel_values[8] = 0;
            break;

        case 2:
            cmd.channel_values[0] = 255;
	        cmd.channel_values[1] = 200;
	        cmd.channel_values[2] = 0;
	        cmd.channel_values[3] = 0;
	        cmd.channel_values[4] = 0;
	        cmd.channel_values[5] = 60;
	        cmd.channel_values[6] = 0;
	        cmd.channel_values[7] = 250;
	        cmd.channel_values[8] = 0;
            break;
    }

    for (int i = 4; i < 9; i++)
        cmd.channel_values[i] = 0;

    xQueueOverwrite(dmxCommandQueue, &cmd);
}

/* === AUDIO TASK === */
void audio_task(void *arg)
{
    (void)arg;

    pdmSemaphore = xSemaphoreCreateBinary();

    clock_init();
    timer_init();
    leds_init();

    cyhal_pdm_pcm_init(&pdm_pcm, P10_5, P10_4, &audio_clock, &pdm_cfg);
    cyhal_pdm_pcm_register_callback(&pdm_pcm, pdm_isr, NULL);
    cyhal_pdm_pcm_enable_event(&pdm_pcm,
        CYHAL_PDM_PCM_ASYNC_COMPLETE,
        CYHAL_ISR_PRIORITY_DEFAULT,
        true);

    cyhal_pdm_pcm_start(&pdm_pcm);
    cyhal_pdm_pcm_read_async(&pdm_pcm, audio_frame, FRAME_SIZE);
    
    set_rgb(0,1,1);
	vTaskDelay(pdMS_TO_TICKS(500));
	set_rgb(1,0,1);
	vTaskDelay(pdMS_TO_TICKS(500));
	set_rgb(1,1,0);
	vTaskDelay(pdMS_TO_TICKS(500));
	set_rgb(1,1,1);

    for (;;)
    {
        if (xSemaphoreTake(pdmSemaphore, portMAX_DELAY))
        {
            float energy = 0;

            for (uint32_t i = 0; i < FRAME_SIZE; i++)
            {
                float s = (float)audio_frame[i];
                energy += s * s;
            }

            energy /= FRAME_SIZE;

            envelope = alpha_env * energy + (1 - alpha_env) * envelope;
            avg_energy = alpha_avg * energy + (1 - alpha_avg) * avg_energy;
            threshold = avg_energy * THRESHOLD_FACTOR;

            uint32_t now = xTaskGetTickCount();

            if ((envelope > threshold) &&
                (now - last_beat_time > pdMS_TO_TICKS(REFRACTORY_MS)))
            {
                last_beat_time = now;
                //kinda broken rn fix later
                send_beat();
                
           	
			    // DEBUG LED
			    uint8_t color;
			    uint8_t prev = color;
			    color = rand() % 4;
			    
			    if(color == prev && color != 0)
			        --color;
			    else if (color == prev)
			        color++;
			
			    switch(color)
			    {
			        case 0: set_rgb(0,1,1); break; // rood
			        case 1: set_rgb(1,0,1); break; // groen
			        case 2: set_rgb(1,1,0); break; // blauw
			        case 3: set_rgb(0,0,0); break; // all
			    }
			}

            cyhal_pdm_pcm_read_async(&pdm_pcm, audio_frame, FRAME_SIZE);
        }
    }
}