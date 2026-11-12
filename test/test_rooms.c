/* Host tests for the lecture-room lighting rules (components/rooms/core.c). */
#include "check.h"
#include "rooms/core.h"

static room_inputs_t in_at(int64_t t, bool pir, int lux)
{
    room_inputs_t in = { .pir = pir, .lux = lux, .building_open = true, .building_inside = 3,
                         .inside_known = true, .now_ms = t };
    return in;
}

/* runs the controller every 100 ms from t0 to t1 */
static void run(room_core_t *r, const room_params_t *p, int64_t t0, int64_t t1, bool pir, int lux)
{
    for (int64_t t = t0; t <= t1; t += 100) {
        room_inputs_t in = in_at(t, pir, lux);
        room_core_step(r, &in, p);
    }
}

static void test_occupancy_grace_and_off(void)
{
    room_params_t p;
    room_core_t r;
    room_params_default(&p);
    p.vacant_after_s = 30;
    p.grace_s = 10;
    room_core_init(&r, 0);

    room_inputs_t in = in_at(0, true, 50);   /* dark room, motion */
    room_events_t ev = room_core_step(&r, &in, &p);
    CHECK(ev.became_occupied && ev.level_changed && r.level == 100);

    run(&r, &p, 100, 30000, false, 50);       /* 30 s without motion: still full */
    CHECK(r.occupied && r.level == 100);
    run(&r, &p, 30100, 35000, false, 50);     /* grace: dimmed */
    CHECK(r.occupied && r.level == p.grace_level);
    run(&r, &p, 35100, 40200, false, 50);     /* after grace: off */
    CHECK(!r.occupied && r.level == 0);
}

static void test_daylight(void)
{
    room_params_t p;
    room_params_default(&p);
    CHECK(room_daylight_level(800, &p) == 0);
    CHECK(room_daylight_level(300, &p) == 50);
    CHECK(room_daylight_level(50, &p) == 100);

    room_core_t r;
    room_core_init(&r, 0);
    room_inputs_t in = in_at(0, true, 800);   /* sunny room, occupied */
    room_core_step(&r, &in, &p);
    CHECK(r.occupied && r.level == 0);
}

static void test_modes_emergency_and_closing(void)
{
    room_params_t p;
    room_core_t r;
    room_params_default(&p);
    room_core_init(&r, 0);

    room_core_set_mode(&r, ROOM_MODE_MANUAL_ON, 0, &p);
    room_inputs_t in = in_at(100, false, 50);
    room_core_step(&r, &in, &p);
    CHECK(r.level == 100);                    /* manual ON with nobody there */
    in.now_ms = (int64_t)p.override_s * 1000 + 200;
    room_events_t ev = room_core_step(&r, &in, &p);
    CHECK(ev.override_expired && r.mode == ROOM_MODE_AUTO && r.level == 0);

    room_core_set_mode(&r, ROOM_MODE_FORCED_OFF, in.now_ms, &p);
    in.pir = true;
    in.now_ms += 100;
    room_core_step(&r, &in, &p);
    CHECK(r.level == 0);                      /* forced off wins over motion */
    in.emergency = true;
    in.now_ms += 100;
    room_core_step(&r, &in, &p);
    CHECK(r.level == 100);                    /* emergency wins over everything */

    /* building closed: no grace period */
    room_core_t r2;
    room_core_init(&r2, 0);
    room_inputs_t a = in_at(0, true, 50);
    room_core_step(&r2, &a, &p);
    a.pir = false;
    a.building_open = false;
    a.now_ms = (int64_t)p.vacant_after_s * 1000 + 100;
    room_core_step(&r2, &a, &p);
    CHECK(!r2.occupied && r2.level == 0);
}

static void test_intrusion_when_empty(void)
{
    room_params_t p;
    room_core_t r;
    room_params_default(&p);
    room_core_init(&r, 0);
    room_inputs_t in = in_at(0, true, 50);
    in.building_inside = 0;                   /* entrance says nobody is inside */
    room_events_t ev = room_core_step(&r, &in, &p);
    CHECK(ev.intrusion && !r.occupied && r.level == 0);
    in.inside_known = false;                  /* count uncertain: act locally */
    in.now_ms = 100;
    ev = room_core_step(&r, &in, &p);
    CHECK(!ev.intrusion && r.occupied && r.level == 100);
}

static void test_energy(void)
{
    room_params_t p;
    room_core_t r;
    room_params_default(&p);
    p.load_w = 3600.0;                        /* 1 Wh per second at 100 % */
    room_core_init(&r, 0);
    room_core_set_mode(&r, ROOM_MODE_MANUAL_ON, 0, &p);
    room_inputs_t in = in_at(0, false, 50);
    room_core_step(&r, &in, &p);              /* level 100 from t=0 */
    in.now_ms = 10000;
    room_core_step(&r, &in, &p);              /* 10 s on */
    CHECK(r.on_s == 10 && r.wh > 9.99 && r.wh < 10.01);
    CHECK(r.baseline_wh > 9.99 && room_core_saved_wh(&r) < 0.01);
    room_core_set_mode(&r, ROOM_MODE_AUTO, in.now_ms, &p);
    in.now_ms = 10100;
    room_core_step(&r, &in, &p);              /* off now */
    in.now_ms = 20100;
    room_core_step(&r, &in, &p);
    CHECK(room_core_saved_wh(&r) > 9.9);      /* 10 s off during opening hours */
}

int main(void)
{
    test_occupancy_grace_and_off();
    test_daylight();
    test_modes_emergency_and_closing();
    test_intrusion_when_empty();
    test_energy();
    TEST_DONE("rooms");
}
