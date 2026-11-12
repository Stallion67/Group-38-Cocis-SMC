/*
 * CoCIS Smart Control and Monitoring System (SCMS)
 * Makerere University, College of Computing and Information Sciences
 *
 * One ESP32 runs the three control terminals as independent FreeRTOS tasks:
 *   Terminal 1  Lecture Room 1   (components/rooms)
 *   Terminal 2  Lecture Room 2   (components/rooms)
 *   Terminal 3  Main entrance    (components/entrance) - system coordinator
 * Terminals exchange messages only through components/comms (queues),
 * so each one could move to its own ESP32 with a network link later.
 *
 * Start order matters:
 *   NVS  -> storage (SPI bus + SD + logger) -> comms -> actuators
 *        -> rooms -> entrance (RFID, keypad, LCD, RTC, Wi-Fi) -> console
 */
#include <stdio.h>
#include "actuator/runtime.h"
#include "auth/runtime.h"
#include "cli/runtime.h"
#include "comms/bus.h"
#include "entrance/runtime.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "rooms/runtime.h"
#include "storage/runtime.h"

static void start_or_halt(const char *what, esp_err_t e)
{
    if (e == ESP_OK) return;
    printf("[SYSTEM] FATAL: %s failed (%s)\n", what, esp_err_to_name(e));
    ESP_ERROR_CHECK(e);
}

void app_main(void)
{
    printf("\n[SYSTEM] CoCIS Smart Control and Monitoring System starting\n");
    /* quiet library logs: our own [TAG] lines carry the story */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("sdspi_transaction", ESP_LOG_NONE);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* only on a layout change: the partition cannot be read at all */
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    start_or_halt("NVS", e);
    auth_runtime_init();

    start_or_halt("storage", storage_start());
    start_or_halt("message bus", bus_init() ? ESP_OK : ESP_ERR_NO_MEM);
    start_or_halt("actuators", actuator_start());
    start_or_halt("rooms", rooms_start());
    start_or_halt("entrance", entrance_start());
    start_or_halt("console", cli_start());
    printf("[SYSTEM] all terminals running\n");
}
