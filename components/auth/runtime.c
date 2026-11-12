#include "auth/runtime.h"

#include <stdio.h>
#include <string.h>
#include "mbedtls/sha256.h"
#include "nvs.h"

/* Salted SHA-256 PIN hashes in NVS. Defaults apply until a PIN is changed. */

static const char *const AUTH_DEFAULT_PIN[AUTH_NUM_OPS] = { "1234", "4321" };

static void auth_hash(int op, const char *pin, uint8_t out[32])
{
    char buf[64];
    snprintf(buf, sizeof buf, "CoCIS-SCMS:%s:%s", auth_op_name(op), pin);
    mbedtls_sha256((const unsigned char *)buf, strlen(buf), out, 0);
}

static void auth_stored_hash(int op, uint8_t out[32])
{
    nvs_handle_t h;
    char key[16];
    size_t len = 32;
    snprintf(key, sizeof key, "pin_%s", auth_op_name(op));
    if (nvs_open("auth", NVS_READONLY, &h) == ESP_OK) {
        esp_err_t e = nvs_get_blob(h, key, out, &len);
        nvs_close(h);
        if (e == ESP_OK && len == 32) return;
    }
    auth_hash(op, AUTH_DEFAULT_PIN[op], out);
}

/* constant-time compare: no early exit that leaks timing */
static bool auth_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= a[i] ^ b[i];
    return d == 0;
}

void auth_runtime_init(void) { }

int auth_check(const char *name, const char *pin)
{
    int op = auth_op_find(name);
    if (op < 0 || !pin) return -1;
    uint8_t want[32], got[32];
    auth_stored_hash(op, want);
    auth_hash(op, pin, got);
    return auth_equal(want, got) ? op : -1;
}

int auth_check_any(const char *pin)
{
    for (int op = 0; op < AUTH_NUM_OPS; op++)
        if (auth_check(auth_op_name(op), pin) >= 0) return op;
    return -1;
}

bool auth_set_pin(int op, const char *pin)
{
    if (op < 0 || op >= AUTH_NUM_OPS || !pin || strlen(pin) < 4 || strlen(pin) > 8) return false;
    for (const char *p = pin; *p; p++)
        if (*p < '0' || *p > '9') return false;     /* digits only: keypad must work */
    uint8_t h32[32];
    char key[16];
    nvs_handle_t h;
    auth_hash(op, pin, h32);
    snprintf(key, sizeof key, "pin_%s", auth_op_name(op));
    if (nvs_open("auth", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, key, h32, 32) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
