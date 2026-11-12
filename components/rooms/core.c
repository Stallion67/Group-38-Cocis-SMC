#include "rooms/core.h"

#include <string.h>
#include "common/config.h"

void room_params_default(room_params_t *p)
{
    p->vacant_after_s = VACANT_AFTER_S;
    p->grace_s = GRACE_S;
    p->grace_level = GRACE_LEVEL;
    p->override_s = OVERRIDE_S;
    p->lux_bright = LUX_BRIGHT;
    p->lux_medium = LUX_MEDIUM;
    p->load_w = ROOM_LOAD_W;
}

void room_core_init(room_core_t *r, int64_t now_ms)
{
    memset(r, 0, sizeof *r);
    r->last_meter_ms = now_ms;
    r->last_motion_ms = now_ms - 3600000;   /* starts vacant */
}

const char *room_mode_name(room_mode_t m)
{
    return m == ROOM_MODE_MANUAL_ON ? "manual" : m == ROOM_MODE_FORCED_OFF ? "off" : "auto";
}

int room_daylight_level(int lux, const room_params_t *p)
{
    if (lux >= p->lux_bright) return 0;     /* enough daylight */
    if (lux >= p->lux_medium) return 50;    /* top up          */
    return 100;                             /* dark            */
}

void room_core_set_mode(room_core_t *r, room_mode_t mode, int64_t now_ms, const room_params_t *p)
{
    r->mode = mode;
    if (mode == ROOM_MODE_MANUAL_ON) r->override_until_ms = now_ms + (int64_t)p->override_s * 1000;
}

double room_core_saved_wh(const room_core_t *r)
{
    double s = r->baseline_wh - r->wh;
    return s > 0 ? s : 0;
}

room_events_t room_core_step(room_core_t *r, const room_inputs_t *in, const room_params_t *p)
{
    room_events_t ev = { .prev_level = r->level };
    int64_t now = in->now_ms;

    /* 1. manual override expires on its own */
    if (r->mode == ROOM_MODE_MANUAL_ON && now >= r->override_until_ms) {
        r->mode = ROOM_MODE_AUTO;
        ev.override_expired = true;
    }

    /* 2. occupancy. When the entrance reports an empty building with a
          trusted count, motion is not a lecture: raise an intrusion event
          and keep the lights off (unless in emergency). */
    bool building_empty = in->inside_known && in->building_inside == 0 && !in->emergency;
    if (in->pir && building_empty) {
        ev.intrusion = true;
    } else if (in->pir) {
        r->last_motion_ms = now;
        if (!r->occupied) {
            r->occupied = true;
            ev.became_occupied = true;
        }
    }
    int64_t idle_ms = now - r->last_motion_ms;
    int64_t vacant_ms = (int64_t)p->vacant_after_s * 1000;
    int64_t off_ms = vacant_ms + (int64_t)p->grace_s * 1000;
    bool in_grace = r->occupied && idle_ms > vacant_ms && idle_ms <= off_ms;
    bool vacant_due = idle_ms > off_ms;
    /* Building closed (or emptied): no grace, switch off at once. */
    if ((!in->building_open || building_empty) && idle_ms > vacant_ms) {
        vacant_due = true;
        in_grace = false;
    }
    if (r->occupied && vacant_due) {
        r->occupied = false;
        ev.became_vacant = true;
    }

    /* 3. lighting decision, highest priority first */
    int target;
    if (in->emergency) target = 100;
    else if (r->mode == ROOM_MODE_FORCED_OFF) target = 0;
    else if (r->mode == ROOM_MODE_MANUAL_ON) target = 100;
    else if (!r->occupied) target = 0;
    else if (in_grace) target = p->grace_level;
    else target = room_daylight_level(in->lux, p);
    if (target != r->level) {
        r->level = target;
        ev.level_changed = true;
    }

    /* 4. energy metering in whole seconds */
    while (now - r->last_meter_ms >= 1000) {
        r->last_meter_ms += 1000;
        r->wh += p->load_w * r->level / 100.0 / 3600.0;
        if (r->level > 0) r->on_s++;
        if (in->building_open || in->emergency) r->baseline_wh += p->load_w / 3600.0;
    }
    return ev;
}
