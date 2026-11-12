#ifndef STORAGE_RUNTIME_H
#define STORAGE_RUNTIME_H

/*
 * SD card logging, RAM fallback and NVS settings.
 *
 * One logger task is the ONLY writer to the SD card. Other tasks call
 * storage_log(), which formats the line and puts it on a queue, so a slow
 * card never blocks a door. The SD card and both RFID readers share one
 * SPI bus; storage_spi_lock() serialises every user of that bus.
 *
 * Daily CSV files (8.3 names): Ayymmdd.CSV access, Eyymmdd.CSV events and
 * alerts, Nyymmdd.CSV energy snapshots, plus USERS.CSV (roster mirror).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "common/system.h"

typedef enum { LOG_ACCESS = 0, LOG_EVENT = 1, LOG_ENERGY = 2 } storage_file_t;

esp_err_t storage_start(void);
bool storage_sd_ok(void);
void storage_spi_lock(void);
void storage_spi_unlock(void);

void storage_log(storage_file_t f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Last n lines of today's file, or of the RAM ring when the SD is missing. */
void storage_print_recent(storage_file_t f, int n);
void storage_list_files(void);
void storage_export(const char *yymmdd);
uint32_t storage_dropped(void);
uint32_t storage_write_failures(void);

/* Roster: NVS is authoritative, SD USERS.CSV is a readable mirror and an
   import path (a USERS.CSV placed on a fresh card is loaded once). */
int storage_load_users(scms_user_t *users, int max);   /* -1: nothing saved */
bool storage_save_users(const scms_user_t *users, int n);

bool storage_nvs_get(const char *key, void *buf, size_t len);
bool storage_nvs_set(const char *key, const void *buf, size_t len);

#endif
