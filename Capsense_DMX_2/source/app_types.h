#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define PICOSPOT_ADDRESS    1
#define PICOSPOT_CHANNELS   9

typedef enum
{
    EQ_MODE_RUSTIG = 0,
    EQ_MODE_AGRESSIEF
} eq_mode_t;

typedef struct
{
    eq_mode_t active_mode;
    uint16_t volume_threshold;
    bool blackout;
} control_state_t;

typedef struct
{
    uint16_t address;
    uint8_t channel_values[PICOSPOT_CHANNELS];
} dmx_command_t;

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

#endif