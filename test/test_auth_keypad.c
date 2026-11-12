/* Host tests for operator sessions (components/auth/session.c) and the
   keypad menu state machine (components/entrance/kpmenu.c). */
#include <string.h>
#include "auth/runtime.h"
#include "check.h"
#include "entrance/kpmenu.h"

static void test_sessions(void)
{
    auth_session_t s;
    const char *why = NULL;
    auth_session_init(&s, 3, 60000, 300000);
    CHECK(!auth_session_require(&s, ROLE_SECURITY, 0, &why) && why);

    CHECK(auth_session_login(&s, -1, 1000) == AUTH_BAD_PIN);
    CHECK(auth_session_login(&s, -1, 2000) == AUTH_BAD_PIN);
    CHECK(auth_session_login(&s, -1, 3000) == AUTH_LOCKED_NOW);
    CHECK(auth_session_login(&s, 0, 4000) == AUTH_LOCKED);        /* right PIN, still locked */
    CHECK(auth_session_login(&s, 0, 63001) == AUTH_OK);

    int security = auth_op_find("security");
    CHECK(security == 1 && auth_op_role(security) == ROLE_SECURITY);
    CHECK(auth_session_login(&s, security, 70000) == AUTH_OK);
    CHECK(auth_session_require(&s, ROLE_SECURITY, 70001, &why));
    CHECK(!auth_session_require(&s, ROLE_ADMIN, 70002, &why));
    CHECK(!auth_session_require(&s, ROLE_SECURITY, 70002 + 300001, &why));   /* idle expiry */
    CHECK(strstr(why, "expired") != NULL);
}

static kp_action_t keys(kp_menu_t *m, const char *k, int64_t *t)
{
    kp_action_t a = { .kind = KPA_NONE };
    for (; *k; k++) {
        *t += 200;
        kp_action_t r = kp_menu_feed(m, *k, *t);
        if (r.kind != KPA_NONE) a = r;
    }
    return a;
}

static void test_keypad(void)
{
    kp_menu_t m;
    int64_t t = 0;
    char rows[4][21];
    kp_menu_init(&m);

    kp_action_t a = keys(&m, "#12", &t);
    CHECK(a.kind == KPA_NONE && m.state == KP_PIN);           /* PIN too short so far */
    kp_menu_render(&m, rows);
    CHECK(strstr(rows[1], "**") && !strstr(rows[1], "12"));   /* masked */
    a = keys(&m, "34#", &t);
    CHECK(a.kind == KPA_VERIFY_PIN && !strcmp(a.pin, "1234"));
    kp_menu_pin_result(&m, ROLE_ADMIN);
    CHECK(m.state == KP_MENU);

    a = keys(&m, "1", &t);
    CHECK(m.state == KP_SET_OPEN);
    a = keys(&m, "0730#", &t);
    CHECK(a.kind == KPA_SET_OPEN && a.minute == 450 && m.state == KP_MENU);
    a = keys(&m, "22599#", &t);                               /* 25:99 invalid */
    CHECK(a.kind == KPA_NONE && m.state == KP_SET_CLOSE);
    a = keys(&m, "1800#", &t);                                /* buffer was cleared */
    CHECK(a.kind == KPA_SET_CLOSE && a.minute == 1080);
    a = keys(&m, "2*", &t);                                   /* '*' on empty buffer = back */
    CHECK(a.kind == KPA_NONE && m.state == KP_MENU);

    a = keys(&m, "5", &t);
    CHECK(a.kind == KPA_ROOM_LIGHT && a.room == 2);
    a = keys(&m, "3", &t);
    CHECK(a.kind == KPA_EMERGENCY_TOGGLE);
    a = keys(&m, "0", &t);
    CHECK(a.kind == KPA_LOGOUT && m.state == KP_IDLE && m.role == -1);

    /* security may not change the schedule */
    keys(&m, "#4321#", &t);
    kp_menu_pin_result(&m, ROLE_SECURITY);
    a = keys(&m, "1", &t);
    CHECK(a.kind == KPA_NONE && m.state == KP_MENU && strstr(m.note, "Admin"));
    a = keys(&m, "6", &t);
    CHECK(a.kind == KPA_ALARM_OFF);

    /* idle timeout */
    a = kp_menu_tick(&m, t + 30001, 30000);
    CHECK(a.kind == KPA_TIMEOUT && m.state == KP_IDLE);
    a = kp_menu_tick(&m, t + 60000, 30000);
    CHECK(a.kind == KPA_NONE);

    /* wrong PIN stays on the PIN screen */
    keys(&m, "#0000#", &t);
    kp_menu_pin_result(&m, -1);
    CHECK(m.state == KP_PIN && strstr(m.note, "Wrong"));
}

int main(void)
{
    test_sessions();
    test_keypad();
    TEST_DONE("auth+keypad");
}
