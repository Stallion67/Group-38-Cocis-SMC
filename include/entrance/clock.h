#ifndef ENTRANCE_CLOCK_H
#define ENTRANCE_CLOCK_H

/*
 * Wall clock for the schedule. Sources, best first:
 *   SNTP (when Wi-Fi works) -> written back to the DS1307 RTC
 *   DS1307 RTC (battery-backed, keeps local Kampala time)
 *   manual "time set" by an admin
 * Without any of these the clock is invalid and only security/admin
 * cards are accepted (rule NO_CLOCK).
 */
#include <stdbool.h>
#include <time.h>
#include "driver/i2c_master.h"

void clock_init(i2c_master_bus_handle_t bus);
bool clock_valid(void);
bool clock_rtc_ok(void);
const char *clock_source(void);
void clock_set_manual(time_t t);
/* Called from the SNTP callback. */
void clock_sntp_synced(void);
/* "YYYY-MM-DD HH:MM:SS" or "unsynced" */
void clock_now_str(char *out, size_t n);
/* local weekday (0 = Sunday) and minute of day; false if invalid */
bool clock_local(int *wday, int *minute);

#endif
