#include "storage/runtime.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "common/config.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdmmc_cmd.h"

#define ST_MOUNT "/sdcard"
#define ST_RING_N 32
#define ST_LINE 160

typedef struct {
    storage_file_t file;
    char line[ST_LINE];
} st_rec_t;

static SemaphoreHandle_t st_spi_mtx, st_ring_mtx;
static QueueHandle_t st_queue;
static bool st_sd_ok;
static uint32_t st_drops, st_fails;
static char st_ring[3][ST_RING_N][ST_LINE];
static int st_ring_head[3], st_ring_count[3];

static const char *const ST_HEADER[3] = {
    "time,door,uid,name,result,reason,inside\n",
    "time,level,source,message\n",
    "time,room,on_hours,used_wh,saved_wh,saved_ugx\n",
};

void storage_spi_lock(void) { xSemaphoreTake(st_spi_mtx, portMAX_DELAY); }
void storage_spi_unlock(void) { xSemaphoreGive(st_spi_mtx); }
bool storage_sd_ok(void) { return st_sd_ok; }
uint32_t storage_dropped(void) { return st_drops; }
uint32_t storage_write_failures(void) { return st_fails; }

static bool st_time_valid(void) { return time(NULL) > 1735689600; }   /* after 1 Jan 2025 */

static void st_date_tag(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    if (st_time_valid()) strftime(out, n, "%y%m%d", &tm);
    else snprintf(out, n, "000000");
}

static void st_path(storage_file_t f, const char *tag, char *out, size_t n)
{
    static const char PREFIX[3] = { 'A', 'E', 'N' };
    snprintf(out, n, ST_MOUNT "/%c%.6s.CSV", PREFIX[f], tag);
}

static void st_ring_push(storage_file_t f, const char *line)
{
    xSemaphoreTake(st_ring_mtx, portMAX_DELAY);
    snprintf(st_ring[f][st_ring_head[f]], ST_LINE, "%s", line);
    st_ring_head[f] = (st_ring_head[f] + 1) % ST_RING_N;
    if (st_ring_count[f] < ST_RING_N) st_ring_count[f]++;
    xSemaphoreGive(st_ring_mtx);
}

void storage_log(storage_file_t f, const char *fmt, ...)
{
    st_rec_t r = { .file = f };
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    int n;
    if (st_time_valid()) n = (int)strftime(r.line, sizeof r.line, "%Y-%m-%d %H:%M:%S,", &tm);
    else n = snprintf(r.line, sizeof r.line, "T+%llds,", (long long)(esp_timer_get_time() / 1000000));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r.line + n, sizeof r.line - (size_t)n, fmt, ap);
    va_end(ap);
    st_ring_push(f, r.line);
    if (!st_queue || xQueueSend(st_queue, &r, 0) != pdTRUE) st_drops++;   /* kept in RAM ring */
}

static void st_logger_task(void *arg)
{
    (void)arg;
    st_rec_t r;
    for (;;) {
        if (xQueueReceive(st_queue, &r, portMAX_DELAY) != pdTRUE || !st_sd_ok) continue;
        char tag[8], path[32];
        st_date_tag(tag, sizeof tag);
        st_path(r.file, tag, path, sizeof path);
        storage_spi_lock();
        struct stat st;
        bool fresh = stat(path, &st) != 0;
        FILE *fp = fopen(path, "a");
        if (fp) {
            if (fresh) fputs(ST_HEADER[r.file], fp);
            fprintf(fp, "%s\n", r.line);
            if (fclose(fp) != 0) st_fails++;
        } else {
            st_fails++;
        }
        storage_spi_unlock();
    }
}

static bool st_mount(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = true,   /* blank simulated card */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    esp_err_t e = esp_vfs_fat_sdspi_mount(ST_MOUNT, &host, &slot, &mc, &card);
    if (e != ESP_OK) {
        printf("[STORAGE] SD mount failed (0x%x): records kept in RAM only\n", e);
        return false;
    }
    printf("[STORAGE] SD card mounted (%s)\n", card->cid.name);
    return true;
}

esp_err_t storage_start(void)
{
    st_spi_mtx = xSemaphoreCreateMutex();
    st_ring_mtx = xSemaphoreCreateMutex();
    st_queue = xQueueCreate(24, sizeof(st_rec_t));
    if (!st_spi_mtx || !st_ring_mtx || !st_queue) return ESP_ERR_NO_MEM;

    /* The bus is shared: SD (here) and the MFRC522 readers (entrance). */
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SPI_MOSI, .miso_io_num = PIN_SPI_MISO, .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    esp_err_t e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) return e;
    /* Keep both RFID readers deselected while the SD card starts up,
       otherwise they could answer on MISO during SD initialisation. */
    gpio_config_t cs = {
        .pin_bit_mask = (1ULL << PIN_RFID_ENTRY_SS) | (1ULL << PIN_RFID_EXIT_SS), .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cs);
    gpio_set_level(PIN_RFID_ENTRY_SS, 1);
    gpio_set_level(PIN_RFID_EXIT_SS, 1);
    storage_spi_lock();
    st_sd_ok = st_mount();
    storage_spi_unlock();
    if (xTaskCreate(st_logger_task, "sd_logger", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

/* Prints the tail of a file without loading it all (large-file safe). */
static void st_print_tail(const char *path, int n)
{
    static char buf[4097];
    storage_spi_lock();
    FILE *fp = fopen(path, "r");
    size_t got = 0;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        long start = size > 4096 ? size - 4096 : 0;
        fseek(fp, start, SEEK_SET);
        got = fread(buf, 1, 4096, fp);
        fclose(fp);
    }
    storage_spi_unlock();
    if (!fp) { printf("  (no file %s yet)\n", path); return; }
    buf[got] = '\0';
    int lines = 0;
    for (long i = (long)got - 1; i >= 0; i--) {
        if (buf[i] == '\n' && i != (long)got - 1 && ++lines == n) {
            printf("%s", buf + i + 1);
            return;
        }
    }
    printf("%s", buf);
}

void storage_print_recent(storage_file_t f, int n)
{
    if (n <= 0 || n > 50) n = 10;
    if (st_sd_ok) {
        char tag[8], path[32];
        st_date_tag(tag, sizeof tag);
        st_path(f, tag, path, sizeof path);
        vTaskDelay(pdMS_TO_TICKS(50));   /* let the logger flush */
        printf(" Last %d lines of %s\n", n, path);
        st_print_tail(path, n);
        return;
    }
    printf(" SD card missing. Last %d records from RAM:\n", n);
    xSemaphoreTake(st_ring_mtx, portMAX_DELAY);
    int cnt = st_ring_count[f] < n ? st_ring_count[f] : n;
    for (int i = cnt; i > 0; i--) printf("  %s\n", st_ring[f][(st_ring_head[f] - i + ST_RING_N) % ST_RING_N]);
    if (!cnt) printf("  (none)\n");
    xSemaphoreGive(st_ring_mtx);
}

void storage_list_files(void)
{
    if (!st_sd_ok) { printf(" SD card not mounted.\n"); return; }
    storage_spi_lock();
    DIR *d = opendir(ST_MOUNT);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            char p[300];
            struct stat st;
            snprintf(p, sizeof p, ST_MOUNT "/%s", e->d_name);
            if (stat(p, &st) == 0) printf("  %-14s %8ld bytes\n", e->d_name, (long)st.st_size);
        }
        closedir(d);
    }
    storage_spi_unlock();
}

void storage_export(const char *tag)
{
    if (!st_sd_ok) { printf(" SD card not mounted.\n"); return; }
    for (int f = 0; f < 3; f++) {
        char path[32], line[ST_LINE + 8];
        st_path((storage_file_t)f, tag, path, sizeof path);
        storage_spi_lock();
        FILE *fp = fopen(path, "r");
        if (fp) {
            printf("----- BEGIN %s -----\n", path);
            while (fgets(line, sizeof line, fp)) printf("%s", line);
            printf("----- END %s -----\n", path);
            fclose(fp);
        } else {
            printf("  (no file %s)\n", path);
        }
        storage_spi_unlock();
    }
}

/* ---------------- NVS ---------------------------------------------------- */
bool storage_nvs_get(const char *key, void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open("scms", NVS_READONLY, &h) != ESP_OK) return false;
    size_t got = len;
    esp_err_t e = nvs_get_blob(h, key, buf, &got);
    nvs_close(h);
    return e == ESP_OK && got == len;
}

bool storage_nvs_set(const char *key, const void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open("scms", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, key, buf, len) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

/* ---------------- roster ------------------------------------------------- */
typedef struct {
    char uid[SCMS_UID_HEX_MAX];
    char name[SCMS_NAME_MAX];
    uint8_t role;
    uint8_t blocked;
} st_user_rec_t;

static int st_load_csv(scms_user_t *users, int max)
{
    if (!st_sd_ok) return -1;
    int n = -1;
    storage_spi_lock();
    FILE *fp = fopen(ST_MOUNT "/USERS.CSV", "r");
    if (fp) {
        char line[96];
        n = 0;
        while (fgets(line, sizeof line, fp) && n < max) {
            char uid[24], name[40];
            int role, blk;
            line[strcspn(line, "\r\n")] = '\0';
            if (sscanf(line, "%23[^,],%d,%d,%39[^\n]", uid, &role, &blk, name) != 4) continue; /* header */
            if (role < ROLE_STUDENT || role > ROLE_ADMIN || !scms_uid_normalise(uid, users[n].uid)) continue;
            snprintf(users[n].name, sizeof users[n].name, "%.*s", (int)sizeof users[n].name - 1, name);
            users[n].role = (scms_role_t)role;
            users[n].blocked = blk != 0;
            users[n].inside = false;
            users[n].since = 0;
            n++;
        }
        fclose(fp);
    }
    storage_spi_unlock();
    return n > 0 ? n : -1;
}

static void st_save_csv(const scms_user_t *users, int n)
{
    if (!st_sd_ok) return;
    storage_spi_lock();
    FILE *fp = fopen(ST_MOUNT "/USERS.CSV", "w");
    if (fp) {
        fputs("uid,role,blocked,name\n", fp);
        for (int i = 0; i < n; i++)
            fprintf(fp, "%s,%d,%d,%s\n", users[i].uid, users[i].role, users[i].blocked, users[i].name);
        if (fclose(fp) != 0) st_fails++;
    } else {
        st_fails++;
    }
    storage_spi_unlock();
}

int storage_load_users(scms_user_t *users, int max)
{
    static st_user_rec_t recs[SCMS_MAX_USERS];
    uint16_t count = 0;
    if (storage_nvs_get("users_n", &count, sizeof count) && count > 0 && count <= SCMS_MAX_USERS &&
        (int)count <= max && storage_nvs_get("users", recs, sizeof(st_user_rec_t) * count)) {
        for (int i = 0; i < count; i++) {
            memset(&users[i], 0, sizeof users[i]);
            memcpy(users[i].uid, recs[i].uid, sizeof users[i].uid);
            memcpy(users[i].name, recs[i].name, sizeof users[i].name);
            users[i].uid[SCMS_UID_HEX_MAX - 1] = users[i].name[SCMS_NAME_MAX - 1] = '\0';
            users[i].role = (scms_role_t)recs[i].role;
            users[i].blocked = recs[i].blocked;
        }
        printf("[STORAGE] %d users loaded from NVS\n", count);
        return count;
    }
    int n = st_load_csv(users, max);   /* import path for a prepared card */
    if (n > 0) printf("[STORAGE] %d users imported from SD USERS.CSV\n", n);
    return n;
}

bool storage_save_users(const scms_user_t *users, int n)
{
    static st_user_rec_t recs[SCMS_MAX_USERS];
    if (n < 0 || n > SCMS_MAX_USERS) return false;
    memset(recs, 0, sizeof recs);
    for (int i = 0; i < n; i++) {
        memcpy(recs[i].uid, users[i].uid, sizeof recs[i].uid);
        memcpy(recs[i].name, users[i].name, sizeof recs[i].name);
        recs[i].role = (uint8_t)users[i].role;
        recs[i].blocked = users[i].blocked;
    }
    uint16_t count = (uint16_t)n;
    bool ok = storage_nvs_set("users", recs, sizeof(st_user_rec_t) * (size_t)(n ? n : 1)) &&
              storage_nvs_set("users_n", &count, sizeof count);
    st_save_csv(users, n);
    return ok;
}
