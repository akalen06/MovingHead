#include "lighting_task.h"
#include "app_types.h"
#include "dmx_task.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

extern QueueHandle_t controlQueue;

#define COLOR_RED           16
#define COLOR_BLUE          60
#define COLOR_CYAN          71
#define COLOR_PURPLE        82

#define GOBO_OPEN           0
#define GOBO_3              52
#define GOBO1_SHAKE         140
#define GOBO3_SHAKE         170

#define SHUTTER_OPEN        0
#define STROBE_MEDIUM       120
#define STROBE_FAST         220

static const scene_t scenes[] =
{
    {128, 128, 0, 0, 10, COLOR_CYAN,   GOBO_OPEN,   220, SHUTTER_OPEN},
    {150, 150, 0, 0, 80, COLOR_RED,    GOBO1_SHAKE, 255, STROBE_FAST},
    { 60, 170, 0, 0, 20, COLOR_BLUE,   GOBO_3,      230, SHUTTER_OPEN},
    {200,  70, 0, 0,100, COLOR_PURPLE, GOBO3_SHAKE, 255, STROBE_MEDIUM}
};

void lighting_task(void *pvParameters)
{
    (void) pvParameters;

    dmx_command_t cmd;
    control_state_t ctrl;
    uint8_t scene_index = 0;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.active_mode = EQ_MODE_RUSTIG;
    ctrl.volume_threshold = 1500;
    ctrl.blackout = false;

    for (;;)
    {
        xQueuePeek(controlQueue, &ctrl, 0);

        if (ctrl.blackout)
        {
            cmd.address = PICOSPOT_ADDRESS;
            memset(cmd.channel_values, 0, sizeof(cmd.channel_values));
            xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (ctrl.active_mode == EQ_MODE_RUSTIG)
        {
            static const uint8_t rustig_scenes[] = {0, 2};
            static uint8_t rustig_idx = 0;
            scene_index = rustig_scenes[rustig_idx];
            rustig_idx = (rustig_idx + 1u) % 2u;
        }
        else
        {
            static const uint8_t agressief_scenes[] = {1, 3};
            static uint8_t agressief_idx = 0;
            scene_index = agressief_scenes[agressief_idx];
            agressief_idx = (agressief_idx + 1u) % 2u;
        }

        const scene_t *sc = &scenes[scene_index];

        cmd.address = PICOSPOT_ADDRESS;
        cmd.channel_values[0] = sc->pan;
        cmd.channel_values[1] = sc->tilt;
        cmd.channel_values[2] = sc->pan_fine;
        cmd.channel_values[3] = sc->tilt_fine;
        cmd.channel_values[4] = sc->move_speed;
        cmd.channel_values[5] = sc->color;
        cmd.channel_values[6] = sc->gobo;
        cmd.channel_values[7] = sc->dimmer;
        cmd.channel_values[8] = sc->shutter;

        xQueueSend(dmxCommandQueue, &cmd, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}