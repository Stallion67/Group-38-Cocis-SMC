#include "entrance/core.h"

#include <stdio.h>
#include <string.h>

/*
 * Rules applied to every scan, in this order (first match wins):
 *   1. reader locked after repeated invalid cards -> deny
 *   2. emergency mode                              -> grant (doors released)
 *   3. unknown card                                -> deny
 *   4. blocked card                                -> deny
 *   5. clock unknown (students and staff)          -> deny
 *   6. outside hours (students and staff)          -> deny entry, hold exit
 *   7. anti-passback: entry while inside / exit while outside -> deny
 *   8. otherwise                                   -> grant
 * Security and admin cards pass rules 5 and 6 (patrols, call-outs).
 */

void ent_init(ent_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->schedule.open_min = 8 * 60;
    s->schedule.close_min = 18 * 60;
    s->schedule.days_mask = 0x7E;
    s->schedule.version = 1;
}

void ent_load_demo_users(ent_state_t *s)
{
    s->user_count = 0;
    s->inside_count = 0;
    /* UIDs match the Wokwi MFRC522 card presets (see docs/INTERFACES.md). */
    ent_add_user(s, "A1B2C3D4", ROLE_STAFF, "Dr Nakato Sarah", false);       /* Blue card, uid attr */
    ent_add_user(s, "11223344", ROLE_STUDENT, "Okello John", false);         /* Green card  */
    ent_add_user(s, "55667788", ROLE_STUDENT, "Achieng Grace", false);       /* Yellow card */
    ent_add_user(s, "04112233445566", ROLE_SECURITY, "Mugisha Peter", false); /* NFC tag, 7-byte */
    ent_add_user(s, "C0FFEE99", ROLE_STUDENT, "Blocked Student", true);      /* Key fob     */
    /* Red card AABBCCDD is left unregistered on purpose. */
}

int ent_find(const ent_state_t *s, const char *uid)
{
    for (int i = 0; i < s->user_count; i++)
        if (strcmp(s->users[i].uid, uid) == 0) return i;
    return -1;
}

bool ent_add_user(ent_state_t *s, const char *uid, scms_role_t role, const char *name, bool blocked)
{
    char norm[SCMS_UID_HEX_MAX];
    if (!scms_uid_normalise(uid, norm) || role < ROLE_STUDENT || role > ROLE_ADMIN) return false;
    if (s->user_count >= SCMS_MAX_USERS || ent_find(s, norm) >= 0) return false;
    scms_user_t *u = &s->users[s->user_count++];
    memset(u, 0, sizeof *u);
    snprintf(u->uid, sizeof u->uid, "%s", norm);
    snprintf(u->name, sizeof u->name, "%s", name ? name : "");
    u->role = role;
    u->blocked = blocked;
    return true;
}

bool ent_remove_user(ent_state_t *s, const char *uid)
{
    int i = ent_find(s, uid);
    if (i < 0 || s->users[i].inside) return false;
    memmove(&s->users[i], &s->users[i + 1], sizeof(scms_user_t) * (size_t)(s->user_count - i - 1));
    s->user_count--;
    return true;
}

bool ent_set_blocked(ent_state_t *s, const char *uid, bool blocked)
{
    int i = ent_find(s, uid);
    if (i < 0) return false;
    s->users[i].blocked = blocked;
    return true;
}

bool ent_schedule_valid(const scms_schedule_t *sch)
{
    return sch->open_min < 1440 && sch->close_min < 1440 && sch->open_min != sch->close_min &&
           (sch->days_mask & 0x7F) != 0;
}

bool ent_schedule_is_open(const scms_schedule_t *sch, int wday, int minute)
{
    if (!ent_schedule_valid(sch) || wday < 0 || wday > 6) return false;
    if (sch->open_min < sch->close_min) {
        return (sch->days_mask & (1u << wday)) && minute >= sch->open_min && minute < sch->close_min;
    }
    /* Overnight window, e.g. 18:00 to 02:00: the part after midnight
       belongs to the previous day's opening. */
    if (minute >= sch->open_min) return sch->days_mask & (1u << wday);
    if (minute < sch->close_min) return sch->days_mask & (1u << ((wday + 6) % 7));
    return false;
}

bool ent_reader_locked(const ent_state_t *s, scms_point_t p, int64_t now_ms)
{
    return now_ms < s->lock_until_ms[p];
}

static bool register_failure(ent_state_t *s, scms_point_t p, const ent_ctx_t *c)
{
    int limit = c->fail_limit > 0 && c->fail_limit <= 8 ? c->fail_limit : 3;
    s->fail_ms[p][s->fail_idx[p] % limit] = c->now_ms > 0 ? c->now_ms : 1;
    s->fail_idx[p] = (s->fail_idx[p] + 1) % limit;
    int recent = 0;
    for (int i = 0; i < limit; i++)
        if (s->fail_ms[p][i] && c->now_ms - s->fail_ms[p][i] <= c->fail_window_ms) recent++;
    if (recent < limit) return false;
    s->lock_until_ms[p] = c->now_ms + c->lock_ms;
    memset(s->fail_ms[p], 0, sizeof s->fail_ms[p]);
    return true;
}

ent_decision_t ent_process_scan(ent_state_t *s, scms_point_t p, const char *uid, const ent_ctx_t *c)
{
    ent_decision_t d = { .result = ACC_DENIED_UNKNOWN, .user = ent_find(s, uid) };
    scms_user_t *u = d.user >= 0 ? &s->users[d.user] : NULL;
    bool privileged = u && u->role >= ROLE_SECURITY;
    bool open = c->clock_valid && ent_schedule_is_open(&s->schedule, c->wday, c->minute);

    if (ent_reader_locked(s, p, c->now_ms)) d.result = ACC_DENIED_READER_LOCKED;
    else if (c->emergency) d.result = ACC_GRANTED_EMERGENCY;
    else if (!u) d.result = ACC_DENIED_UNKNOWN;
    else if (u->blocked) d.result = ACC_DENIED_BLOCKED;
    else if (!c->clock_valid && !privileged) d.result = ACC_DENIED_NO_CLOCK;
    else if (!open && !privileged)
        d.result = p == POINT_ENTRY ? ACC_DENIED_OUTSIDE_HOURS : ACC_DENIED_EXIT_AFTER_HOURS;
    else if (p == POINT_ENTRY && u->inside) d.result = ACC_DENIED_ALREADY_INSIDE;
    else if (p == POINT_EXIT && !u->inside) d.result = ACC_DENIED_NOT_INSIDE;
    else d.result = open ? ACC_GRANTED : ACC_GRANTED_PRIVILEGED;

    if (scms_result_granted(d.result)) {
        s->granted++;
        /* In an emergency an unknown card still opens the door, but the
           roll-call only changes for registered people. */
        if (u && p == POINT_ENTRY && !u->inside) {
            u->inside = true;
            u->since = c->wall_s;
            s->inside_count++;
        } else if (u && p == POINT_EXIT && u->inside) {
            u->inside = false;
            s->inside_count--;
        }
        if ((uint32_t)s->inside_count > s->peak_inside) s->peak_inside = (uint32_t)s->inside_count;
    } else {
        s->denied++;
        /* Only invalid cards (unknown or blocked) count toward the failed-scan
           alarm. Hours, clock and anti-passback denials have their own alerts. */
        if (d.result == ACC_DENIED_UNKNOWN || d.result == ACC_DENIED_BLOCKED)
            d.lockout_tripped = register_failure(s, p, c);
        d.after_hours_inside_alert = d.result == ACC_DENIED_EXIT_AFTER_HOURS;
    }
    d.inside_after = s->inside_count;
    return d;
}

bool ent_release(ent_state_t *s, const char *uid)
{
    int i = ent_find(s, uid);
    if (i < 0 || !s->users[i].inside) return false;
    s->users[i].inside = false;
    s->inside_count--;
    return true;
}

void ent_clear_inside(ent_state_t *s)
{
    for (int i = 0; i < s->user_count; i++) s->users[i].inside = false;
    s->inside_count = 0;
}

void ent_clear_lockouts(ent_state_t *s)
{
    memset(s->lock_until_ms, 0, sizeof s->lock_until_ms);
    memset(s->fail_ms, 0, sizeof s->fail_ms);
}
