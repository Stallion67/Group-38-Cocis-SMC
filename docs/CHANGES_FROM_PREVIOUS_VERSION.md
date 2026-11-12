# Changes from the earlier three-board version

| Earlier version | This version |
| --- | --- |
| 3 Wokwi projects (entrance, room 1, room 2) linked by MQTT on a public broker | 1 ESP32, 3 terminal tasks linked by queues. No broker outage risk. |
| Potentiometer and two buttons pretended to be RFID | 2 real MFRC522 readers with a register-level driver |
| One 1960-line main.c plus one 700-line main.c | Components with headers, matching the reference layout |
| LCD 16x2 | LCD 20x4 with a keypad menu view |
| No keypad | 4x3 keypad for login, schedule, emergency, lights |
| No tests | Host tests, wiring check and Wokwi scenarios |
| Each room had its own LDR and override button | One shared LDR. Override moved to keypad and console (GPIO budget) |
| MQTT ACK and retransmit protocol | Queue sequence numbers with lost-event counting and heartbeat timeout |

Kept from your design: roles and demo users, salted PIN hashes, login lockout, anti-passback, failed-scan lockout, emergency mode, after-hours exit hold and release, energy and UGX report, grace dimming, daylight levels, RTC with SNTP, NVS roll-call, daily CSV files, RAM fallback, the console command set.
