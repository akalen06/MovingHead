#ifndef SHARED_H
#define SHARED_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <stdarg.h>

#define PICOSPOT_CHANNELS   9

/* =================================================
 * CLI command queue types
 * Used by CLI task, audio task → scene task
 * ================================================= */
typedef enum
{
    CLI_CMD_SET_MODE  = 0,
    CLI_CMD_SET_SCENE = 1,
} cli_cmd_type_t;

typedef struct
{
    cli_cmd_type_t type;
    uint32_t       value;
} cli_task_cmd_t;

/* =================================================
 * Speed states
 * ================================================= */
typedef enum
{
    SPEED_SLOW   = 0,
    SPEED_NORMAL = 1,
    SPEED_FAST   = 2,
    SPEED_STATE_COUNT
} speed_state_t;

/* =================================================
 * Shared runtime state (protected by g_state_mutex)
 * Written by: dmx_task (ch, frame_count)
 *             scene_task (speed, scene_index)
 *             audio_task (bpm)
 * Read by:    cli_task (status command)
 * ================================================= */
typedef struct
{
    uint8_t  speed;
    uint32_t scene_index;
    uint8_t  ch[PICOSPOT_CHANNELS];
    uint32_t frame_count;
    uint32_t bpm;
} cli_state_t;

/* =================================================
 * Extern handles — defined in main.c
 * ================================================= */
extern cli_state_t       g_state;
extern SemaphoreHandle_t g_state_mutex;
extern QueueHandle_t     cliCmdQueue;

/* CLI print function — defined in main.c, usable from any task */
void cli_print(const char *fmt, ...);

#endif /* SHARED_H */