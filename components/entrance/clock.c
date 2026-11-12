#include "entrance/clock.h"

#include <stdio.h>
#include <sys/time.h>
#include "common/config.h"

static i2c_master_dev_handle_t clk_rtc;
static bool clk_rtc_ok;
static const char *clk_src = "none";

static uint8_t clk_bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t clk_bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool clock_valid(void) { return time(NULL) > 1735689600; }   /* after 1 Jan 2025 */
bool clock_rtc_ok(void) { return clk_rtc_ok; }
const char *clock_source(void) { return clock_valid() ? clk_src : "unsynced"; }

static bool clk_rtc_read(time_t *out)
{
    uint8_t reg = 0, r[7];
    if (i2c_master_transmit_receive(clk_rtc, &reg, 1, r, 7, 50) != ESP_OK) return false;
    if (r[0] & 0x80) return false;                 /* oscillator halted */
    struct tm tm = { 0 };
    tm.tm_sec = clk_bcd2bin(r[0] & 0x7F);
    tm.tm_min = clk_bcd2bin(r[1]);
    tm.tm_hour = clk_bcd2bin(r[2] & 0x3F);
    tm.tm_mday = clk_bcd2bin(r[4]);
    tm.tm_mon = clk_bcd2bin(r[5] & 0x1F) - 1;
    tm.tm_year = clk_bcd2bin(r[6]) + 100;
    tm.tm_isdst = -1;
    *out = mktime(&tm);                            /* RTC holds local time */
    return *out > 1735689600;
}

static void clk_rtc_write_now(void)
{
    if (!clk_rtc_ok) return;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    uint8_t b[8] = { 0, clk_bin2bcd(tm.tm_sec), clk_bin2bcd(tm.tm_min), clk_bin2bcd(tm.tm_hour),
                     (uint8_t)(tm.tm_wday + 1), clk_bin2bcd(tm.tm_mday), clk_bin2bcd(tm.tm_mon + 1),
                     clk_bin2bcd(tm.tm_year - 100) };
    i2c_master_transmit(clk_rtc, b, sizeof b, 50);
}

void clock_init(i2c_master_bus_handle_t bus)
{
    setenv("TZ", TZ_STRING, 1);
    tzset();
    if (!bus || i2c_master_probe(bus, RTC_I2C_ADDR, 50) != ESP_OK) {
        printf("[CLOCK] DS1307 not found, waiting for SNTP or 'time set'\n");
        return;
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RTC_I2C_ADDR, .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &dc, &clk_rtc) != ESP_OK) return;
    clk_rtc_ok = true;
    time_t t;
    if (clk_rtc_read(&t)) {
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        clk_src = "RTC";
        char s[24];
        clock_now_str(s, sizeof s);
        printf("[CLOCK] loaded from DS1307 RTC: %s\n", s);
    } else {
        printf("[CLOCK] RTC has no valid time yet\n");
    }
}

void clock_set_manual(time_t t)
{
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);
    clk_src = "manual";
    clk_rtc_write_now();
}

void clock_sntp_synced(void)
{
    clk_src = "SNTP";
    clk_rtc_write_now();
    printf("[CLOCK] synchronised by SNTP\n");
}

void clock_now_str(char *out, size_t n)
{
    if (!clock_valid()) { snprintf(out, n, "unsynced"); return; }
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm);
}

bool clock_local(int *wday, int *minute)
{
    if (!clock_valid()) return false;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    *wday = tm.tm_wday;
    *minute = tm.tm_hour * 60 + tm.tm_min;
    return true;
}
