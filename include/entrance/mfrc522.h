#ifndef ENTRANCE_MFRC522_H
#define ENTRANCE_MFRC522_H

/*
 * Minimal MFRC522 driver over the shared SPI bus: detects an ISO/IEC 14443A
 * card and reads its UID (4, 7 or 10 bytes). Access rules live in core.c.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MFRC_READER_ENTRY 0
#define MFRC_READER_EXIT 1

/* Call after storage_start() (it initialises the SPI bus). */
esp_err_t mfrc522_init(void);
bool mfrc522_ready(int reader);
/* true when a card answered; uid[] gets its bytes, *len 4, 7 or 10. */
bool mfrc522_read_uid(int reader, uint8_t uid[10], uint8_t *len);

#endif
