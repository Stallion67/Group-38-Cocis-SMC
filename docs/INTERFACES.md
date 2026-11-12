# Pin map and interfaces

Board: `board-esp32-devkit-c-v4`. Source of truth: `include/common/config.h`. `test/test_wiring.py` checks `diagram.json` against it.

## Pins

| GPIO | Part | Notes |
| --- | --- | --- |
| 18 / 23 / 19 | SPI SCK / MOSI / MISO | shared by SD card and both RFID readers |
| 5 | Entry MFRC522 SDA (SS) | left reader |
| 13 | Exit MFRC522 SDA (SS) | right reader |
| 2 | microSD CS | 10k pull-up |
| 3V3 | MFRC522 RST | tied high, driver uses soft reset |
| 21 / 22 | I2C SDA / SCL | LCD 20x4 (0x27) and DS1307 RTC (0x68) |
| 34 | PIR Room 1 | input-only pin |
| 35 | PIR Room 2 | input-only pin |
| 36 (VP) | LDR module AO | ADC1 channel 0, one daylight sensor for both rooms |
| 26 | Room 1 lights | LEDC PWM, 5 kHz |
| 25 | Room 2 lights | LEDC PWM, 5 kHz |
| 27 | Entry door servo | LEDC 50 Hz |
| 12 | Exit door servo | LEDC 50 Hz, 10k pull-down (strap pin) |
| 4 | Buzzer | LEDC high-speed timer |
| 39 (VN) | Break-glass button | 10k pull-up, press = low |
| 14 / 15 / 16 / 17 | Keypad rows R1-R4 | outputs |
| 32 / 33 / 0 | Keypad columns C1-C3 | inputs, internal pull-ups |

The single ESP32 has 24 usable GPIOs and this design uses all of them. Two choices made the budget fit: one LDR serves both rooms (same building face) and the keypad runs in 4x3 mode.

## LEDC channels

| Mode / timer | Channel | Use |
| --- | --- | --- |
| Low-speed timer 0, 50 Hz | 0, 1 | entry and exit servos |
| Low-speed timer 1, 5 kHz | 2, 3 | Room 1 and Room 2 lights |
| High-speed timer 0 | 0 | buzzer tones |

## Messages between terminals (`include/comms/bus.h`)

| Message | From to | Content |
| --- | --- | --- |
| ROOM_STATE | room to entrance, every 2 s | occupied, light %, mode, lux, Wh used, baseline Wh, seconds on |
| ROOM_EVENT | room to entrance | sequence number, type (OCCUPIED, VACANT, LIGHT, REMOTE, INTRUSION...), detail |
| ROOM_CMD | entrance to room | light mode, vacancy timeout, fault-drill pause |
| BUILDING | entrance to both rooms, retained | open, emergency, people inside, count trusted, schedule version |

A gap in sequence numbers counts as lost events (`rooms` command). No heartbeat for 8 s marks the room OFFLINE and raises an alert.

## Files on the SD card

| File | Content |
| --- | --- |
| `Ayymmdd.CSV` | access records: time, door, uid, name, result, reason, inside |
| `Eyymmdd.CSV` | events and alerts: time, level, source, message |
| `Nyymmdd.CSV` | energy snapshot every 5 minutes per room |
| `USERS.CSV` | roster mirror. A USERS.CSV on a fresh card imports once. |

NVS keeps the schedule, the roster, the people-inside list and the hashed PINs, so a power cut loses none of them.
