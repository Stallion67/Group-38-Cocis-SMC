# CoCIS Smart Control and Monitoring System (SCMS)

ESP-IDF firmware and Wokwi circuit for the CoCIS pilot: one main entrance with RFID entry and exit, plus two lecture rooms with automatic lighting.

One ESP32 runs the three control terminals as separate FreeRTOS tasks. The terminals talk only through message queues (`components/comms`), the same way three boards would talk over a network.

| Terminal | Code | Hardware in `diagram.json` |
| --- | --- | --- |
| 1. Lecture Room 1 | `components/rooms` | PIR, dimmable light bank (LED), shared daylight sensor |
| 2. Lecture Room 2 | `components/rooms` | PIR, dimmable light bank (LED), shared daylight sensor |
| 3. Main entrance (coordinator) | `components/entrance` | 2 x MFRC522 RFID readers, 2 door servos, 20x4 LCD, 4x3 keypad, DS1307 RTC, buzzer, break-glass button, microSD |

## Build and run

Option A, VS Code (recommended, same as the reference project):

1. Install VS Code, the PlatformIO extension and the Wokwi extension.
2. Open this folder and run `pio run -e esp32dev`.
3. Press F1, choose "Wokwi: Start Simulator". Open the serial monitor.
4. Wait for `[ENTRANCE] terminal ready`, then type `help`.

Option B, plain ESP-IDF 5.3 or newer: `idf.py build`, rename `wokwi.idf.toml` to `wokwi.toml`, then start the simulator.

Option C, wokwi.com in the browser: run `python3 scripts/make_web_bundle.py`, create a new ESP-IDF ESP32 project on wokwi.com, paste `web/main.c` into `main.c` and `diagram.json` into `diagram.json`. See `web/README.md`.

## Using the simulation

Cards (click a reader, pick a card, press the tag button):

| Wokwi card | UID | Person | Result |
| --- | --- | --- | --- |
| Blue (uid attribute) | A1B2C3D4 | Dr Nakato Sarah, STAFF | allowed in opening hours |
| Green | 11223344 | Okello John, STUDENT | allowed in opening hours |
| Yellow | 55667788 | Achieng Grace, STUDENT | allowed in opening hours |
| NFC tag (7-byte UID) | 04112233445566 | Mugisha Peter, SECURITY | allowed at any hour |
| Key fob | C0FFEE99 | Blocked Student | always denied (blocked) |
| Red | AABBCCDD | not registered | always denied (unknown) |

Left reader = ENTRY, right reader = EXIT. A granted scan swings that door servo open for 3 s.

Operator logins (console, keypad and web): `admin / 1234` and `security / 4321`. Change them with `passwd`.

Keypad: `#` PIN `#` to log in, then `1` opening time, `2` closing time (type HHMM then `#`), `3` emergency, `4`/`5` room lights, `6` alarm off, `0` log out.

Lights: room lights stay off while the entrance count says the building is empty. Admit someone first, then move the PIR slider. Motion in an empty building raises an INTRUSION alert instead.

Outside 08:00-18:00 (Mon-Sat) only the security tag works. To demonstrate students at any hour: `login admin 1234`, then `schedule set 00:00 23:59 1111111`.

## Folder map

See [docs/REPOSITORY_STRUCTURE_GUIDE.md](docs/REPOSITORY_STRUCTURE_GUIDE.md). Short version:

- `src/main.c` starts everything in a fixed order.
- `components/` holds one folder per module, `include/` holds their public headers.
- `test/` holds host tests for the pure logic and a wiring check. Run `sh scripts/test-all.sh`.
- `scenarios/` holds Wokwi automation tests for the RFID readers.
- `docs/` holds the operation report, pin map, demo script and test results.

## Feature list

Required: RFID entry and exit, door control, roll-call of who is inside, occupancy-based lighting for two rooms, SD card logging, serial menus, configurable schedule enforced at entry and exit, authenticated configuration, mutex and queue synchronisation.

Added: anti-passback, failed-scan alarm with reader lockout, emergency mode (break-glass), after-hours exit hold with security release, daylight dimming with a grace period, energy and UGX savings report, intrusion alert, room heartbeat supervision with a fault drill, keypad and LCD management, web dashboard, RTC with SNTP correction, roster and roll-call kept across power cuts.
