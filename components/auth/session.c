#include "auth/runtime.h"

#include <string.h>
#include <strings.h>

/* Pure session logic: login lockout, role check and idle timeout. */

static const struct { const char *name; scms_role_t role; } AUTH_OPS[AUTH_NUM_OPS] = {
    { "admin", ROLE_ADMIN },
    { "security", ROLE_SECURITY },
};

const char *auth_op_name(int op) { return op >= 0 && op < AUTH_NUM_OPS ? AUTH_OPS[op].name : "-"; }
scms_role_t auth_op_role(int op) { return op >= 0 && op < AUTH_NUM_OPS ? AUTH_OPS[op].role : ROLE_STUDENT; }

int auth_op_find(const char *name)
{
    for (int i = 0; name && i < AUTH_NUM_OPS; i++)
        if (strcasecmp(name, AUTH_OPS[i].name) == 0) return i;
    return -1;
}

void auth_session_init(auth_session_t *s, int fail_limit, int64_t lock_ms, int64_t idle_ms)
{
    memset(s, 0, sizeof *s);
    s->op = -1;
    s->fail_limit = fail_limit;
    s->lock_ms = lock_ms;
    s->idle_ms = idle_ms;
}

auth_login_t auth_session_login(auth_session_t *s, int op, int64_t now_ms)
{
    if (now_ms < s->lock_until_ms) return AUTH_LOCKED;
    if (op >= 0 && op < AUTH_NUM_OPS) {
        s->op = op;
        s->fails = 0;
        s->last_activity_ms = now_ms;
        return AUTH_OK;
    }
    s->op = -1;
    if (++s->fails >= s->fail_limit) {
        s->fails = 0;
        s->lock_until_ms = now_ms + s->lock_ms;
        return AUTH_LOCKED_NOW;
    }
    return AUTH_BAD_PIN;
}

bool auth_session_active(auth_session_t *s, int64_t now_ms)
{
    if (s->op >= 0 && now_ms - s->last_activity_ms > s->idle_ms) s->op = -1;   /* expired */
    return s->op >= 0;
}

bool auth_session_require(auth_session_t *s, scms_role_t need, int64_t now_ms, const char **why)
{
    const char *reason = NULL;
    bool had = s->op >= 0;
    if (!auth_session_active(s, now_ms))
        reason = had ? "session expired, please login again" : "login required (login <user> <pin>)";
    else if (AUTH_OPS[s->op].role < need)
        reason = need == ROLE_ADMIN ? "ADMIN role required" : "SECURITY role required";
    if (why) *why = reason;
    if (reason) return false;
    s->last_activity_ms = now_ms;
    return true;
}

void auth_session_logout(auth_session_t *s) { s->op = -1; }
