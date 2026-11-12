#ifndef AUTH_RUNTIME_H
#define AUTH_RUNTIME_H

/*
 * Operator authentication shared by the serial console, the keypad and the
 * web dashboard. PINs are stored as salted SHA-256 hashes in NVS.
 *   admin    / 1234  -> ADMIN    (everything)
 *   security / 4321  -> SECURITY (roll-call, logs, release, emergency)
 * Change them with "passwd" after the first login.
 */
#include <stdbool.h>
#include <stdint.h>
#include "common/system.h"

#define AUTH_NUM_OPS 2

/* ---- session logic (pure C, host tested) ---------------------------------- */
typedef struct {
    int op;                    /* -1 = nobody logged in */
    int fails;
    int64_t lock_until_ms;
    int64_t last_activity_ms;
    int fail_limit;
    int64_t lock_ms;
    int64_t idle_ms;
} auth_session_t;

typedef enum { AUTH_OK = 0, AUTH_BAD_PIN, AUTH_LOCKED_NOW, AUTH_LOCKED } auth_login_t;

void auth_session_init(auth_session_t *s, int fail_limit, int64_t lock_ms, int64_t idle_ms);
/* op = index of the operator whose PIN matched, or -1. */
auth_login_t auth_session_login(auth_session_t *s, int op, int64_t now_ms);
/* true if logged in with at least role `need`; refreshes the idle timer. */
bool auth_session_require(auth_session_t *s, scms_role_t need, int64_t now_ms, const char **why);
void auth_session_logout(auth_session_t *s);
bool auth_session_active(auth_session_t *s, int64_t now_ms);

const char *auth_op_name(int op);
scms_role_t auth_op_role(int op);
int auth_op_find(const char *name);

/* ---- PIN store (ESP32 runtime: NVS + mbedTLS) ----------------------------- */
void auth_runtime_init(void);
/* index of `name` if `pin` matches, otherwise -1 */
int auth_check(const char *name, const char *pin);
/* index of the first operator whose PIN matches (keypad, web), otherwise -1 */
int auth_check_any(const char *pin);
bool auth_set_pin(int op, const char *pin);

#endif
