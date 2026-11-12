#ifndef ENTRANCE_RUNTIME_H
#define ENTRANCE_RUNTIME_H

/*
 * Terminal 3: main entrance and system coordinator.
 *   rfid task        polls both MFRC522 readers, queues each new card
 *   access task      the ONLY task that changes the roll-call (no races)
 *   coordinator task room messages, heartbeats, schedule changes, LCD
 *   keypad task      4x3 keypad management menu + break-glass button
 * Functions marked (auth) must be called only after the caller checked
 * the operator's role (console, keypad or dashboard).
 */
#include <stdbool.h>
#include <stdint.h>
#include "comms/bus.h"
#include "common/system.h"
#include "esp_err.h"

typedef struct {
    bool clock_valid;
    char clock_source[10];
    char now[24];
    bool open;
    bool emergency;
    bool alarm;
    bool inside_known;
    int inside;
    scms_schedule_t schedule;
    bool reader_ready[2];
    bool reader_locked[2];
    bool door_open[2];
    uint32_t granted, denied, peak, alerts;
    bool sd_ok;
    bool lcd_ok;
    bool rtc_ok;
} entrance_status_t;

typedef struct {
    bool seen;
    bool online;
    int64_t age_ms;
    bus_room_state_t state;
    uint32_t last_seq;
    uint32_t lost_events;
} entrance_room_view_t;

esp_err_t entrance_start(void);

void entrance_get_status(entrance_status_t *out);
void entrance_get_room(int room, entrance_room_view_t *out);
int entrance_copy_users(scms_user_t *out, int max);

/* Alerts go to the console (red), the SD event log and the LCD. */
void entrance_alert(const char *level, const char *source, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

esp_err_t entrance_set_schedule(uint16_t open_min, uint16_t close_min, int days_mask, const char *by); /* (auth) */
void entrance_set_emergency(bool on, const char *by);                                               /* (auth) */
void entrance_alarm_off(const char *by);                                                            /* (auth) */
bool entrance_release(const char *uid, const char *by);                                             /* (auth) */
void entrance_confirm_empty(const char *by);                                                        /* (auth) */
bool entrance_user_add(const char *uid, scms_role_t role, const char *name, const char *by);        /* (auth) */
bool entrance_user_remove(const char *uid, const char *by);                                         /* (auth) */
bool entrance_user_block(const char *uid, bool blocked, const char *by);                            /* (auth) */
/* mode: room_mode_t or BUS_NO_CHANGE; vacancy_s / pause_s 0 = no change (auth) */
bool entrance_room_command(int room, uint8_t mode, uint16_t vacancy_s, uint16_t pause_s, const char *by);
/* Queues a scan exactly as if a card was tapped (console "tap"). (auth) */
bool entrance_simulate_scan(scms_point_t point, const char *uid);
void entrance_set_time(int64_t epoch_s, const char *by);                                             /* (auth) */

#endif
