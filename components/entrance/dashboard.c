#include "entrance/dashboard.h"

#include <stdio.h>
#include "common/config.h"

#if !SCMS_ENABLE_NETWORK
/* Network left out at build time: the system runs on the RTC clock only. */
void dashboard_start(void) { printf("[WEB] network disabled in config.h (SCMS_ENABLE_NETWORK 0)\n"); }
bool dashboard_wifi_up(void) { return false; }
void dashboard_ip(char *out, int n) { snprintf(out, (size_t)n, "-"); }
#else
#include <stdlib.h>
#include <string.h>
#include "auth/runtime.h"
#include "common/config.h"
#include "entrance/clock.h"
#include "entrance/runtime.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "rooms/core.h"

static volatile bool db_wifi;
static char db_ip_str[16] = "-";
static httpd_handle_t db_server;
static bool db_sntp_started;

bool dashboard_wifi_up(void) { return db_wifi; }
void dashboard_ip(char *out, int n) { snprintf(out, (size_t)n, "%s", db_ip_str); }

static void db_sntp_cb(struct timeval *tv)
{
    (void)tv;
    clock_sntp_synced();
}

/* ---------------- HTTP handlers -------------------------------------------- */
static esp_err_t db_status_json(httpd_req_t *req)
{
    static char js[900];
    entrance_status_t s;
    entrance_get_status(&s);
    int n = snprintf(js, sizeof js,
                     "{\"time\":\"%s\",\"clock\":\"%s\",\"open\":%s,\"emergency\":%s,\"alarm\":%s,"
                     "\"inside\":%d,\"inside_known\":%s,\"schedule\":{\"open\":\"%02d:%02d\",\"close\":\"%02d:%02d\","
                     "\"days\":%u,\"version\":%lu},\"granted\":%lu,\"denied\":%lu,\"alerts\":%lu,\"rooms\":[",
                     s.now, s.clock_source, s.open ? "true" : "false", s.emergency ? "true" : "false",
                     s.alarm ? "true" : "false", s.inside, s.inside_known ? "true" : "false",
                     s.schedule.open_min / 60, s.schedule.open_min % 60, s.schedule.close_min / 60,
                     s.schedule.close_min % 60, s.schedule.days_mask, (unsigned long)s.schedule.version,
                     (unsigned long)s.granted, (unsigned long)s.denied, (unsigned long)s.alerts);
    for (int r = 1; r <= NUM_ROOMS && n < (int)sizeof js; r++) {
        entrance_room_view_t v;
        entrance_get_room(r, &v);
        n += snprintf(js + n, sizeof js - (size_t)n,
                      "%s{\"room\":%d,\"online\":%s,\"occupied\":%s,\"light\":%d,\"mode\":\"%s\",\"lux\":%d,"
                      "\"used_wh\":%.1f,\"baseline_wh\":%.1f}",
                      r > 1 ? "," : "", r, v.online ? "true" : "false", v.state.occupied ? "true" : "false",
                      v.state.level, room_mode_name((room_mode_t)v.state.mode), v.state.lux, v.state.wh,
                      v.state.baseline_wh);
    }
    if (n < (int)sizeof js) snprintf(js + n, sizeof js - (size_t)n, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, js);
}

static esp_err_t db_page(httpd_req_t *req)
{
    static char html[2600];
    entrance_status_t s;
    entrance_get_status(&s);
    int n = snprintf(html, sizeof html,
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta http-equiv='refresh' content='5'>"
        "<meta name='viewport' content='width=device-width'><title>CoCIS SCMS</title><style>"
        "body{font-family:sans-serif;margin:16px;background:#f4f6f8}h1{font-size:20px}"
        ".c{background:#fff;border-radius:8px;padding:12px;margin:8px 0}"
        "td{padding:4px 10px}.bad{color:#b00020;font-weight:bold}.ok{color:#0a7d32}</style></head><body>"
        "<h1>CoCIS Smart Control and Monitoring</h1><div class='c'>"
        "<b>Time:</b> %s (%s)<br><b>Building:</b> <span class='%s'>%s</span><br>"
        "<b>People inside:</b> %d%s<br><b>Schedule:</b> %02d:%02d to %02d:%02d (v%lu)<br>"
        "<b>Scans:</b> %lu granted, %lu denied. <b>Alerts:</b> %lu%s</div>"
        "<div class='c'><table><tr><th>Room</th><th>Link</th><th>Occupancy</th><th>Lights</th><th>Mode</th>"
        "<th>Energy used (Wh)</th><th>Saved (Wh)</th></tr>",
        s.now, s.clock_source, s.emergency ? "bad" : "ok",
        s.emergency ? "EMERGENCY: doors released" : s.open ? "OPEN" : "CLOSED", s.inside,
        s.inside_known ? "" : " (needs roll-call check)", s.schedule.open_min / 60, s.schedule.open_min % 60,
        s.schedule.close_min / 60, s.schedule.close_min % 60, (unsigned long)s.schedule.version,
        (unsigned long)s.granted, (unsigned long)s.denied, (unsigned long)s.alerts,
        s.alarm ? " <span class='bad'>ALARM ACTIVE</span>" : "");
    for (int r = 1; r <= NUM_ROOMS && n < (int)sizeof html; r++) {
        entrance_room_view_t v;
        entrance_get_room(r, &v);
        double saved = v.state.baseline_wh > v.state.wh ? v.state.baseline_wh - v.state.wh : 0;
        n += snprintf(html + n, sizeof html - (size_t)n,
                      "<tr><td>%d</td><td class='%s'>%s</td><td>%s</td><td>%d%%</td><td>%s</td><td>%.1f</td>"
                      "<td>%.1f</td></tr>",
                      r, v.online ? "ok" : "bad", v.online ? "online" : "OFFLINE",
                      v.state.occupied ? "occupied" : "vacant", v.state.level,
                      room_mode_name((room_mode_t)v.state.mode), v.state.wh, saved);
    }
    if (n < (int)sizeof html)
        snprintf(html + n, sizeof html - (size_t)n,
                 "</table></div><div class='c'><form method='post' action='/api/schedule'>"
                 "<b>Change schedule (admin)</b><br>Open <input name='open' size='5' placeholder='08:00'> "
                 "Close <input name='close' size='5' placeholder='18:00'> PIN "
                 "<input name='pin' type='password' size='6'> <button>Save</button></form></div>"
                 "<p><a href='/api/status'>JSON</a></p></body></html>");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, html);
}

/* reads key=value from a form body; decodes %3A and '+' */
static bool db_form_value(const char *body, const char *key, char *out, size_t n)
{
    char raw[24];
    if (httpd_query_key_value(body, key, raw, sizeof raw) != ESP_OK) return false;
    size_t j = 0;
    for (size_t i = 0; raw[i] && j + 1 < n; i++) {
        if (raw[i] == '%' && raw[i + 1] && raw[i + 2]) {
            char hex[3] = { raw[i + 1], raw[i + 2], 0 };
            out[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            out[j++] = raw[i] == '+' ? ' ' : raw[i];
        }
    }
    out[j] = '\0';
    return true;
}

static esp_err_t db_schedule_post(httpd_req_t *req)
{
    char body[128] = "", pin[12], open_s[8], close_s[8];
    int len = req->content_len < (int)sizeof body - 1 ? req->content_len : (int)sizeof body - 1;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, (size_t)(len - got));
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[got] = '\0';
    const char *msg;
    if (!db_form_value(body, "pin", pin, sizeof pin) || !db_form_value(body, "open", open_s, sizeof open_s) ||
        !db_form_value(body, "close", close_s, sizeof close_s)) {
        msg = "Missing fields.";
    } else {
        int op = auth_check_any(pin);
        int o = scms_parse_hhmm(open_s), c = scms_parse_hhmm(close_s);
        if (op < 0 || auth_op_role(op) < ROLE_ADMIN) {
            msg = "Rejected: admin PIN required.";
            entrance_alert("MEDIUM", "WEB", "schedule change rejected: wrong PIN");
        } else if (o < 0 || c < 0 || entrance_set_schedule((uint16_t)o, (uint16_t)c, -1, "web:admin") != ESP_OK) {
            msg = "Rejected: invalid times.";
        } else {
            msg = "Schedule saved.";
        }
    }
    char page[256];
    snprintf(page, sizeof page, "<html><body><p>%s</p><p><a href='/'>Back</a></p></body></html>", msg);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, page);
}

static void db_start_server(void)
{
    if (db_server) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    if (httpd_start(&db_server, &cfg) != ESP_OK) {
        printf("[WEB] server failed to start\n");
        return;
    }
    httpd_uri_t pages[] = {
        { .uri = "/", .method = HTTP_GET, .handler = db_page },
        { .uri = "/api/status", .method = HTTP_GET, .handler = db_status_json },
        { .uri = "/api/schedule", .method = HTTP_POST, .handler = db_schedule_post },
    };
    for (size_t i = 0; i < sizeof pages / sizeof pages[0]; i++) httpd_register_uri_handler(db_server, &pages[i]);
    printf("[WEB] dashboard on http://%s/ (Wokwi gateway: http://localhost:9080/)\n", db_ip_str);
}

/* ---------------- Wi-Fi ---------------------------------------------------- */
static void db_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        db_wifi = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(db_ip_str, sizeof db_ip_str, IPSTR, IP2STR(&ev->ip_info.ip));
        db_wifi = true;
        printf("[WEB] Wi-Fi connected, IP %s\n", db_ip_str);
        if (!db_sntp_started) {
            db_sntp_started = true;
            esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER_1);
            sc.sync_cb = db_sntp_cb;
            if (esp_netif_sntp_init(&sc) != ESP_OK) printf("[CLOCK] SNTP start failed\n");
        }
        db_start_server();
    }
}

void dashboard_start(void)
{
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) {
        printf("[WEB] network stack unavailable, running offline\n");
        return;
    }
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wc) != ESP_OK) {
        printf("[WEB] Wi-Fi init failed, running offline\n");
        return;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, db_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, db_wifi_event, NULL);
    wifi_config_t cfg = { 0 };
    snprintf((char *)cfg.sta.ssid, sizeof cfg.sta.ssid, "%s", WIFI_SSID);
    snprintf((char *)cfg.sta.password, sizeof cfg.sta.password, "%s", WIFI_PASS);
    cfg.sta.channel = WIFI_CHANNEL;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
}
#endif /* SCMS_ENABLE_NETWORK */
