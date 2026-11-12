# Test results (12 November 2026)

## Build

| Build | Result |
| --- | --- |
| Modular project, ESP-IDF v5.3.2, `idf.py build` | PASS, 0 warnings, 905 KB app |
| Single-file `web/main.c`, default sdkconfig (wokwi.com style) | PASS, 987 KB app (1 MB partition) |

PlatformIO uses the same CMake tree. Run `pio run -e esp32dev` on your machine to confirm.

## Host tests (`sh scripts/test-all.sh`)

| Suite | Checks | Result |
| --- | --- | --- |
| Access rules (schedule, overnight window, anti-passback, hours, roles, lockout, emergency, roster) | 58 | PASS |
| Room lighting (grace, daylight, modes, emergency, closing, intrusion, energy) | 18 | PASS |
| Login sessions and keypad menu | 29 | PASS |
| Wiring check, diagram.json against config.h | 24 signals | PASS |

## Emulator run (Espressif QEMU)

The firmware booted in QEMU with stubs for parts QEMU lacks (ADC, SPI devices, Wi-Fi). Console tests passed for: login lockout, `time set`, schedule open and close notices, student entry and exit, after-hours exit hold, `release`, emergency on and off (both rooms went to 100% and back), unknown card during emergency, remote light control, vacancy change, room heartbeat loss and recovery (`drill`), user add, block and delete, `confirm-empty`, PIN change, RAM log fallback with no SD card.

## Still to record in Wokwi

QEMU does not model the MFRC522, LCD, keypad, PIR or servos. Run the files in `scenarios/` and the demo script in Wokwi, then paste the serial output here as your evidence.
