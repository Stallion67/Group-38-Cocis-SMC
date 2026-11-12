#ifndef COMMON_SYSTEM_H
#define COMMON_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

/* Types shared by every component. Pure C: also used by the host tests. */

#define SCMS_MAX_USERS     32
#define SCMS_UID_HEX_MAX   21   /* 10-byte UID = 20 hex chars + NUL */
#define SCMS_NAME_MAX      24

typedef enum {
    ROLE_STUDENT = 0,
    ROLE_STAFF = 1,
    ROLE_SECURITY = 2,
    ROLE_ADMIN = 3
} scms_role_t;

typedef enum { POINT_ENTRY = 0, POINT_EXIT = 1 } scms_point_t;

typedef struct {
    char uid[SCMS_UID_HEX_MAX];  /* upper-case hex, no separators */
    char name[SCMS_NAME_MAX];
    scms_role_t role;
    bool blocked;
    bool inside;
    int64_t since;               /* wall time of last entry (seconds) */
} scms_user_t;

typedef struct {
    uint16_t open_min;           /* minutes after midnight            */
    uint16_t close_min;          /* may be < open_min (overnight)     */
    uint8_t days_mask;           /* bit0 = Sunday ... bit6 = Saturday */
    uint32_t version;            /* +1 on every change                */
} scms_schedule_t;

typedef enum {
    ACC_GRANTED = 0,
    ACC_GRANTED_EMERGENCY,       /* doors released, life safety first */
    ACC_GRANTED_PRIVILEGED,      /* security/admin outside the hours  */
    ACC_DENIED_UNKNOWN,          /* card not registered               */
    ACC_DENIED_BLOCKED,          /* card disabled by admin            */
    ACC_DENIED_READER_LOCKED,    /* too many failed scans             */
    ACC_DENIED_NO_CLOCK,         /* time unknown: cannot apply rules  */
    ACC_DENIED_OUTSIDE_HOURS,    /* entry outside the schedule        */
    ACC_DENIED_EXIT_AFTER_HOURS, /* inside after closing: held        */
    ACC_DENIED_ALREADY_INSIDE,   /* anti-passback on entry            */
    ACC_DENIED_NOT_INSIDE        /* anti-passback on exit (tailgater) */
} scms_access_result_t;

static inline bool scms_result_granted(scms_access_result_t r)
{
    return r == ACC_GRANTED || r == ACC_GRANTED_EMERGENCY || r == ACC_GRANTED_PRIVILEGED;
}

const char *scms_result_name(scms_access_result_t r);
const char *scms_role_name(scms_role_t r);
const char *scms_point_name(scms_point_t p);
/* Returns -1 when the text is not a role name (case-insensitive). */
int scms_role_parse(const char *text);
/* "HH:MM" or "HHMM" to minutes after midnight, -1 when invalid. */
int scms_parse_hhmm(const char *text);
/* Copies a UID, removes ':' and spaces, upper-cases it. false if not hex. */
bool scms_uid_normalise(const char *in, char out[SCMS_UID_HEX_MAX]);

#endif
