#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

/*
 * One place for every pin and tuning value.
 * The pin map matches diagram.json (board-esp32-devkit-c-v4).
 * See docs/INTERFACES.md for the wiring table.
 */

/* ---------------- shared SPI bus: SD card + two MFRC522 readers ------------ */
#define PIN_SPI_SCK          18
#define PIN_SPI_MOSI         23
#define PIN_SPI_MISO         19
#define PIN_RFID_ENTRY_SS    5    /* left reader, people coming in   */
#define PIN_RFID_EXIT_SS     13   /* right reader, people going out  */
#define PIN_SD_CS            2    /* 10k pull-up in diagram.json     */
/* MFRC522 RST is tied to 3V3; the driver uses the soft-reset command. */

/* ---------------- shared I2C bus: LCD 20x4 + DS1307 RTC ------------------- */
#define PIN_I2C_SDA          21
#define PIN_I2C_SCL          22
#define LCD_I2C_ADDR         0x27
#define RTC_I2C_ADDR         0x68

/* ---------------- lecture rooms -------------------------------------------- */
#define PIN_PIR_ROOM1        34   /* input-only pin                  */
#define PIN_PIR_ROOM2        35   /* input-only pin                  */
#define PIN_LDR_DAYLIGHT     36   /* VP, ADC1 channel 0, shared      */
#define LDR_ADC_CHANNEL      0    /* ADC_CHANNEL_0                   */
#define PIN_LIGHT_ROOM1      26   /* PWM, dimmable light bank        */
#define PIN_LIGHT_ROOM2      25

/* ---------------- entrance outputs ----------------------------------------- */
#define PIN_SERVO_ENTRY      27   /* entry door lock (servo)         */
#define PIN_SERVO_EXIT       12   /* exit door lock (10k pull-down)  */
#define PIN_BUZZER           4
#define PIN_EMERGENCY_BTN    39   /* VN, break-glass, 10k pull-up    */

/* ---------------- 4x3 keypad ----------------------------------------------- */
#define PIN_KP_R1            14
#define PIN_KP_R2            15
#define PIN_KP_R3            16
#define PIN_KP_R4            17
#define PIN_KP_C1            32
#define PIN_KP_C2            33
#define PIN_KP_C3            0

/* ---------------- network (optional, system works offline) ---------------- */
/* Set to 0 to leave out Wi-Fi, SNTP and the web dashboard (smaller build). */
#ifndef SCMS_ENABLE_NETWORK
#define SCMS_ENABLE_NETWORK  1
#endif
#define WIFI_SSID            "Wokwi-GUEST"
#define WIFI_PASS            ""
#define WIFI_CHANNEL         6
#define SNTP_SERVER_1        "pool.ntp.org"
#define TZ_STRING            "EAT-3"             /* Kampala, UTC+3 */

/* ---------------- access control ------------------------------------------- */
#define DOOR_OPEN_MS         3000
#define SCAN_REPEAT_MS       1500  /* same card on same reader ignored    */
#define FAIL_LIMIT           3     /* denied scans ...                    */
#define FAIL_WINDOW_MS       60000 /* ... inside this window ...          */
#define READER_LOCK_MS       30000 /* ... lock that reader for this long  */
#define DEFAULT_OPEN_MIN     (8 * 60)
#define DEFAULT_CLOSE_MIN    (18 * 60)
#define DEFAULT_DAYS_MASK    0x7E  /* Monday to Saturday (bit0 = Sunday) */

/* ---------------- operator login ------------------------------------------- */
#define LOGIN_FAIL_LIMIT     3
#define LOGIN_LOCK_MS        60000
#define SESSION_IDLE_MS      300000
#define KEYPAD_IDLE_MS       30000

/* ---------------- lighting and energy -------------------------------------- */
#define VACANT_AFTER_S       30    /* no motion this long => grace period */
#define GRACE_S              10    /* lights dim before switching off     */
#define GRACE_LEVEL          20    /* % brightness during grace           */
#define OVERRIDE_S           1800  /* manual ON lasts 30 minutes          */
#define LUX_BRIGHT           500   /* daylight enough: lights off         */
#define LUX_MEDIUM           200   /* some daylight: lights 50 %          */
#define ROOM_LOAD_W          288.0 /* 2 banks x 4 tubes x 36 W            */
#define TARIFF_UGX_PER_KWH   756.0 /* update to the current UMEME tariff  */

/* ---------------- coordination --------------------------------------------- */
#define ROOM_HEARTBEAT_MS    2000
#define ROOM_TIMEOUT_MS      8000  /* missing heartbeats => room OFFLINE   */
#define NUM_ROOMS            2

/* Status lines use printf: some Wokwi ESP-IDF builds hide ESP_LOGI output. */
#endif
