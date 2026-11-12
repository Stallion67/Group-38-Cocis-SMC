#ifndef ROOMS_CORE_H
#define ROOMS_CORE_H

/*
 * Lighting and occupancy rules for one lecture room. Pure C (host tested).
 * Inputs: PIR motion, daylight level, building state from the entrance.
 * Output: light level 0..100 % plus events and energy figures.
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum { ROOM_MODE_AUTO = 0, ROOM_MODE_MANUAL_ON, ROOM_MODE_FORCED_OFF } room_mode_t;

typedef struct {
    int vacant_after_s;
    int grace_s;
    int grace_level;
    int override_s;
    int lux_bright;
    int lux_medium;
    double load_w;
} room_params_t;

typedef struct {
    bool occupied;
    int64_t last_motion_ms;
    int level;                 /* current light level, % */
    room_mode_t mode;
    int64_t override_until_ms;
    int64_t last_meter_ms;
    double wh;                 /* energy used                         */
    double baseline_wh;        /* lights-on-all-opening-hours baseline */
    uint32_t on_s;             /* seconds with lights on               */
} room_core_t;

typedef struct {
    bool pir;
    int lux;
    bool emergency;
    bool building_open;
    int building_inside;       /* people inside, from the entrance   */
    bool inside_known;         /* entrance count is trustworthy      */
    int64_t now_ms;
} room_inputs_t;

typedef struct {
    bool became_occupied;
    bool became_vacant;
    bool level_changed;
    int prev_level;
    bool override_expired;
    bool intrusion;            /* motion while building is empty     */
} room_events_t;

void room_params_default(room_params_t *p);
void room_core_init(room_core_t *r, int64_t now_ms);
int room_daylight_level(int lux, const room_params_t *p);
room_events_t room_core_step(room_core_t *r, const room_inputs_t *in, const room_params_t *p);
void room_core_set_mode(room_core_t *r, room_mode_t mode, int64_t now_ms, const room_params_t *p);
const char *room_mode_name(room_mode_t m);
/* Savings against the baseline, in Wh. */
double room_core_saved_wh(const room_core_t *r);

#endif
