#ifndef ENTRANCE_DASHBOARD_H
#define ENTRANCE_DASHBOARD_H

/*
 * Optional network layer: Wi-Fi (Wokwi-GUEST), SNTP time and a small web
 * dashboard on port 80. Pages:
 *   GET  /             live status page (refreshes every 5 s)
 *   GET  /api/status   JSON for other tools
 *   POST /api/schedule pin=....&open=HH:MM&close=HH:MM  (admin PIN)
 * In VS Code + Wokwi the page opens through the Private IoT Gateway at
 * http://localhost:9080/ (wokwi.toml forwards port 80).
 * The system keeps working when the network is absent.
 */
#include <stdbool.h>

void dashboard_start(void);
bool dashboard_wifi_up(void);
void dashboard_ip(char *out, int n);

#endif
