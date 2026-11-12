#ifndef ENTRANCE_DISPLAY_H
#define ENTRANCE_DISPLAY_H

/* 20x4 character LCD behind a PCF8574 I2C backpack (address 0x27). */
#include <stdbool.h>
#include "driver/i2c_master.h"

bool display_init(i2c_master_bus_handle_t bus);
bool display_ok(void);
/* Writes only rows that changed, so it is cheap to call often. */
void display_write(const char rows[4][21]);

#endif
