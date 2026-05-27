#ifndef AUDIO_H
#define AUDIO_H

#include "shared.h"

/* Task entry point — pass to xTaskCreate */
void audio_task(void *arg);

#endif /* AUDIO_H */