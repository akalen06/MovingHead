#ifndef DMX_TASK_H
#define DMX_TASK_H

#include "FreeRTOS.h"
#include "queue.h"
#include "app_types.h"

void dmx_task(void *pvParameters);

extern QueueHandle_t dmxCommandQueue;

#endif