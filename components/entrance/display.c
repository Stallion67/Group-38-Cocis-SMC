#include "entrance/display.h"

#include <stdio.h>
#include <string.h>
#include "common/config.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* PCF8574 bits: P0 RS, P1 RW, P2 EN, P3 backlight, P4-P7 data D4-D7 */
static i2c_master_dev_handle_t lcd_dev;
static bool lcd_ready;
static char lcd_cache[4][21];

static void lcd_nibble(uint8_t nib, bool rs)
{
    uint8_t d = (uint8_t)((nib << 4) | (rs ? 0x01 : 0) | 0x08);
    uint8_t seq[2] = { (uint8_t)(d | 0x04), d };   /* pulse EN */
    i2c_master_transmit(lcd_dev, seq, 2, 50);
}

static void lcd_byte(uint8_t b, bool rs)
{
    lcd_nibble(b >> 4, rs);
    lcd_nibble(b & 0x0F, rs);
}

bool display_init(i2c_master_bus_handle_t bus)
{
    if (!bus || i2c_master_probe(bus, LCD_I2C_ADDR, 50) != ESP_OK) {
        printf("[LCD] not found at 0x%02X\n", LCD_I2C_ADDR);
        return false;
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = LCD_I2C_ADDR, .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &dc, &lcd_dev) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_nibble(0x03, false); esp_rom_delay_us(5000);
    lcd_nibble(0x03, false); esp_rom_delay_us(1000);
    lcd_nibble(0x03, false);
    lcd_nibble(0x02, false);              /* 4-bit mode           */
    lcd_byte(0x28, false);                /* 2-line logic, 5x8    */
    lcd_byte(0x0C, false);                /* display on, no cursor */
    lcd_byte(0x06, false);                /* left to right        */
    lcd_byte(0x01, false);                /* clear                */
    esp_rom_delay_us(3000);
    memset(lcd_cache, 0, sizeof lcd_cache);
    lcd_ready = true;
    return true;
}

bool display_ok(void) { return lcd_ready; }

void display_write(const char rows[4][21])
{
    static const uint8_t ROW_ADDR[4] = { 0x00, 0x40, 0x14, 0x54 };
    if (!lcd_ready) return;
    for (int r = 0; r < 4; r++) {
        char buf[21];
        snprintf(buf, sizeof buf, "%-20.20s", rows[r]);
        if (strcmp(buf, lcd_cache[r]) == 0) continue;
        memcpy(lcd_cache[r], buf, sizeof buf);
        lcd_byte((uint8_t)(0x80 | ROW_ADDR[r]), false);
        for (int i = 0; i < 20; i++) lcd_byte((uint8_t)buf[i], true);
    }
}
