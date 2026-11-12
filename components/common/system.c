#include "common/system.h"

#include <ctype.h>
#include <string.h>

const char *scms_result_name(scms_access_result_t r)
{
    switch (r) {
    case ACC_GRANTED: return "OK";
    case ACC_GRANTED_EMERGENCY: return "EMERGENCY";
    case ACC_GRANTED_PRIVILEGED: return "PRIVILEGED_AFTER_HOURS";
    case ACC_DENIED_UNKNOWN: return "UNKNOWN_CARD";
    case ACC_DENIED_BLOCKED: return "CARD_BLOCKED";
    case ACC_DENIED_READER_LOCKED: return "READER_LOCKED";
    case ACC_DENIED_NO_CLOCK: return "NO_CLOCK";
    case ACC_DENIED_OUTSIDE_HOURS: return "OUTSIDE_HOURS";
    case ACC_DENIED_EXIT_AFTER_HOURS: return "EXIT_LOCKED_HOURS";
    case ACC_DENIED_ALREADY_INSIDE: return "ANTI_PASSBACK_IN";
    case ACC_DENIED_NOT_INSIDE: return "ANTI_PASSBACK_OUT";
    }
    return "?";
}

static const char *const ROLE_NAMES[] = { "STUDENT", "STAFF", "SECURITY", "ADMIN" };

const char *scms_role_name(scms_role_t r)
{
    return (r >= ROLE_STUDENT && r <= ROLE_ADMIN) ? ROLE_NAMES[r] : "?";
}

const char *scms_point_name(scms_point_t p) { return p == POINT_ENTRY ? "ENTRY" : "EXIT"; }

int scms_role_parse(const char *text)
{
    if (!text) return -1;
    for (int i = 0; i <= ROLE_ADMIN; i++) {
        const char *a = ROLE_NAMES[i], *b = text;
        while (*a && *b && toupper((unsigned char)*b) == *a) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

int scms_parse_hhmm(const char *t)
{
    if (!t) return -1;
    int digits[4], n = 0;
    size_t len = strlen(t);
    if (len == 5 && t[2] == ':') {
        digits[0] = t[0]; digits[1] = t[1]; digits[2] = t[3]; digits[3] = t[4]; n = 4;
    } else if (len == 4) {
        for (int i = 0; i < 4; i++) digits[i] = t[i];
        n = 4;
    }
    if (n != 4) return -1;
    for (int i = 0; i < 4; i++) {
        if (!isdigit(digits[i])) return -1;
        digits[i] -= '0';
    }
    int h = digits[0] * 10 + digits[1], m = digits[2] * 10 + digits[3];
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

bool scms_uid_normalise(const char *in, char out[SCMS_UID_HEX_MAX])
{
    size_t n = 0;
    if (!in) return false;
    for (; *in; in++) {
        if (*in == ':' || *in == ' ' || *in == '-') continue;
        if (!isxdigit((unsigned char)*in) || n >= SCMS_UID_HEX_MAX - 1) return false;
        out[n++] = (char)toupper((unsigned char)*in);
    }
    out[n] = '\0';
    /* 4, 7 or 10 byte UIDs (ISO/IEC 14443A) */
    return n == 8 || n == 14 || n == 20;
}
