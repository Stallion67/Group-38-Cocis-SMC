/* Host tests for the access decision engine (components/entrance/core.c). */
#include <string.h>
#include "check.h"
#include "common/config.h"
#include "entrance/core.h"

static ent_ctx_t ctx(int wday, int minute, int64_t now_ms)
{
    ent_ctx_t c = {
        .clock_valid = true, .wday = wday, .minute = minute, .now_ms = now_ms, .wall_s = 1800000000,
        .fail_limit = FAIL_LIMIT, .fail_window_ms = FAIL_WINDOW_MS, .lock_ms = READER_LOCK_MS,
    };
    return c;
}

#define WED 3
#define SUN 0
#define AT(h, m) ((h) * 60 + (m))

static void test_schedule(void)
{
    scms_schedule_t s = { AT(8, 0), AT(18, 0), 0x7E, 1 };   /* Mon-Sat */
    CHECK(ent_schedule_valid(&s));
    CHECK(ent_schedule_is_open(&s, WED, AT(8, 0)));
    CHECK(ent_schedule_is_open(&s, WED, AT(17, 59)));
    CHECK(!ent_schedule_is_open(&s, WED, AT(18, 0)));
    CHECK(!ent_schedule_is_open(&s, WED, AT(7, 59)));
    CHECK(!ent_schedule_is_open(&s, SUN, AT(10, 0)));          /* Sunday closed */

    scms_schedule_t night = { AT(18, 0), AT(2, 0), 0x08, 1 };  /* Wednesday night only */
    CHECK(ent_schedule_is_open(&night, WED, AT(23, 0)));
    CHECK(ent_schedule_is_open(&night, 4, AT(1, 30)));         /* Thursday 01:30 belongs to Wednesday */
    CHECK(!ent_schedule_is_open(&night, WED, AT(1, 30)));      /* Tuesday night not allowed */
    CHECK(!ent_schedule_is_open(&night, 4, AT(2, 0)));

    scms_schedule_t bad = { AT(9, 0), AT(9, 0), 0x7E, 1 };
    CHECK(!ent_schedule_valid(&bad));
    bad.close_min = AT(10, 0);
    bad.days_mask = 0;
    CHECK(!ent_schedule_valid(&bad));
}

static void test_entry_exit_and_antipassback(void)
{
    ent_state_t s;
    ent_init(&s);
    ent_load_demo_users(&s);
    ent_ctx_t c = ctx(WED, AT(10, 0), 1000);

    ent_decision_t d = ent_process_scan(&s, POINT_ENTRY, "11223344", &c);
    CHECK(d.result == ACC_GRANTED && d.inside_after == 1);
    d = ent_process_scan(&s, POINT_ENTRY, "11223344", &c);
    CHECK(d.result == ACC_DENIED_ALREADY_INSIDE && d.inside_after == 1);
    d = ent_process_scan(&s, POINT_EXIT, "55667788", &c);     /* never entered */
    CHECK(d.result == ACC_DENIED_NOT_INSIDE);
    d = ent_process_scan(&s, POINT_EXIT, "11223344", &c);
    CHECK(d.result == ACC_GRANTED && d.inside_after == 0);
    CHECK(s.granted == 2 && s.denied == 2 && s.peak_inside == 1);
}

static void test_hours_and_roles(void)
{
    ent_state_t s;
    ent_init(&s);
    ent_load_demo_users(&s);
    ent_ctx_t day = ctx(WED, AT(10, 0), 1000), night = ctx(WED, AT(20, 0), 2000);

    CHECK(ent_process_scan(&s, POINT_ENTRY, "11223344", &night).result == ACC_DENIED_OUTSIDE_HOURS);
    CHECK(ent_process_scan(&s, POINT_ENTRY, "11223344", &day).result == ACC_GRANTED);
    /* still inside after closing: exit is held, alert raised */
    ent_decision_t d = ent_process_scan(&s, POINT_EXIT, "11223344", &night);
    CHECK(d.result == ACC_DENIED_EXIT_AFTER_HOURS && d.after_hours_inside_alert && d.inside_after == 1);
    /* security officer works at any hour */
    CHECK(ent_process_scan(&s, POINT_ENTRY, "04112233445566", &night).result == ACC_GRANTED_PRIVILEGED);
    /* security releases the student by hand */
    CHECK(ent_release(&s, "11223344") && s.inside_count == 1);
    CHECK(!ent_release(&s, "11223344"));

    /* no trusted clock: students refused, security still allowed */
    ent_state_t s2;
    ent_init(&s2);
    ent_load_demo_users(&s2);
    ent_ctx_t noclk = ctx(WED, AT(10, 0), 1000);
    noclk.clock_valid = false;
    CHECK(ent_process_scan(&s2, POINT_ENTRY, "11223344", &noclk).result == ACC_DENIED_NO_CLOCK);
    CHECK(ent_process_scan(&s2, POINT_ENTRY, "04112233445566", &noclk).result == ACC_GRANTED_PRIVILEGED);
}

static void test_failed_scan_lockout(void)
{
    ent_state_t s;
    ent_init(&s);
    ent_load_demo_users(&s);
    ent_ctx_t c = ctx(WED, AT(10, 0), 10000);

    CHECK(!ent_process_scan(&s, POINT_ENTRY, "AABBCCDD", &c).lockout_tripped);   /* unknown */
    c.now_ms += 2000;
    CHECK(!ent_process_scan(&s, POINT_ENTRY, "C0FFEE99", &c).lockout_tripped);   /* blocked */
    c.now_ms += 2000;
    ent_decision_t d = ent_process_scan(&s, POINT_ENTRY, "AABBCCDD", &c);
    CHECK(d.lockout_tripped);
    c.now_ms += 1000;
    CHECK(ent_process_scan(&s, POINT_ENTRY, "11223344", &c).result == ACC_DENIED_READER_LOCKED);
    CHECK(ent_process_scan(&s, POINT_EXIT, "11223344", &c).result == ACC_DENIED_NOT_INSIDE);  /* other reader free */
    c.now_ms += READER_LOCK_MS;
    CHECK(ent_process_scan(&s, POINT_ENTRY, "11223344", &c).result == ACC_GRANTED);

    /* failures spread over more than the window do not trip */
    ent_state_t s2;
    ent_init(&s2);
    ent_load_demo_users(&s2);
    ent_ctx_t c2 = ctx(WED, AT(10, 0), 10000);
    for (int i = 0; i < 5; i++) {
        CHECK(!ent_process_scan(&s2, POINT_ENTRY, "AABBCCDD", &c2).lockout_tripped);
        c2.now_ms += FAIL_WINDOW_MS / 2 + 1000;
    }
    /* anti-passback denials have their own alert and never lock the reader */
    ent_state_t s3;
    ent_init(&s3);
    ent_load_demo_users(&s3);
    ent_ctx_t c3 = ctx(WED, AT(10, 0), 10000);
    ent_process_scan(&s3, POINT_ENTRY, "11223344", &c3);
    for (int i = 0; i < 4; i++) {
        c3.now_ms += 1000;
        CHECK(!ent_process_scan(&s3, POINT_ENTRY, "11223344", &c3).lockout_tripped);
    }
}

static void test_emergency_and_roster(void)
{
    ent_state_t s;
    ent_init(&s);
    ent_load_demo_users(&s);
    ent_ctx_t c = ctx(SUN, AT(3, 0), 1000);
    c.emergency = true;
    CHECK(ent_process_scan(&s, POINT_ENTRY, "AABBCCDD", &c).result == ACC_GRANTED_EMERGENCY);
    CHECK(s.inside_count == 0);                       /* unknown card: roll-call unchanged */
    CHECK(ent_process_scan(&s, POINT_ENTRY, "C0FFEE99", &c).result == ACC_GRANTED_EMERGENCY);

    CHECK(ent_add_user(&s, "de:ad:be:ef", ROLE_STAFF, "New Lecturer", false));
    CHECK(ent_find(&s, "DEADBEEF") >= 0);
    CHECK(!ent_add_user(&s, "DEADBEEF", ROLE_STAFF, "Duplicate", false));
    CHECK(!ent_add_user(&s, "123", ROLE_STAFF, "Too short", false));
    CHECK(ent_set_blocked(&s, "DEADBEEF", true));
    c.emergency = false;
    c.wday = WED;
    c.minute = AT(10, 0);
    CHECK(ent_process_scan(&s, POINT_ENTRY, "DEADBEEF", &c).result == ACC_DENIED_BLOCKED);
    CHECK(ent_process_scan(&s, POINT_ENTRY, "A1B2C3D4", &c).result == ACC_GRANTED);
    CHECK(!ent_remove_user(&s, "A1B2C3D4"));          /* still inside */
    CHECK(ent_remove_user(&s, "DEADBEEF"));
    ent_clear_inside(&s);
    CHECK(s.inside_count == 0 && ent_remove_user(&s, "A1B2C3D4"));
}

static void test_helpers(void)
{
    char u[SCMS_UID_HEX_MAX];
    CHECK(scms_uid_normalise("04:11:22:33:44:55:66", u) && !strcmp(u, "04112233445566"));
    CHECK(!scms_uid_normalise("XYZ12345", u));
    CHECK(scms_parse_hhmm("08:30") == 510 && scms_parse_hhmm("0830") == 510);
    CHECK(scms_parse_hhmm("24:00") == -1 && scms_parse_hhmm("8:30") == -1);
    CHECK(scms_role_parse("security") == ROLE_SECURITY && scms_role_parse("boss") == -1);
}

int main(void)
{
    test_schedule();
    test_entry_exit_and_antipassback();
    test_hours_and_roles();
    test_failed_scan_lockout();
    test_emergency_and_roster();
    test_helpers();
    TEST_DONE("entrance");
}
