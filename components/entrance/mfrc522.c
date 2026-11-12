#include "entrance/mfrc522.h"

#include <stdio.h>
#include <string.h>
#include "common/config.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage/runtime.h"

/*
 * Card dialogue (ISO/IEC 14443-3 type A):
 *   REQA (7 bits)            -> ATQA        "is a card there?"
 *   ANTICOLL cascade level n -> 4 UID bytes + BCC check byte
 *   SELECT cascade level n   -> SAK         (bit 2 set: more UID follows)
 *   HLTA                     -> card sleeps until it is removed
 * Every SPI exchange holds the shared-bus mutex (storage_spi_lock).
 */

/* MFRC522 registers */
#define MF_COMMAND      0x01
#define MF_COM_IRQ      0x04
#define MF_DIV_IRQ      0x05
#define MF_ERROR        0x06
#define MF_FIFO_DATA    0x09
#define MF_FIFO_LEVEL   0x0A
#define MF_CONTROL      0x0C
#define MF_BIT_FRAMING  0x0D
#define MF_COLL         0x0E
#define MF_MODE         0x11
#define MF_TX_CONTROL   0x14
#define MF_TX_ASK       0x15
#define MF_CRC_H        0x21
#define MF_CRC_L        0x22
#define MF_T_MODE       0x2A
#define MF_T_PRESCALER  0x2B
#define MF_T_RELOAD_H   0x2C
#define MF_T_RELOAD_L   0x2D
#define MF_VERSION      0x37

/* MFRC522 commands */
#define MFC_IDLE        0x00
#define MFC_CALC_CRC    0x03
#define MFC_TRANSCEIVE  0x0C
#define MFC_SOFT_RESET  0x0F

/* card commands */
#define PICC_REQA       0x26
#define PICC_HLTA       0x50

static spi_device_handle_t mf_dev[2];
static bool mf_ok[2];

static esp_err_t mf_write(int r, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), val };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    return spi_device_transmit(mf_dev[r], &t);
}

static uint8_t mf_read(int r, uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7E) | 0x80), 0 }, rx[2] = { 0 };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx, .rx_buffer = rx };
    return spi_device_transmit(mf_dev[r], &t) == ESP_OK ? rx[1] : 0;
}

static void mf_set_bits(int r, uint8_t reg, uint8_t mask) { mf_write(r, reg, mf_read(r, reg) | mask); }
static void mf_clear_bits(int r, uint8_t reg, uint8_t mask) { mf_write(r, reg, mf_read(r, reg) & (uint8_t)~mask); }

/* Waits for any bit in `mask` of `reg`, yielding to other tasks. */
static uint8_t mf_wait(int r, uint8_t reg, uint8_t mask, int timeout_ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        uint8_t v = mf_read(r, reg);
        if (v & mask) return v;
        if (esp_timer_get_time() > end) return 0;
        vTaskDelay(1);
    }
}

static bool mf_crc(int r, const uint8_t *data, int n, uint8_t out[2])
{
    mf_write(r, MF_COMMAND, MFC_IDLE);
    mf_write(r, MF_DIV_IRQ, 0x04);           /* clear CRCIRq   */
    mf_write(r, MF_FIFO_LEVEL, 0x80);        /* flush the FIFO */
    for (int i = 0; i < n; i++) mf_write(r, MF_FIFO_DATA, data[i]);
    mf_write(r, MF_COMMAND, MFC_CALC_CRC);
    if (!(mf_wait(r, MF_DIV_IRQ, 0x04, 30) & 0x04)) return false;
    mf_write(r, MF_COMMAND, MFC_IDLE);
    out[0] = mf_read(r, MF_CRC_L);
    out[1] = mf_read(r, MF_CRC_H);
    return true;
}

/* Sends `n` bytes (last byte has `tx_bits` valid bits, 0 = all 8) and
   collects the card's reply. Returns the number of bytes received, or
   -1 on timeout (no card) and -2 on a transmission error. */
static int mf_transceive(int r, const uint8_t *tx, int n, uint8_t tx_bits, uint8_t *rx, int rx_max,
                         uint8_t *rx_bits)
{
    mf_write(r, MF_COMMAND, MFC_IDLE);
    mf_write(r, MF_COM_IRQ, 0x7F);           /* clear all interrupt flags */
    mf_write(r, MF_FIFO_LEVEL, 0x80);
    for (int i = 0; i < n; i++) mf_write(r, MF_FIFO_DATA, tx[i]);
    mf_write(r, MF_BIT_FRAMING, tx_bits & 0x07);
    mf_write(r, MF_COMMAND, MFC_TRANSCEIVE);
    mf_set_bits(r, MF_BIT_FRAMING, 0x80);    /* StartSend */

    /* RxIRq (0x20) or IdleIRq (0x10) = done, TimerIRq (0x01) = no answer */
    uint8_t irq = mf_wait(r, MF_COM_IRQ, 0x31, 20);
    mf_clear_bits(r, MF_BIT_FRAMING, 0x80);
    if (!(irq & 0x30)) return -1;
    if (mf_read(r, MF_ERROR) & 0x13) return -2;   /* buffer, parity, protocol */
    int got = mf_read(r, MF_FIFO_LEVEL);
    if (got > rx_max) return -2;
    for (int i = 0; i < got; i++) rx[i] = mf_read(r, MF_FIFO_DATA);
    if (rx_bits) *rx_bits = mf_read(r, MF_CONTROL) & 0x07;
    return got;
}

static void mf_halt(int r)
{
    uint8_t cmd[4] = { PICC_HLTA, 0x00, 0, 0 }, rx[4], bits;
    if (!mf_crc(r, cmd, 2, &cmd[2])) return;
    (void)mf_transceive(r, cmd, 4, 0, rx, sizeof rx, &bits);   /* no reply = success */
}

static bool mf_read_uid_locked(int r, uint8_t uid[10], uint8_t *len)
{
    uint8_t buf[9], bits = 0;
    uint8_t req = PICC_REQA;
    mf_clear_bits(r, MF_COLL, 0x80);
    if (mf_transceive(r, &req, 1, 7, buf, sizeof buf, &bits) != 2) return false;

    static const uint8_t SEL[3] = { 0x93, 0x95, 0x97 };
    int used = 0;
    for (int level = 0; level < 3; level++) {
        uint8_t ac[2] = { SEL[level], 0x20 }, blk[5];
        if (mf_transceive(r, ac, 2, 0, blk, sizeof blk, &bits) != 5) return false;
        if ((blk[0] ^ blk[1] ^ blk[2] ^ blk[3]) != blk[4]) return false;   /* BCC */

        uint8_t sel[9] = { SEL[level], 0x70, blk[0], blk[1], blk[2], blk[3], blk[4], 0, 0 };
        uint8_t sak[3];
        if (!mf_crc(r, sel, 7, &sel[7])) return false;
        if (mf_transceive(r, sel, 9, 0, sak, sizeof sak, &bits) != 3) return false;

        if (blk[0] == 0x88) {           /* cascade tag: 3 UID bytes here, more follow */
            memcpy(uid + used, blk + 1, 3);
            used += 3;
            if (!(sak[0] & 0x04)) return false;
            continue;
        }
        memcpy(uid + used, blk, 4);
        used += 4;
        break;
    }
    if (used != 4 && used != 7 && used != 10) return false;
    *len = (uint8_t)used;
    mf_halt(r);
    return true;
}

bool mfrc522_read_uid(int r, uint8_t uid[10], uint8_t *len)
{
    if (r < 0 || r > 1 || !mf_ok[r]) return false;
    storage_spi_lock();
    bool ok = mf_read_uid_locked(r, uid, len);
    storage_spi_unlock();
    return ok;
}

bool mfrc522_ready(int r) { return r >= 0 && r <= 1 && mf_ok[r]; }

esp_err_t mfrc522_init(void)
{
    const int cs[2] = { PIN_RFID_ENTRY_SS, PIN_RFID_EXIT_SS };
    for (int r = 0; r < 2; r++) {
        spi_device_interface_config_t cfg = {
            .clock_speed_hz = 1000000, .mode = 0, .spics_io_num = cs[r], .queue_size = 1,
        };
        esp_err_t e = spi_bus_add_device(SPI2_HOST, &cfg, &mf_dev[r]);
        if (e != ESP_OK) return e;

        storage_spi_lock();
        mf_write(r, MF_COMMAND, MFC_SOFT_RESET);
        storage_spi_unlock();
        vTaskDelay(pdMS_TO_TICKS(60));
        storage_spi_lock();
        uint8_t version = mf_read(r, MF_VERSION);
        if (version != 0x00 && version != 0xFF) {
            /* timer: 13.56 MHz / (2*169+1) = 40 kHz, reload 200 => 5 ms reply timeout */
            mf_write(r, MF_T_MODE, 0x80);
            mf_write(r, MF_T_PRESCALER, 0xA9);
            mf_write(r, MF_T_RELOAD_H, 0x00);
            mf_write(r, MF_T_RELOAD_L, 0xC8);
            mf_write(r, MF_TX_ASK, 0x40);        /* 100 % ASK modulation */
            mf_write(r, MF_MODE, 0x3D);          /* CRC preset 0x6363    */
            mf_set_bits(r, MF_TX_CONTROL, 0x03); /* antenna on           */
            mf_ok[r] = true;
        }
        storage_spi_unlock();
        printf("[RFID] %s reader %s (version 0x%02X)\n", r == 0 ? "ENTRY" : "EXIT",
               mf_ok[r] ? "ready" : "NOT FOUND", version);
    }
    return (mf_ok[0] || mf_ok[1]) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
