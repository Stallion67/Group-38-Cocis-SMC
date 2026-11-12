#include "entrance/kpmenu.h"

#include <stdio.h>
#include <string.h>
#include "common/system.h"

void kp_menu_init(kp_menu_t *m)
{
    memset(m, 0, sizeof *m);
    m->role = -1;
}

void kp_menu_note(kp_menu_t *m, const char *text)
{
    snprintf(m->note, sizeof m->note, "%s", text ? text : "");
}

static void kp_clear(kp_menu_t *m)
{
    m->len = 0;
    m->buf[0] = '\0';
}

static kp_action_t kp_logout(kp_menu_t *m, const char *why)
{
    kp_action_t a = { .kind = KPA_LOGOUT };
    m->state = KP_IDLE;
    m->role = -1;
    kp_clear(m);
    kp_menu_note(m, why);
    return a;
}

kp_action_t kp_menu_feed(kp_menu_t *m, char key, int64_t now_ms)
{
    kp_action_t a = { .kind = KPA_NONE };
    m->last_key_ms = now_ms;
    bool digit = key >= '0' && key <= '9';

    switch (m->state) {
    case KP_IDLE:
        if (key == '#') {
            m->state = KP_PIN;
            kp_clear(m);
            kp_menu_note(m, "Enter PIN then #");
        }
        break;

    case KP_PIN:
        if (digit && m->len < 8) {
            m->buf[m->len++] = key;
            m->buf[m->len] = '\0';
        } else if (key == '*') {
            if (m->len) kp_clear(m);
            else { m->state = KP_IDLE; kp_menu_note(m, ""); }
        } else if (key == '#' && m->len >= 4) {
            a.kind = KPA_VERIFY_PIN;
            snprintf(a.pin, sizeof a.pin, "%s", m->buf);
            kp_clear(m);
        }
        break;

    case KP_MENU:
        if (key == '0' || key == '*') return kp_logout(m, "Logged out");
        if (key == '1' || key == '2') {
            if (m->role < ROLE_ADMIN) { kp_menu_note(m, "Admin only"); break; }
            m->state = key == '1' ? KP_SET_OPEN : KP_SET_CLOSE;
            kp_clear(m);
            kp_menu_note(m, "Type HHMM then #");
        } else if (key == '3') {
            a.kind = KPA_EMERGENCY_TOGGLE;
        } else if (key == '4' || key == '5') {
            if (m->role < ROLE_ADMIN) { kp_menu_note(m, "Admin only"); break; }
            a.kind = KPA_ROOM_LIGHT;
            a.room = key - '3';
        } else if (key == '6') {
            a.kind = KPA_ALARM_OFF;
        }
        break;

    case KP_SET_OPEN:
    case KP_SET_CLOSE:
        if (digit && m->len < 4) {
            m->buf[m->len++] = key;
            m->buf[m->len] = '\0';
        } else if (key == '*') {
            if (m->len) kp_clear(m);
            else { m->state = KP_MENU; kp_menu_note(m, ""); }
        } else if (key == '#') {
            int minute = m->len == 4 ? scms_parse_hhmm(m->buf) : -1;
            if (minute < 0) {
                kp_menu_note(m, "Invalid time");
                kp_clear(m);
                break;
            }
            a.kind = m->state == KP_SET_OPEN ? KPA_SET_OPEN : KPA_SET_CLOSE;
            a.minute = minute;
            m->state = KP_MENU;
            kp_clear(m);
        }
        break;
    }
    return a;
}

kp_action_t kp_menu_tick(kp_menu_t *m, int64_t now_ms, int64_t idle_ms)
{
    kp_action_t a = { .kind = KPA_NONE };
    if (m->state != KP_IDLE && now_ms - m->last_key_ms > idle_ms) {
        a = kp_logout(m, "Timed out");
        a.kind = KPA_TIMEOUT;
    }
    return a;
}

void kp_menu_pin_result(kp_menu_t *m, int role)
{
    if (role >= ROLE_SECURITY) {
        m->role = role;
        m->state = KP_MENU;
        kp_menu_note(m, role == ROLE_ADMIN ? "Welcome admin" : "Welcome security");
    } else {
        m->role = -1;
        m->state = KP_PIN;
        kp_menu_note(m, "Wrong PIN");
    }
}

void kp_menu_render(const kp_menu_t *m, char rows[4][21])
{
    char stars[9] = "";
    for (int i = 0; i < m->len && i < 8; i++) stars[i] = '*';
    stars[m->len < 8 ? m->len : 8] = '\0';
    switch (m->state) {
    case KP_PIN:
        snprintf(rows[0], 21, "MANAGEMENT LOGIN");
        snprintf(rows[1], 21, "PIN: %s", stars);
        snprintf(rows[2], 21, "# enter  * clear");
        break;
    case KP_MENU:
        snprintf(rows[0], 21, "1 Open  2 Close time");
        snprintf(rows[1], 21, "3 Emergency 6 Alarm");
        snprintf(rows[2], 21, "4/5 Room1/2 lights");
        break;
    case KP_SET_OPEN:
    case KP_SET_CLOSE:
        snprintf(rows[0], 21, "%s TIME (HHMM)", m->state == KP_SET_OPEN ? "OPENING" : "CLOSING");
        snprintf(rows[1], 21, "> %s", m->buf);
        snprintf(rows[2], 21, "# save  * back");
        break;
    default:
        snprintf(rows[0], 21, "Press # to manage");
        rows[1][0] = rows[2][0] = '\0';
        break;
    }
    snprintf(rows[3], 21, "%s", m->note);
}
