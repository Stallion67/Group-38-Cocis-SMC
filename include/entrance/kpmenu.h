#ifndef ENTRANCE_KPMENU_H
#define ENTRANCE_KPMENU_H

/*
 * Keypad management menu as a pure state machine (host tested).
 * Keys: 0-9, '*' (clear / back), '#' (enter).
 *
 *   #  PIN  #        log in (admin or security PIN)
 *   1  HHMM #        set opening time        (admin)
 *   2  HHMM #        set closing time        (admin)
 *   3                emergency on / off      (security, admin)
 *   4 / 5            room 1 / room 2 lights: auto -> on -> off -> auto (admin)
 *   6                silence alarm, unlock readers (security, admin)
 *   0 or *           log out
 * The menu returns an action; the runtime performs it and reports back.
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum { KP_IDLE = 0, KP_PIN, KP_MENU, KP_SET_OPEN, KP_SET_CLOSE } kp_state_t;

typedef enum {
    KPA_NONE = 0,
    KPA_VERIFY_PIN,     /* pin[] holds the digits */
    KPA_SET_OPEN,       /* minute holds the time  */
    KPA_SET_CLOSE,
    KPA_EMERGENCY_TOGGLE,
    KPA_ROOM_LIGHT,     /* room holds 1 or 2      */
    KPA_ALARM_OFF,
    KPA_LOGOUT,
    KPA_TIMEOUT
} kp_action_kind_t;

typedef struct {
    kp_action_kind_t kind;
    char pin[9];
    int minute;
    int room;
} kp_action_t;

typedef struct {
    kp_state_t state;
    char buf[9];
    int len;
    int64_t last_key_ms;
    int role;           /* role of the logged-in operator, -1 none */
    char note[21];      /* last message for the LCD                */
} kp_menu_t;

void kp_menu_init(kp_menu_t *m);
kp_action_t kp_menu_feed(kp_menu_t *m, char key, int64_t now_ms);
/* Call regularly; returns KPA_TIMEOUT once after KEYPAD_IDLE_MS idle. */
kp_action_t kp_menu_tick(kp_menu_t *m, int64_t now_ms, int64_t idle_ms);
/* Runtime reports the PIN check: role >= 0 on success. */
void kp_menu_pin_result(kp_menu_t *m, int role);
void kp_menu_note(kp_menu_t *m, const char *text);
/* Four LCD rows (20 chars) for the admin view. */
void kp_menu_render(const kp_menu_t *m, char rows[4][21]);

#endif
