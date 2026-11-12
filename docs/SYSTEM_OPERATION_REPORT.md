# System Operation Report

Draft text for deliverable 1. Add the Wokwi link, video links, member details and contributions before you submit.

## 1. Purpose

CoCIS needs two things from this pilot. Staff want only registered people inside the building, inside agreed hours. Management wants lights off in empty lecture rooms to cut the electricity bill. The system covers the main entrance and two lecture rooms.

## 2. Architecture

Three control terminals run on one ESP32 as independent FreeRTOS tasks:

- Terminal 1 and Terminal 2 (one task each) control Lecture Room 1 and Lecture Room 2.
- Terminal 3 controls the main entrance and coordinates the whole system.

Each terminal owns its hardware and its data. Terminals exchange messages through FreeRTOS queues in `components/comms`. No terminal reads another terminal's variables. Moving a room onto its own ESP32 later means swapping the queue for a network link (ESP-NOW or MQTT) with no change to the room logic.

The entrance publishes a retained BUILDING message (open or closed, emergency, people inside). A room which restarts reads the retained copy at once, the same idea as an MQTT retained message.

## 3. Hardware (Wokwi)

| Part | Job |
| --- | --- |
| ESP32 DevKitC V4 | runs all three terminals |
| 2 x MFRC522 RFID reader | entry (left) and exit (right) scans |
| 2 x servo | entry and exit door locks |
| LCD 20x4 (I2C) | status, welcome and denial messages, keypad menu |
| 4x3 keypad | management menu at the entrance |
| DS1307 RTC | wall clock which survives power loss |
| microSD card | access, event and energy logs |
| Buzzer | key clicks, grant and deny tones, siren |
| Red push button | break-glass emergency |
| 2 x PIR sensor | room occupancy |
| 2 x LED (PWM) | room light banks |
| Photoresistor module | daylight level |

## 4. Software design

| Component | Responsibility |
| --- | --- |
| `entrance/core.c` | access rules (pure C, host tested) |
| `entrance/runtime.c` | RFID polling, single access task, coordinator, keypad, alerts |
| `entrance/mfrc522.c` | RFID driver: REQA, anti-collision, select, halt (4, 7 and 10-byte UIDs) |
| `entrance/kpmenu.c` | keypad menu state machine (host tested) |
| `entrance/display.c`, `clock.c`, `dashboard.c` | LCD, RTC and SNTP, Wi-Fi web page |
| `rooms/core.c` | lighting rules and energy metering (host tested) |
| `rooms/runtime.c` | the two room tasks |
| `comms/bus.c` | terminal message queues and retained building state |
| `storage/runtime.c` | SD logger task, RAM fallback, NVS settings and roster |
| `actuator/runtime.c` | door servos with independent timers, buzzer patterns |
| `auth/` | operator sessions, login lockout, salted SHA-256 PINs |
| `cli/runtime.c` | serial management console |

## 5. How a card scan works

1. The RFID task polls both readers every 100 ms and reads the card UID.
2. The scan goes onto one queue. One access task processes every scan in order, so two doors never update the roll-call at the same moment.
3. The rules run in this order: reader locked, emergency, unknown card, blocked card, no trusted clock, outside hours, anti-passback, grant.
4. A granted scan opens the door servo for 3 s, updates the people-inside list in RAM and NVS, logs to the SD card and tells both rooms the new count.
5. A denied scan sounds two low beeps, shows the reason on the LCD and logs the attempt.

## 6. Schedule rules (requirement 2.6)

- Default: 08:00 to 18:00, Monday to Saturday. Overnight windows such as 18:00 to 02:00 work.
- Outside the window students and staff cannot enter.
- A person still inside after closing cannot leave by card. The system raises an alert and security uses `release <uid>`. Requirement 2.6 asks for this rule.
- Security and admin cards work at any hour for patrols.
- Only logged-in admins change the schedule: console `schedule set`, keypad option 1 or 2, or the web form with the admin PIN.
- Every change gets a version number, goes to NVS, the SD event log and both rooms.

## 7. Lighting rules (requirement 2.2)

Priority, highest first: emergency 100%, admin forced off, manual on (30 minutes), vacant 0%, grace period 20%, daylight level (0%, 50% or 100%).

- No motion for 30 s starts a 10 s dimmed warning, then lights go off.
- When the building closes, empty rooms switch off with no grace period.
- When the entrance count says nobody is inside, motion keeps the lights off and raises an INTRUSION alert.
- Each room meters Wh used against a baseline of lights on for all opening hours. The `energy` command shows savings in kWh and UGX.

## 8. Synchronisation (requirement 2.7)

| Shared resource | Protection |
| --- | --- |
| Roll-call, roster, schedule, statistics | `en_mtx` mutex. Only the access task changes who is inside. |
| SPI bus (SD card and two readers) | `storage_spi_lock()` mutex around every card transaction and file operation |
| SD card writes | one logger task fed by a queue. Callers never block on the card. |
| RAM log rings | `st_ring_mtx` |
| Daylight ADC | `rm_adc_mtx` (two room tasks share one sensor) |
| Keypad menu | `kp_mtx` (keypad task writes, coordinator draws the LCD) |
| Terminal messages | FreeRTOS queues plus a mutex for the retained state |
| Card scans | `en_scan_q`: readers and console both feed one consumer |

## 9. Reliability

- SD card missing: records stay in RAM rings and the console still shows them.
- Power cut: schedule, roster, PINs and people inside come back from NVS.
- Clock: RTC at boot, SNTP correction over Wi-Fi, manual `time set`. Without a clock only security cards work.
- Room terminal stops: the entrance marks it OFFLINE after 8 s. `drill 2 12` demonstrates this.
- Doors close on their own timers, so a slow SD write never holds a door open.

## 10. Serial interface (requirement 2.5)

Public: `help`, `status`, `rooms`, `energy`, `stats`, `schedule`, `login`, `logout`, `whoami`.
Security: `who`, `users`, `logs`, `alerts`, `release`, `emergency`, `alarm off`, `tap`, `passwd`.
Admin: `schedule set`, `user add/del/block/unblock`, `light`, `vacancy`, `drill`, `confirm-empty`, `time set`, `files`, `export`.

Three wrong logins lock the console for 60 s and raise an alert. Sessions end after 5 minutes idle.

## 11. Innovation features

1. Anti-passback on entry and exit.
2. Failed-scan alarm: 3 invalid cards in 60 s lock the reader for 30 s and sound a siren.
3. Emergency mode: break-glass button or command opens both doors and sets all lights to 100%.
4. After-hours exit hold with security release.
5. Intrusion alert from room PIRs when the building is empty.
6. Energy and UGX savings report.
7. Daylight dimming and a grace period before lights go off.
8. Room heartbeat supervision with a fault drill.
9. Keypad and LCD management, plus a web dashboard.
10. Roll-call survives power cuts.
