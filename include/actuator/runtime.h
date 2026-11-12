#ifndef ACTUATOR_RUNTIME_H
#define ACTUATOR_RUNTIME_H

/*
 * Entrance outputs: two door-lock servos and a buzzer.
 * Each door has its own one-shot timer, so the entry door closing never
 * waits for the exit door, the SD card or the network.
 * Emergency mode holds both doors open until it is cleared.
 */
#include <stdbool.h>
#include "esp_err.h"
#include "common/system.h"

typedef enum { BEEP_OK = 1, BEEP_DENY, BEEP_ALERT, BEEP_KEY } actuator_beep_t;

esp_err_t actuator_start(void);
void actuator_open_door(scms_point_t door);
void actuator_close_all(void);
void actuator_emergency(bool on);
bool actuator_door_open(scms_point_t door);
void actuator_beep(actuator_beep_t kind);
/* Continuous siren until cleared (reader lockout, intrusion). */
void actuator_alarm(bool on);
bool actuator_alarm_active(void);

#endif
