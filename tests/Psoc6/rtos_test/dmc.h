#ifndef DMX_H
#define DMX_H

#include "FreeRTOS.h"
#include "queue.h"

typedef struct {
    uint16_t address;
    uint8_t channel_values[9];
} dmx_command_t;

extern QueueHandle_t dmxCommandQueue;

#endif