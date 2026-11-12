#ifndef ROOMS_RUNTIME_H
#define ROOMS_RUNTIME_H

/*
 * Terminals 1 and 2: one FreeRTOS task per lecture room.
 * Each task owns its PIR input and its light PWM channel. The daylight
 * sensor (LDR) is shared, so ADC reads are protected by a mutex.
 * Rooms talk to the entrance only through comms/bus.h messages.
 */
#include "esp_err.h"

esp_err_t rooms_start(void);

#endif
