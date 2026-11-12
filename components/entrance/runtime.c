#include "entrance/runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "actuator/runtime.h"
#include "auth/runtime.h"
#include "common/config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "entrance/clock.h"
#include "entrance/core.h"
#include "entrance/dashboard.h"
#include "entrance/display.h"
#include "entrance/kpmenu.h"
#include "entrance/mfrc522.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rooms/core.h"
#include "storage/runtime.h"

#define EN_SAY(fmt, ...) printf("[ENTRANCE] " fmt "\n", ##__VA_ARGS__)

typedef struct {
    scms_point_t point;
    char uid[SCMS_UID_HEX_MAX];
} en_scan_t;

/* ---- shared state -----------------------------------------------------------
 * en_mtx  : roll-call, users, schedule, statistics, room views, flags
 * kp_mtx  : keypad menu (keypad task writes, coordinator renders)
 * ui_mtx  : transient LCD message
 * en_scan_q: every card scan, from the readers and the console, goes
 *            through this queue to ONE access task.
 * --------------------------------------------------------------------------- */
static SemaphoreHandle_t en_mtx, kp_mtx, ui_mtx;
static QueueHandle_t en_scan_q;
static ent_state_t en;
static bool en_emergency, en_uncertain;
static uint32_t en_alerts;
static entrance_room_view_t en_room[NUM_ROOMS + 1];
static int64_t en_last_seen[NUM_ROOMS + 1];   /* ms, monotonic */
static kp_menu_t en_kp;
static auth_session_t en_kp_sess;
static char ui_l1[21], ui_l2[21];
static int64_t ui_until_ms;
static i2c_master_bus_handle_t en_i2c;

static int64_t en_now_ms(void) { return esp_timer_get_time() / 1000; }

/* ======================================================================== */
/*  helpers                                                                  */
/* ======================================================================== */
static void en_ui_flash(const char *l1, const char *l2)
{
    xSemaphoreTake(ui_mtx, portMAX_DELAY);
    snprintf(ui_l1, sizeof ui_l1, "%s", l1);
    snprintf(ui_l2, sizeof ui_l2, "%s", l2);
    ui_until_ms = en_now_ms() + 3000;
    xSemaphoreGive(ui_mtx);
}

void entrance_alert(const char *level, const char *source, const char *fmt, ...)
{
    char msg[112], ts[24];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    clock_now_str(ts, sizeof ts);
    bool info = !strcmp(level, "INFO");   /* notices in cyan, real alerts in red */
    printf("\n\033[1;%dm*** %s %s %s [%s] %s ***\033[0m\n", info ? 36 : 31, info ? "NOTICE" : "ALERT", ts, level,
           source, msg);
    storage_log(LOG_EVENT, "%s,%s,%s", level, source, msg);
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    en_alerts++;
    xSemaphoreGive(en_mtx);
    if (!strcmp(level, "HIGH") || !strcmp(level, "CRITICAL")) {
        char l1[21];
        snprintf(l1, sizeof l1, "ALERT: %s", source);
        en_ui_flash(l1, msg);
        actuator_beep(BEEP_ALERT);
    }
}

static bool en_is_open_locked(void)
{
    int wday, minute;
    return clock_local(&wday, &minute) && ent_schedule_is_open(&en.schedule, wday, minute);
}

static void en_publish(void)
{
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    bus_building_t b = {
        .open = en_is_open_locked(),
        .emergency = en_emergency,
        .inside = en.inside_count,
        .inside_known = !en_uncertain,
        .schedule_version = en.schedule.version,
    };
    xSemaphoreGive(en_mtx);
    bus_publish_building(&b);
}

/* Occupancy survives a power cut: UIDs of people inside are kept in NVS. */
static void en_persist_inside(void)
{
    static char blob[SCMS_MAX_USERS * (SCMS_UID_HEX_MAX + 1) + 1];
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    blob[0] = '\0';
    for (int i = 0; i < en.user_count; i++) {
        if (!en.users[i].inside) continue;
        strncat(blob, en.users[i].uid, sizeof blob - strlen(blob) - 2);
        strcat(blob, ";");
    }
    xSemaphoreGive(en_mtx);
    storage_nvs_set("inside", blob, sizeof blob);
}

static void en_restore_inside(void)
{
    static char blob[SCMS_MAX_USERS * (SCMS_UID_HEX_MAX + 1) + 1];
    if (!storage_nvs_get("inside", blob, sizeof blob)) return;
    blob[sizeof blob - 1] = '\0';
    char *save = NULL;
    for (char *tok = strtok_r(blob, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        int i = ent_find(&en, tok);
        if (i >= 0 && !en.users[i].inside) {
            en.users[i].inside = true;
            en.users[i].since = time(NULL);
            en.inside_count++;
        }
    }
    if (en.inside_count) EN_SAY("restored %d people inside after restart", en.inside_count);
}

static void en_save_users(void)
{
    static scms_user_t copy[SCMS_MAX_USERS];
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    int n = en.user_count;
    memcpy(copy, en.users, sizeof(scms_user_t) * (size_t)n);
    xSemaphoreGive(en_mtx);
    if (!storage_save_users(copy, n)) EN_SAY("warning: roster not saved to NVS");
}

/* ======================================================================== */
/*  access decisions: the only writer of the roll-call                       */
/* ======================================================================== */
static void en_process(const en_scan_t *s)
{
    ent_ctx_t ctx = {
        .emergency = false,
        .now_ms = en_now_ms(),
        .wall_s = time(NULL),
        .fail_limit = FAIL_LIMIT,
        .fail_window_ms = FAIL_WINDOW_MS,
        .lock_ms = READER_LOCK_MS,
    };
    ctx.clock_valid = clock_local(&ctx.wday, &ctx.minute);
    char name[SCMS_NAME_MAX] = "UNKNOWN";

    xSemaphoreTake(en_mtx, portMAX_DELAY);
    ctx.emergency = en_emergency;
    int before = en.inside_count;
    ent_decision_t d = ent_process_scan(&en, s->point, s->uid, &ctx);
    if (d.user >= 0) snprintf(name, sizeof name, "%s", en.users[d.user].name);
    if (d.result == ACC_DENIED_NOT_INSIDE) en_uncertain = true;   /* count may be wrong */
    xSemaphoreGive(en_mtx);

    bool ok = scms_result_granted(d.result);
    const char *reason = scms_result_name(d.result);
    printf("[ACCESS] %s uid=%s name=%s %s (%s) inside=%d\n", scms_point_name(s->point), s->uid, name,
           ok ? "GRANTED" : "DENIED", reason, d.inside_after);
    storage_log(LOG_ACCESS, "%s,%s,%s,%s,%s,%d", scms_point_name(s->point), s->uid, name,
                ok ? "GRANTED" : "DENIED", reason, d.inside_after);

    if (ok) {
        actuator_open_door(s->point);
        actuator_beep(BEEP_OK);
        en_ui_flash(s->point == POINT_ENTRY ? "WELCOME" : "GOODBYE", name);
        if (d.inside_after != before) en_persist_inside();
        en_publish();
    } else {
        actuator_beep(BEEP_DENY);
        en_ui_flash("ACCESS DENIED", reason);
    }

    if (d.lockout_tripped) {
        actuator_alarm(true);
        entrance_alert("HIGH", scms_point_name(s->point), "%d failed scans in %ds (last UID %s). Reader locked %ds",
                       FAIL_LIMIT, FAIL_WINDOW_MS / 1000, s->uid, READER_LOCK_MS / 1000);
    }
    if (d.result == ACC_DENIED_ALREADY_INSIDE)
        entrance_alert("MEDIUM", "ANTI-PASSBACK", "%s (%s) used the entry reader while inside", name, s->uid);
    if (d.result == ACC_DENIED_NOT_INSIDE)
        entrance_alert("MEDIUM", "ANTI-PASSBACK", "%s (%s) tried to exit with no entry record: tailgating?", name,
                       s->uid);
    if (d.after_hours_inside_alert)
        entrance_alert("MEDIUM", "EXIT", "%s (%s) is inside after hours and tried to leave", name, s->uid);
}

static void en_access_task(void *arg)
{
    (void)arg;
    en_scan_t s;
    for (;;)
        if (xQueueReceive(en_scan_q, &s, portMAX_DELAY) == pdTRUE) en_process(&s);
}

/* ======================================================================== */
/*  RFID readers                                                             */
/* ======================================================================== */
static void en_rfid_task(void *arg)
{
    (void)arg;
    char last_uid[2][SCMS_UID_HEX_MAX] = { "", "" };
    int64_t last_ms[2] = { 0, 0 };
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        for (int r = 0; r < 2; r++) {
            uint8_t raw[10], len = 0;
            if (!mfrc522_read_uid(r, raw, &len)) continue;
            en_scan_t s = { .point = r == MFRC_READER_ENTRY ? POINT_ENTRY : POINT_EXIT };
            for (int i = 0; i < len; i++) snprintf(s.uid + i * 2, 3, "%02X", raw[i]);
            int64_t now = en_now_ms();
            if (!strcmp(s.uid, last_uid[r]) && now - last_ms[r] < SCAN_REPEAT_MS) continue;
            snprintf(last_uid[r], sizeof last_uid[r], "%s", s.uid);
            last_ms[r] = now;
            printf("[RFID] %s reader: card %s\n", scms_point_name(s.point), s.uid);
            if (xQueueSend(en_scan_q, &s, 0) != pdTRUE) printf("[RFID] scan queue full, card ignored\n");
        }
    }
}

bool entrance_simulate_scan(scms_point_t point, const char *uid)
{
    en_scan_t s = { .point = point };
    if (!scms_uid_normalise(uid, s.uid)) return false;
    return xQueueSend(en_scan_q, &s, 0) == pdTRUE;
}

/* ======================================================================== */
/*  management actions (callers authenticate first)                          */
/* ======================================================================== */
esp_err_t entrance_set_schedule(uint16_t open_min, uint16_t close_min, int days_mask, const char *by)
{
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    scms_schedule_t s = en.schedule;
    s.open_min = open_min;
    s.close_min = close_min;
    if (days_mask >= 0) s.days_mask = (uint8_t)days_mask;
    if (!ent_schedule_valid(&s)) {
        xSemaphoreGive(en_mtx);
        return ESP_ERR_INVALID_ARG;
    }
    s.version = en.schedule.version + 1;
    scms_schedule_t old = en.schedule;
    en.schedule = s;
    xSemaphoreGive(en_mtx);
    storage_nvs_set("sched", &s, sizeof s);
    entrance_alert("INFO", "CONFIG", "schedule %02d:%02d-%02d:%02d -> %02d:%02d-%02d:%02d days 0x%02X (v%lu) by %s",
                   old.open_min / 60, old.open_min % 60, old.close_min / 60, old.close_min % 60,
                   s.open_min / 60, s.open_min % 60, s.close_min / 60, s.close_min % 60, s.days_mask,
                   (unsigned long)s.version, by);
    en_publish();
    return ESP_OK;
}

void entrance_set_emergency(bool on, const char *by)
{
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    bool changed = en_emergency != on;
    en_emergency = on;
    xSemaphoreGive(en_mtx);
    if (!changed) return;
    actuator_emergency(on);
    if (on) entrance_alert("CRITICAL", "EMERGENCY", "emergency ON by %s: doors released, all lights 100%%", by);
    else entrance_alert("INFO", "EMERGENCY", "emergency cleared by %s, normal operation", by);
    en_publish();
}

void entrance_alarm_off(const char *by)
{
    actuator_alarm(false);
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    ent_clear_lockouts(&en);
    xSemaphoreGive(en_mtx);
    entrance_alert("INFO", "ALARM", "alarm silenced and readers unlocked by %s", by);
}

bool entrance_release(const char *uid, const char *by)
{
    char norm[SCMS_UID_HEX_MAX], name[SCMS_NAME_MAX] = "";
    if (!scms_uid_normalise(uid, norm)) return false;
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    int i = ent_find(&en, norm);
    bool ok = ent_release(&en, norm);
    if (ok) snprintf(name, sizeof name, "%s", en.users[i].name);
    int inside = en.inside_count;
    xSemaphoreGive(en_mtx);
    if (!ok) return false;
    actuator_open_door(POINT_EXIT);
    storage_log(LOG_ACCESS, "EXIT,%s,%s,GRANTED,RELEASED_BY_%s,%d", norm, name, by, inside);
    entrance_alert("MEDIUM", "EXIT", "%s released by %s", name, by);
    en_persist_inside();
    en_publish();
    return true;
}

void entrance_confirm_empty(const char *by)
{
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    ent_clear_inside(&en);
    en_uncertain = false;
    xSemaphoreGive(en_mtx);
    en_persist_inside();
    entrance_alert("INFO", "ROLLCALL", "building confirmed empty by %s", by);
    en_publish();
}

bool entrance_user_add(const char *uid, scms_role_t role, const char *name, const char *by)
{
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    bool ok = ent_add_user(&en, uid, role, name, false);
    xSemaphoreGive(en_mtx);
    if (!ok) return false;
    en_save_users();
    storage_log(LOG_EVENT, "INFO,USERS,added %s %s (%s) by %s", uid, name, scms_role_name(role), by);
    return true;
}

bool entrance_user_remove(const char *uid, const char *by)
{
    char norm[SCMS_UID_HEX_MAX];
    if (!scms_uid_normalise(uid, norm)) return false;
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    bool ok = ent_remove_user(&en, norm);
    xSemaphoreGive(en_mtx);
    if (!ok) return false;
    en_save_users();
    storage_log(LOG_EVENT, "INFO,USERS,removed %s by %s", norm, by);
    return true;
}

bool entrance_user_block(const char *uid, bool blocked, const char *by)
{
    char norm[SCMS_UID_HEX_MAX];
    if (!scms_uid_normalise(uid, norm)) return false;
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    bool ok = ent_set_blocked(&en, norm, blocked);
    xSemaphoreGive(en_mtx);
    if (!ok) return false;
    en_save_users();
    storage_log(LOG_EVENT, "INFO,USERS,%s %s by %s", blocked ? "blocked" : "unblocked", norm, by);
    return true;
}

bool entrance_room_command(int room, uint8_t mode, uint16_t vacancy_s, uint16_t pause_s, const char *by)
{
    if (room < 1 || room > NUM_ROOMS) return false;
    bus_msg_t m = { .kind = MSG_ROOM_CMD, .src = NODE_ENTRANCE };
    m.u.cmd.mode = mode;
    m.u.cmd.vacancy_s = vacancy_s;
    m.u.cmd.pause_s = pause_s;
    bool ok = bus_send((bus_node_t)room, &m, pdMS_TO_TICKS(100));
    storage_log(LOG_EVENT, "INFO,ROOM%d,command mode=%u vacancy=%u pause=%u by %s %s", room, mode, vacancy_s,
                pause_s, by, ok ? "sent" : "FAILED");
    return ok;
}

void entrance_set_time(int64_t epoch_s, const char *by)
{
    clock_set_manual((time_t)epoch_s);
    char ts[24];
    clock_now_str(ts, sizeof ts);
    entrance_alert("INFO", "CONFIG", "clock set to %s by %s", ts, by);
    en_publish();
}

/* ======================================================================== */
/*  status for the console and the dashboard                                 */
/* ======================================================================== */
void entrance_get_status(entrance_status_t *o)
{
    memset(o, 0, sizeof *o);
    o->clock_valid = clock_valid();
    snprintf(o->clock_source, sizeof o->clock_source, "%s", clock_source());
    clock_now_str(o->now, sizeof o->now);
    int64_t now = en_now_ms();
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    o->open = en_is_open_locked();
    o->emergency = en_emergency;
    o->inside = en.inside_count;
    o->inside_known = !en_uncertain;
    o->schedule = en.schedule;
    for (int p = 0; p < 2; p++) o->reader_locked[p] = ent_reader_locked(&en, (scms_point_t)p, now);
    o->granted = en.granted;
    o->denied = en.denied;
    o->peak = en.peak_inside;
    o->alerts = en_alerts;
    xSemaphoreGive(en_mtx);
    for (int p = 0; p < 2; p++) {
        o->reader_ready[p] = mfrc522_ready(p);
        o->door_open[p] = actuator_door_open((scms_point_t)p);
    }
    o->alarm = actuator_alarm_active();
    o->sd_ok = storage_sd_ok();
    o->lcd_ok = display_ok();
    o->rtc_ok = clock_rtc_ok();
}

void entrance_get_room(int room, entrance_room_view_t *out)
{
    memset(out, 0, sizeof *out);
    if (room < 1 || room > NUM_ROOMS) return;
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    *out = en_room[room];
    out->age_ms = out->seen ? en_now_ms() - en_last_seen[room] : -1;
    xSemaphoreGive(en_mtx);
}

int entrance_copy_users(scms_user_t *out, int max)
{
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    int n = en.user_count < max ? en.user_count : max;
    memcpy(out, en.users, sizeof(scms_user_t) * (size_t)n);
    xSemaphoreGive(en_mtx);
    return n;
}

/* ======================================================================== */
/*  coordinator: room messages, heartbeats, schedule changes, LCD            */
/* ======================================================================== */
static void en_handle_room_msg(const bus_msg_t *m)
{
    int room = m->src;
    if (room < 1 || room > NUM_ROOMS) return;
    if (m->kind == MSG_ROOM_STATE) {
        xSemaphoreTake(en_mtx, portMAX_DELAY);
        bool was_offline = en_room[room].seen && !en_room[room].online;
        en_room[room].seen = true;
        en_room[room].online = true;
        en_last_seen[room] = en_now_ms();
        en_room[room].state = m->u.state;
        xSemaphoreGive(en_mtx);
        if (was_offline) entrance_alert("INFO", "LINK", "Room %d heartbeat restored", room);
        return;
    }
    if (m->kind != MSG_ROOM_EVENT) return;
    const bus_room_event_t *e = &m->u.event;
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    uint32_t last = en_room[room].last_seq;
    if (last && e->seq > last + 1) en_room[room].lost_events += e->seq - last - 1;   /* gap = lost */
    en_room[room].last_seq = e->seq;    /* a lower number means the room restarted */
    xSemaphoreGive(en_mtx);
    printf("[ROOM%d] #%lu %s %s\n", room, (unsigned long)e->seq, e->type, e->detail);
    storage_log(LOG_EVENT, "INFO,ROOM%d,%s %s", room, e->type, e->detail);
    if (!strcmp(e->type, "INTRUSION"))
        entrance_alert("HIGH", "INTRUSION", "motion in Room %d while the building is empty", room);
}

static void en_render_lcd(bool open, int64_t now)
{
    char rows[4][21];
    memset(rows, 0, sizeof rows);
    xSemaphoreTake(kp_mtx, portMAX_DELAY);
    bool menu = en_kp.state != KP_IDLE;
    if (menu) kp_menu_render(&en_kp, rows);
    xSemaphoreGive(kp_mtx);
    if (!menu) {
        entrance_room_view_t r1, r2;
        entrance_get_room(1, &r1);
        entrance_get_room(2, &r2);
        xSemaphoreTake(en_mtx, portMAX_DELAY);
        scms_schedule_t s = en.schedule;
        int inside = en.inside_count;
        bool em = en_emergency;
        xSemaphoreGive(en_mtx);
        if (em) snprintf(rows[0], 21, "!!! EMERGENCY !!!");
        else if (actuator_alarm_active()) snprintf(rows[0], 21, "ALARM: see console");
        else snprintf(rows[0], 21, "%s %02d:%02d-%02d:%02d", open ? "OPEN  " : "CLOSED", s.open_min / 60,
                      s.open_min % 60, s.close_min / 60, s.close_min % 60);
        char hms[10] = "--:--:--";
        if (clock_valid()) {
            time_t t = time(NULL);
            struct tm tm;
            localtime_r(&t, &tm);
            strftime(hms, sizeof hms, "%H:%M:%S", &tm);
        }
        snprintf(rows[1], 21, "Inside:%-3d %s", inside, hms);
        /* "1:OCC100%  2:---  0%" = room, occupancy, light level */
        snprintf(rows[2], 21, "1:%s%3d%%  2:%s%3d%%", !r1.online ? "OFF" : r1.state.occupied ? "OCC" : "---",
                 r1.state.level, !r2.online ? "OFF" : r2.state.occupied ? "OCC" : "---", r2.state.level);
        xSemaphoreTake(ui_mtx, portMAX_DELAY);
        if (now < ui_until_ms) {
            snprintf(rows[3], 21, "%.20s", ui_l2[0] ? ui_l2 : ui_l1);
            snprintf(rows[2], 21, "%.20s", ui_l1);
        } else {
            snprintf(rows[3], 21, "Tap card / # manage");
        }
        xSemaphoreGive(ui_mtx);
    }
    display_write(rows);
}

static void en_coordinator_task(void *arg)
{
    (void)arg;
    xSemaphoreTake(en_mtx, portMAX_DELAY);
    bool prev_open = en_is_open_locked();
    xSemaphoreGive(en_mtx);
    int64_t last_tick = 0, last_publish = 0, last_energy = en_now_ms();
    for (;;) {
        bus_msg_t m;
        if (bus_receive(NODE_ENTRANCE, &m, pdMS_TO_TICKS(100))) en_handle_room_msg(&m);
        int64_t now = en_now_ms();
        if (now - last_tick < 500) continue;
        last_tick = now;

        /* schedule transitions */
        xSemaphoreTake(en_mtx, portMAX_DELAY);
        bool open = en_is_open_locked();
        int inside = en.inside_count;
        xSemaphoreGive(en_mtx);
        if (open != prev_open) {
            prev_open = open;
            if (open) entrance_alert("INFO", "SCHEDULE", "building OPEN for access");
            else if (inside) entrance_alert("MEDIUM", "SCHEDULE", "building CLOSED with %d inside: exits held", inside);
            else entrance_alert("INFO", "SCHEDULE", "building CLOSED and empty");
            if (!open) actuator_close_all();   /* ignored during an emergency */
            en_publish();
            last_publish = now;
        }

        /* room heartbeat supervision */
        for (int r = 1; r <= NUM_ROOMS; r++) {
            bool lost = false;
            xSemaphoreTake(en_mtx, portMAX_DELAY);
            if (en_room[r].online && now - en_last_seen[r] > ROOM_TIMEOUT_MS) {
                en_room[r].online = false;
                lost = true;
            }
            xSemaphoreGive(en_mtx);
            if (lost) entrance_alert("MEDIUM", "LINK", "Room %d heartbeat lost (terminal offline)", r);
        }

        /* refresh the retained building state, energy snapshot every 5 min */
        if (now - last_publish >= 5000) {
            en_publish();
            last_publish = now;
        }
        if (now - last_energy >= 300000) {
            last_energy = now;
            for (int r = 1; r <= NUM_ROOMS; r++) {
                entrance_room_view_t v;
                entrance_get_room(r, &v);
                double saved = v.state.baseline_wh > v.state.wh ? v.state.baseline_wh - v.state.wh : 0;
                storage_log(LOG_ENERGY, "%d,%.2f,%.1f,%.1f,%.0f", r, v.state.on_s / 3600.0, v.state.wh, saved,
                            saved / 1000.0 * TARIFF_UGX_PER_KWH);
            }
        }
        en_render_lcd(open, now);
    }
}

/* ======================================================================== */
/*  keypad and break-glass button                                            */
/* ======================================================================== */
static const int KP_ROWS[4] = { PIN_KP_R1, PIN_KP_R2, PIN_KP_R3, PIN_KP_R4 };
static const int KP_COLS[3] = { PIN_KP_C1, PIN_KP_C2, PIN_KP_C3 };
static const char KP_MAP[4][3] = { { '1', '2', '3' }, { '4', '5', '6' }, { '7', '8', '9' }, { '*', '0', '#' } };

/* Returns the pressed key, or 0. Two or more keys at once (or a column
   stuck low) is treated as "no key" so a wiring fault cannot type. */
static char en_kp_scan(void)
{
    char key = 0;
    int pressed = 0;
    for (int r = 0; r < 4; r++) {
        gpio_set_level(KP_ROWS[r], 0);
        esp_rom_delay_us(20);
        for (int c = 0; c < 3; c++) {
            if (gpio_get_level(KP_COLS[c]) == 0) {
                key = KP_MAP[r][c];
                pressed++;
            }
        }
        gpio_set_level(KP_ROWS[r], 1);
    }
    return pressed == 1 ? key : 0;
}

static void en_kp_act(const kp_action_t *a)
{
    const char *who;
    xSemaphoreTake(kp_mtx, portMAX_DELAY);
    who = auth_op_name(en_kp_sess.op);
    xSemaphoreGive(kp_mtx);
    char by[24];
    snprintf(by, sizeof by, "keypad:%s", who);

    switch (a->kind) {
    case KPA_VERIFY_PIN: {
        int op = auth_check_any(a->pin);
        xSemaphoreTake(kp_mtx, portMAX_DELAY);
        auth_login_t res = auth_session_login(&en_kp_sess, op, en_now_ms());
        if (res == AUTH_OK) kp_menu_pin_result(&en_kp, auth_op_role(op));
        else kp_menu_pin_result(&en_kp, -1);
        if (res == AUTH_LOCKED || res == AUTH_LOCKED_NOW) {
            kp_menu_note(&en_kp, "Keypad locked 60s");
            en_kp.state = KP_IDLE;
        }
        xSemaphoreGive(kp_mtx);
        if (res == AUTH_OK) storage_log(LOG_EVENT, "INFO,KEYPAD,login %s", auth_op_name(op));
        if (res == AUTH_LOCKED_NOW)
            entrance_alert("HIGH", "KEYPAD", "%d wrong PINs: keypad locked %ds", LOGIN_FAIL_LIMIT, LOGIN_LOCK_MS / 1000);
        break;
    }
    case KPA_SET_OPEN:
    case KPA_SET_CLOSE: {
        xSemaphoreTake(en_mtx, portMAX_DELAY);
        scms_schedule_t s = en.schedule;
        xSemaphoreGive(en_mtx);
        if (a->kind == KPA_SET_OPEN) s.open_min = (uint16_t)a->minute;
        else s.close_min = (uint16_t)a->minute;
        esp_err_t e = entrance_set_schedule(s.open_min, s.close_min, -1, by);
        xSemaphoreTake(kp_mtx, portMAX_DELAY);
        kp_menu_note(&en_kp, e == ESP_OK ? "Schedule saved" : "Rejected: invalid");
        xSemaphoreGive(kp_mtx);
        break;
    }
    case KPA_EMERGENCY_TOGGLE: {
        xSemaphoreTake(en_mtx, portMAX_DELAY);
        bool on = !en_emergency;
        xSemaphoreGive(en_mtx);
        entrance_set_emergency(on, by);
        xSemaphoreTake(kp_mtx, portMAX_DELAY);
        kp_menu_note(&en_kp, on ? "Emergency ON" : "Emergency OFF");
        xSemaphoreGive(kp_mtx);
        break;
    }
    case KPA_ROOM_LIGHT: {
        entrance_room_view_t v;
        entrance_get_room(a->room, &v);
        uint8_t next = v.state.mode == ROOM_MODE_AUTO ? ROOM_MODE_MANUAL_ON
                       : v.state.mode == ROOM_MODE_MANUAL_ON ? ROOM_MODE_FORCED_OFF : ROOM_MODE_AUTO;
        bool ok = entrance_room_command(a->room, next, 0, 0, by);
        char note[21];
        snprintf(note, sizeof note, "Room %d: %s", a->room, ok ? room_mode_name((room_mode_t)next) : "no link");
        xSemaphoreTake(kp_mtx, portMAX_DELAY);
        kp_menu_note(&en_kp, note);
        xSemaphoreGive(kp_mtx);
        break;
    }
    case KPA_ALARM_OFF:
        entrance_alarm_off(by);
        xSemaphoreTake(kp_mtx, portMAX_DELAY);
        kp_menu_note(&en_kp, "Alarm off");
        xSemaphoreGive(kp_mtx);
        break;
    case KPA_LOGOUT:
    case KPA_TIMEOUT:
        xSemaphoreTake(kp_mtx, portMAX_DELAY);
        auth_session_logout(&en_kp_sess);
        xSemaphoreGive(kp_mtx);
        storage_log(LOG_EVENT, "INFO,KEYPAD,%s", a->kind == KPA_TIMEOUT ? "session timed out" : "logout");
        break;
    default:
        break;
    }
}

static void en_keypad_task(void *arg)
{
    (void)arg;
    char prev = 0, stable = 0;
    int same = 0;
    int prev_btn = gpio_get_level(PIN_EMERGENCY_BTN);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        int64_t now = en_now_ms();

        /* break-glass: turns emergency ON only; clearing needs a login */
        int btn = gpio_get_level(PIN_EMERGENCY_BTN);
        if (prev_btn == 1 && btn == 0) entrance_set_emergency(true, "BREAK-GLASS");
        prev_btn = btn;

        /* keypad: a key must read the same twice (40 ms) to count */
        char k = en_kp_scan();
        same = (k == prev) ? same + 1 : 0;
        prev = k;
        kp_action_t a = { .kind = KPA_NONE };
        if (same == 1 && k != stable) {
            stable = k;
            if (k) {
                actuator_beep(BEEP_KEY);
                xSemaphoreTake(kp_mtx, portMAX_DELAY);
                bool locked = now < en_kp_sess.lock_until_ms;
                if (!locked) a = kp_menu_feed(&en_kp, k, now);
                else kp_menu_note(&en_kp, "Keypad locked");
                xSemaphoreGive(kp_mtx);
            }
        }
        if (a.kind == KPA_NONE) {
            xSemaphoreTake(kp_mtx, portMAX_DELAY);
            a = kp_menu_tick(&en_kp, now, KEYPAD_IDLE_MS);
            xSemaphoreGive(kp_mtx);
        }
        if (a.kind != KPA_NONE) en_kp_act(&a);
    }
}

static esp_err_t en_gpio_init(void)
{
    uint64_t rows = 0, cols = 0;
    for (int i = 0; i < 4; i++) rows |= 1ULL << KP_ROWS[i];
    for (int i = 0; i < 3; i++) cols |= 1ULL << KP_COLS[i];
    gpio_config_t out = { .pin_bit_mask = rows, .mode = GPIO_MODE_OUTPUT };
    gpio_config_t in = { .pin_bit_mask = cols, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config_t btn = { .pin_bit_mask = 1ULL << PIN_EMERGENCY_BTN, .mode = GPIO_MODE_INPUT };  /* external pull-up */
    esp_err_t e = gpio_config(&out);
    if (e == ESP_OK) e = gpio_config(&in);
    if (e == ESP_OK) e = gpio_config(&btn);
    for (int i = 0; i < 4; i++) gpio_set_level(KP_ROWS[i], 1);
    return e;
}

/* ======================================================================== */
/*  start-up                                                                 */
/* ======================================================================== */
esp_err_t entrance_start(void)
{
    en_mtx = xSemaphoreCreateMutex();
    kp_mtx = xSemaphoreCreateMutex();
    ui_mtx = xSemaphoreCreateMutex();
    en_scan_q = xQueueCreate(8, sizeof(en_scan_t));
    if (!en_mtx || !kp_mtx || !ui_mtx || !en_scan_q) return ESP_ERR_NO_MEM;

    /* roster, schedule and roll-call */
    ent_init(&en);
    int n = storage_load_users(en.users, SCMS_MAX_USERS);
    if (n > 0) {
        en.user_count = n;
    } else {
        ent_load_demo_users(&en);
        storage_save_users(en.users, en.user_count);
        EN_SAY("demo roster created (%d users)", en.user_count);
    }
    scms_schedule_t s;
    if (storage_nvs_get("sched", &s, sizeof s) && ent_schedule_valid(&s)) en.schedule = s;
    else {
        en.schedule.open_min = DEFAULT_OPEN_MIN;
        en.schedule.close_min = DEFAULT_CLOSE_MIN;
        en.schedule.days_mask = DEFAULT_DAYS_MASK;
    }
    en_restore_inside();
    kp_menu_init(&en_kp);
    auth_session_init(&en_kp_sess, LOGIN_FAIL_LIMIT, LOGIN_LOCK_MS, KEYPAD_IDLE_MS);

    /* I2C devices: LCD and RTC share one bus */
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc, &en_i2c) != ESP_OK) en_i2c = NULL;
    if (display_init(en_i2c)) {
        const char boot[4][21] = { "CoCIS Smart Control", "and Monitoring", "Starting...", "" };
        display_write(boot);
    }
    clock_init(en_i2c);

    esp_err_t e = mfrc522_init();
    if (e != ESP_OK) EN_SAY("warning: RFID readers not found (0x%x); console 'tap' still works", e);
    if ((e = en_gpio_init()) != ESP_OK) return e;

    if (xTaskCreate(en_access_task, "access", 4096, NULL, 6, NULL) != pdPASS ||
        xTaskCreate(en_rfid_task, "rfid", 3584, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(en_keypad_task, "keypad", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(en_coordinator_task, "coord", 4096, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    en_publish();
    dashboard_start();   /* optional: Wi-Fi, SNTP and web page */
    storage_log(LOG_EVENT, "INFO,SYSTEM,boot (SD %s, RTC %s, LCD %s, readers %d/%d)", storage_sd_ok() ? "ok" : "missing",
                clock_rtc_ok() ? "ok" : "missing", display_ok() ? "ok" : "missing", mfrc522_ready(0), mfrc522_ready(1));
    char ts[24];
    clock_now_str(ts, sizeof ts);
    EN_SAY("terminal ready; users=%d inside=%d clock=%s (%s)", en.user_count, en.inside_count, ts, clock_source());
    return ESP_OK;
}
