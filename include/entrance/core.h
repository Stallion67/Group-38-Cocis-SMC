#ifndef ENTRANCE_CORE_H
#define ENTRANCE_CORE_H

/*
 * Access decision engine. Pure C with no ESP-IDF calls, so the same code
 * runs on the ESP32 and in the host tests (test/test_entrance.c).
 * The runtime owns one ent_state_t and protects it with a mutex.
 */
#include <stdbool.h>
#include <stdint.h>
#include "common/system.h"

typedef struct {
    scms_user_t users[SCMS_MAX_USERS];
    int user_count;
    int inside_count;
    scms_schedule_t schedule;
    /* failed-scan alert: last FAIL_LIMIT failure times per reader */
    int64_t fail_ms[2][8];
    int fail_idx[2];
    int64_t lock_until_ms[2];
    /* statistics */
    uint32_t granted, denied, peak_inside;
} ent_state_t;

typedef struct {
    bool clock_valid;   /* wall clock trusted (RTC or SNTP)  */
    int wday;           /* 0 = Sunday                         */
    int minute;         /* minutes after midnight, local time */
    bool emergency;
    int64_t now_ms;     /* monotonic time                     */
    int64_t wall_s;     /* wall time for "since" fields       */
    int fail_limit;     /* normally FAIL_LIMIT                */
    int64_t fail_window_ms;
    int64_t lock_ms;
} ent_ctx_t;

typedef struct {
    scms_access_result_t result;
    int user;            /* index in users[], -1 if unknown */
    int inside_after;
    bool lockout_tripped;
    bool after_hours_inside_alert; /* someone tried to leave after hours */
} ent_decision_t;

void ent_init(ent_state_t *s);
void ent_load_demo_users(ent_state_t *s);
int ent_find(const ent_state_t *s, const char *uid);
/* false: duplicate, table full or bad UID. */
bool ent_add_user(ent_state_t *s, const char *uid, scms_role_t role, const char *name, bool blocked);
/* false: unknown UID, or person still inside (protects the roll-call). */
bool ent_remove_user(ent_state_t *s, const char *uid);
bool ent_set_blocked(ent_state_t *s, const char *uid, bool blocked);

bool ent_schedule_valid(const scms_schedule_t *sch);
bool ent_schedule_is_open(const scms_schedule_t *sch, int wday, int minute);

ent_decision_t ent_process_scan(ent_state_t *s, scms_point_t point, const char *uid,
                                const ent_ctx_t *ctx);
/* Security lets a person out by hand (after hours or tailgate). */
bool ent_release(ent_state_t *s, const char *uid);
/* Admin confirms the building is empty after a check. */
void ent_clear_inside(ent_state_t *s);
void ent_clear_lockouts(ent_state_t *s);
bool ent_reader_locked(const ent_state_t *s, scms_point_t p, int64_t now_ms);

#endif
