#include "cli/runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "auth/runtime.h"
#include "comms/bus.h"
#include "common/config.h"
#include "entrance/dashboard.h"
#include "entrance/runtime.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rooms/core.h"
#include "storage/runtime.h"

static auth_session_t cli_sess;

static int64_t cli_now_ms(void) { return esp_timer_get_time() / 1000; }

static bool cli_need(scms_role_t role)
{
    const char *why = NULL;
    if (auth_session_require(&cli_sess, role, cli_now_ms(), &why)) return true;
    printf("Access denied: %s.\n", why);
    return false;
}

static const char *cli_who(void) { return auth_op_name(cli_sess.op); }

static void cli_days(uint8_t mask, char out[8])
{
    const char *d = "SMTWTFS";
    for (int i = 0; i < 7; i++) out[i] = (mask & (1u << i)) ? d[i] : '-';
    out[7] = '\0';
}

static void cli_help(void)
{
    printf("\n============== CoCIS SCMS : MANAGEMENT CONSOLE ==============\n"
           " PUBLIC\n"
           "  help | status | rooms | energy | stats | schedule\n"
           "  login <admin|security> <pin>      start an operator session\n"
           "  logout | whoami\n"
           " SECURITY (after login)\n"
           "  who                                people inside now (roll-call)\n"
           "  logs [n]  | alerts [n]             last access records / alerts\n"
           "  release <uid>                      let a person out after hours\n"
           "  emergency on|off                   fire mode: doors open, lights 100%%\n"
           "  alarm off                          silence alarm, unlock readers\n"
           "  tap in|out <uid>                   simulate a card on a reader\n"
           "  users                              list registered cards\n"
           "  passwd <newpin>                    change your own PIN (4-8 digits)\n"
           " ADMIN\n"
           "  schedule set HH:MM HH:MM [0111111] opening hours [+ days, Sun first]\n"
           "  user add <uid> <role> <name>       role: STUDENT STAFF SECURITY ADMIN\n"
           "  user del|block|unblock <uid>\n"
           "  light <1|2> on|off|auto            remote room lighting\n"
           "  vacancy <seconds>                  room vacancy timeout (both rooms)\n"
           "  drill <1|2> <seconds>              fault drill: stop a room's heartbeat\n"
           "  confirm-empty                      reset roll-call after a physical check\n"
           "  time set YYYY-MM-DD HH:MM          set clock (also the RTC)\n"
           "  files | export <yymmdd>            SD card files / dump one day\n"
           "=============================================================\n");
}

static void cli_status(void)
{
    entrance_status_t s;
    entrance_get_status(&s);
    char days[8], ip[16];
    cli_days(s.schedule.days_mask, days);
    dashboard_ip(ip, sizeof ip);
    printf("\n--- BUILDING STATUS ---\n");
    printf(" Time          : %s (%s)\n", s.now, s.clock_source);
    printf(" Schedule      : %02d:%02d - %02d:%02d on %s (v%lu)\n", s.schedule.open_min / 60, s.schedule.open_min % 60,
           s.schedule.close_min / 60, s.schedule.close_min % 60, days, (unsigned long)s.schedule.version);
    printf(" Building      : %s\n", s.emergency ? "EMERGENCY - DOORS RELEASED" : s.open ? "OPEN" : "CLOSED");
    printf(" People inside : %d%s\n", s.inside, s.inside_known ? "" : " (uncertain: run 'who' and 'confirm-empty')");
    printf(" Doors         : entry %s, exit %s\n", s.door_open[0] ? "OPEN" : "locked", s.door_open[1] ? "OPEN" : "locked");
    printf(" Readers       : entry %s, exit %s\n",
           !s.reader_ready[0] ? "MISSING" : s.reader_locked[0] ? "LOCKED-OUT" : "ready",
           !s.reader_ready[1] ? "MISSING" : s.reader_locked[1] ? "LOCKED-OUT" : "ready");
    printf(" Alarm         : %s\n", s.alarm ? "ACTIVE" : "off");
    printf(" Devices       : SD %s, LCD %s, RTC %s, Wi-Fi %s (%s)\n", s.sd_ok ? "ok" : "MISSING (RAM log)",
           s.lcd_ok ? "ok" : "missing", s.rtc_ok ? "ok" : "missing", dashboard_wifi_up() ? "up" : "down", ip);
    printf(" Log queue     : %lu dropped, %lu write failures; bus drops %lu\n", (unsigned long)storage_dropped(),
           (unsigned long)storage_write_failures(), (unsigned long)bus_drops(NODE_ENTRANCE));
}

static void cli_rooms(void)
{
    printf("\n Room  Link     Seen   Occupancy  Lights  Lux     Mode    Events lost\n");
    for (int r = 1; r <= NUM_ROOMS; r++) {
        entrance_room_view_t v;
        entrance_get_room(r, &v);
        if (!v.seen) { printf("  %d    waiting for first heartbeat\n", r); continue; }
        printf("  %d    %-8s %4llds  %-10s %3d%%   %-7d %-7s %lu\n", r, v.online ? "online" : "OFFLINE",
               (long long)(v.age_ms / 1000), v.state.occupied ? "OCCUPIED" : "vacant", v.state.level, v.state.lux,
               room_mode_name((room_mode_t)v.state.mode), (unsigned long)v.lost_events);
    }
}

static void cli_energy(void)
{
    double used = 0, saved = 0;
    printf("\n--- ENERGY REPORT (since boot) ---\n");
    printf(" Room  Lights-on(h)  Used(kWh)  Saved(kWh)  Saved(UGX)\n");
    for (int r = 1; r <= NUM_ROOMS; r++) {
        entrance_room_view_t v;
        entrance_get_room(r, &v);
        double s = v.state.baseline_wh > v.state.wh ? v.state.baseline_wh - v.state.wh : 0;
        printf("  %d    %10.2f  %9.3f  %10.3f  %10.0f\n", r, v.state.on_s / 3600.0, v.state.wh / 1000.0, s / 1000.0,
               s / 1000.0 * TARIFF_UGX_PER_KWH);
        used += v.state.wh;
        saved += s;
    }
    double base = used + saved;
    printf(" TOTAL              %9.3f  %10.3f  %10.0f\n", used / 1000.0, saved / 1000.0,
           saved / 1000.0 * TARIFF_UGX_PER_KWH);
    printf(" Savings against lights-on-all-opening-hours: %.1f%% (tariff UGX %.0f/kWh)\n",
           base > 0 ? saved * 100.0 / base : 0.0, TARIFF_UGX_PER_KWH);
}

static void cli_stats(void)
{
    entrance_status_t s;
    entrance_get_status(&s);
    printf("\n Granted: %lu  Denied: %lu  Alerts: %lu  Peak inside: %lu  Now inside: %d\n", (unsigned long)s.granted,
           (unsigned long)s.denied, (unsigned long)s.alerts, (unsigned long)s.peak, s.inside);
}

static void cli_users(bool inside_only)
{
    static scms_user_t u[SCMS_MAX_USERS];
    int n = entrance_copy_users(u, SCMS_MAX_USERS), count = 0;
    time_t now = time(NULL);
    printf("\n %-15s %-9s %-8s %-10s %s\n", "UID", "Role", "Status", inside_only ? "Inside for" : "Inside", "Name");
    for (int i = 0; i < n; i++) {
        if (inside_only && !u[i].inside) continue;
        char in[16];
        if (inside_only) {
            long m = u[i].since ? (long)(now - u[i].since) / 60 : 0;
            snprintf(in, sizeof in, "%ldh %02ldm", m / 60, m % 60);
        } else {
            snprintf(in, sizeof in, "%s", u[i].inside ? "yes" : "no");
        }
        printf(" %-15s %-9s %-8s %-10s %s\n", u[i].uid, scms_role_name(u[i].role), u[i].blocked ? "BLOCKED" : "active",
               in, u[i].name);
        count++;
    }
    printf(" %d %s\n", count, inside_only ? "inside" : "registered");
}

static void cli_upper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void cli_login(char *user, char *pin)
{
    int op = auth_check(user, pin);
    auth_login_t r = auth_session_login(&cli_sess, op, cli_now_ms());
    if (r == AUTH_OK) {
        printf("Welcome %s (%s). Session ends after %d min idle.\n", auth_op_name(op),
               scms_role_name(auth_op_role(op)), SESSION_IDLE_MS / 60000);
        storage_log(LOG_EVENT, "INFO,CONSOLE,login %s", auth_op_name(op));
    } else if (r == AUTH_LOCKED) {
        printf("Console locked after failed logins. Try again later.\n");
    } else if (r == AUTH_LOCKED_NOW) {
        printf("Invalid user or PIN.\n");
        entrance_alert("HIGH", "CONSOLE", "%d failed logins (user '%s'): console locked %ds", LOGIN_FAIL_LIMIT, user,
                       LOGIN_LOCK_MS / 1000);
    } else {
        printf("Invalid user or PIN.\n");
        storage_log(LOG_EVENT, "WARN,CONSOLE,failed login '%s'", user);
    }
}

static void cli_schedule(int argc, char **argv)
{
    if (argc == 1) {
        entrance_status_t s;
        entrance_get_status(&s);
        char d[8];
        cli_days(s.schedule.days_mask, d);
        printf("Open %02d:%02d to %02d:%02d on %s (v%lu)\n", s.schedule.open_min / 60, s.schedule.open_min % 60,
               s.schedule.close_min / 60, s.schedule.close_min % 60, d, (unsigned long)s.schedule.version);
        return;
    }
    if (argc < 4 || strcmp(argv[1], "set")) { printf("usage: schedule set HH:MM HH:MM [0111111]\n"); return; }
    if (!cli_need(ROLE_ADMIN)) return;
    int o = scms_parse_hhmm(argv[2]), c = scms_parse_hhmm(argv[3]), mask = -1;
    if (argc >= 5) {
        if (strlen(argv[4]) != 7 || strspn(argv[4], "01") != 7) { printf("Days must be 7 digits, Sunday first.\n"); return; }
        mask = 0;
        for (int i = 0; i < 7; i++) if (argv[4][i] == '1') mask |= 1 << i;
    }
    if (o < 0 || c < 0 || entrance_set_schedule((uint16_t)o, (uint16_t)c, mask, cli_who()) != ESP_OK)
        printf("Invalid schedule (times HH:MM, open and close must differ, at least one day).\n");
    else
        printf("Schedule updated.\n");
}

static void cli_user(int argc, char **argv)
{
    if (!cli_need(ROLE_ADMIN)) return;
    if (argc >= 5 && !strcmp(argv[1], "add")) {
        int role = scms_role_parse(argv[3]);
        char name[SCMS_NAME_MAX] = "";
        for (int i = 4; i < argc; i++) {
            strncat(name, argv[i], sizeof name - strlen(name) - 1);
            if (i < argc - 1 && strlen(name) < sizeof name - 1) strcat(name, " ");
        }
        if (role < 0) { printf("Role must be STUDENT, STAFF, SECURITY or ADMIN.\n"); return; }
        if (!entrance_user_add(argv[2], (scms_role_t)role, name, cli_who()))
            printf("Not added: UID must be 8, 14 or 20 hex digits and not already registered (max %d users).\n",
                   SCMS_MAX_USERS);
        else
            printf("Registered %s as %s.\n", name, scms_role_name((scms_role_t)role));
    } else if (argc == 3 && !strcmp(argv[1], "del")) {
        printf(entrance_user_remove(argv[2], cli_who()) ? "Removed.\n" : "Unknown UID, or the person is still inside.\n");
    } else if (argc == 3 && (!strcmp(argv[1], "block") || !strcmp(argv[1], "unblock"))) {
        bool b = !strcmp(argv[1], "block");
        printf(entrance_user_block(argv[2], b, cli_who()) ? "%s is now %s.\n" : "Unknown UID %s.\n", argv[2],
               b ? "BLOCKED" : "active");
    } else {
        printf("usage: user add <uid> <role> <name> | user del|block|unblock <uid>\n");
    }
}

static void cli_time(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "set")) {
        if (!cli_need(ROLE_ADMIN)) return;
        struct tm tm = { 0 };
        int m = scms_parse_hhmm(argv[3]);
        if (sscanf(argv[2], "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3 || m < 0 || tm.tm_year < 2025) {
            printf("usage: time set YYYY-MM-DD HH:MM\n");
            return;
        }
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_hour = m / 60;
        tm.tm_min = m % 60;
        tm.tm_isdst = -1;
        entrance_set_time((int64_t)mktime(&tm), cli_who());
    }
    entrance_status_t s;
    entrance_get_status(&s);
    printf("%s (%s)\n", s.now, s.clock_source);
}

static void cli_handle(char *line)
{
    char *argv[10];
    int argc = 0;
    char *save = NULL;
    for (char *t = strtok_r(line, " ", &save); t && argc < 10; t = strtok_r(NULL, " ", &save)) argv[argc++] = t;
    if (!argc) return;
    const char *c = argv[0];

    if (!strcmp(c, "help") || !strcmp(c, "?")) cli_help();
    else if (!strcmp(c, "status")) cli_status();
    else if (!strcmp(c, "rooms")) cli_rooms();
    else if (!strcmp(c, "energy")) cli_energy();
    else if (!strcmp(c, "stats")) cli_stats();
    else if (!strcmp(c, "schedule")) cli_schedule(argc, argv);
    else if (!strcmp(c, "login") && argc == 3) cli_login(argv[1], argv[2]);
    else if (!strcmp(c, "logout")) {
        if (cli_sess.op >= 0) storage_log(LOG_EVENT, "INFO,CONSOLE,logout %s", cli_who());
        auth_session_logout(&cli_sess);
        printf("Logged out.\n");
    } else if (!strcmp(c, "whoami")) {
        printf("%s\n", auth_session_active(&cli_sess, cli_now_ms()) ? cli_who() : "(not logged in)");
    }
    /* ---- security ---- */
    else if (!strcmp(c, "who")) { if (cli_need(ROLE_SECURITY)) cli_users(true); }
    else if (!strcmp(c, "users")) { if (cli_need(ROLE_SECURITY)) cli_users(false); }
    else if (!strcmp(c, "logs")) { if (cli_need(ROLE_SECURITY)) storage_print_recent(LOG_ACCESS, argc > 1 ? atoi(argv[1]) : 10); }
    else if (!strcmp(c, "alerts")) { if (cli_need(ROLE_SECURITY)) storage_print_recent(LOG_EVENT, argc > 1 ? atoi(argv[1]) : 10); }
    else if (!strcmp(c, "release") && argc == 2) {
        if (cli_need(ROLE_SECURITY) && !entrance_release(argv[1], cli_who())) printf("That UID is not inside.\n");
    } else if (!strcmp(c, "emergency") && argc == 2) {
        if (cli_need(ROLE_SECURITY)) entrance_set_emergency(!strcmp(argv[1], "on"), cli_who());
    } else if (!strcmp(c, "alarm") && argc == 2 && !strcmp(argv[1], "off")) {
        if (cli_need(ROLE_SECURITY)) entrance_alarm_off(cli_who());
    } else if (!strcmp(c, "tap") && argc == 3) {
        if (!cli_need(ROLE_SECURITY)) return;
        cli_upper(argv[2]);
        scms_point_t p = !strcmp(argv[1], "out") ? POINT_EXIT : POINT_ENTRY;
        if (!entrance_simulate_scan(p, argv[2])) printf("UID must be 8, 14 or 20 hex digits.\n");
    } else if (!strcmp(c, "passwd") && argc == 2) {
        if (!cli_need(ROLE_SECURITY)) return;
        if (auth_set_pin(cli_sess.op, argv[1])) {
            printf("PIN changed.\n");
            storage_log(LOG_EVENT, "INFO,CONSOLE,PIN changed for %s", cli_who());
        } else {
            printf("PIN must be 4 to 8 digits.\n");
        }
    }
    /* ---- admin ---- */
    else if (!strcmp(c, "user")) cli_user(argc, argv);
    else if (!strcmp(c, "light") && argc == 3) {
        if (!cli_need(ROLE_ADMIN)) return;
        int room = atoi(argv[1]);
        uint8_t mode = !strcmp(argv[2], "on") ? ROOM_MODE_MANUAL_ON : !strcmp(argv[2], "off") ? ROOM_MODE_FORCED_OFF
                       : !strcmp(argv[2], "auto") ? ROOM_MODE_AUTO : BUS_NO_CHANGE;
        if (mode == BUS_NO_CHANGE || !entrance_room_command(room, mode, 0, 0, cli_who()))
            printf("usage: light <1|2> on|off|auto\n");
        else
            printf("Room %d: light %s sent.\n", room, argv[2]);
    } else if (!strcmp(c, "vacancy") && argc == 2) {
        if (!cli_need(ROLE_ADMIN)) return;
        int s = atoi(argv[1]);
        if (s < 5 || s > 3600) { printf("Use 5 to 3600 seconds.\n"); return; }
        for (int r = 1; r <= NUM_ROOMS; r++) entrance_room_command(r, BUS_NO_CHANGE, (uint16_t)s, 0, cli_who());
        printf("Vacancy timeout %d s sent to both rooms.\n", s);
    } else if (!strcmp(c, "drill") && argc == 3) {
        if (!cli_need(ROLE_ADMIN)) return;
        int room = atoi(argv[1]), s = atoi(argv[2]);
        if (s < 1 || s > 600 || !entrance_room_command(room, BUS_NO_CHANGE, 0, (uint16_t)s, cli_who()))
            printf("usage: drill <1|2> <1-600 seconds>\n");
    } else if (!strcmp(c, "confirm-empty")) {
        if (cli_need(ROLE_ADMIN)) entrance_confirm_empty(cli_who());
    } else if (!strcmp(c, "time")) cli_time(argc, argv);
    else if (!strcmp(c, "files")) { if (cli_need(ROLE_ADMIN)) storage_list_files(); }
    else if (!strcmp(c, "export") && argc == 2) { if (cli_need(ROLE_ADMIN)) storage_export(argv[1]); }
    else printf("Unknown command '%s'. Type help.\n", c);
}

static void cli_task(void *arg)
{
    (void)arg;
    char line[128];
    int len = 0;
    cli_help();
    printf("> ");
    fflush(stdout);
    for (;;) {
        int r = fgetc(stdin);
        if (r == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        char ch = (char)r;
        if (ch == '\r' || ch == '\n') {
            printf("\n");
            line[len] = '\0';
            if (len) cli_handle(line);
            len = 0;
            printf("> ");
            fflush(stdout);
        } else if ((ch == 8 || ch == 127) && len > 0) {
            len--;
            printf("\b \b");
            fflush(stdout);
        } else if (isprint((unsigned char)ch) && len < (int)sizeof line - 1) {
            line[len++] = ch;
            /* mask the PIN after "login <user> " and "passwd " */
            int spaces = 0;
            for (int i = 0; i < len; i++) spaces += line[i] == ' ';
            bool mask = ch != ' ' && ((!strncmp(line, "login", 5) && spaces >= 2) || (!strncmp(line, "passwd", 6) && spaces >= 1));
            putchar(mask ? '*' : ch);
            fflush(stdout);
        }
    }
}

esp_err_t cli_start(void)
{
    auth_session_init(&cli_sess, LOGIN_FAIL_LIMIT, LOGIN_LOCK_MS, SESSION_IDLE_MS);
    return xTaskCreate(cli_task, "console", 6144, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
