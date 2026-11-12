# Components

| Folder | Terminal | What it holds |
| --- | --- | --- |
| common | all | shared types, UID and time helpers |
| comms | all | terminal message queues, retained building state |
| auth | entrance | operator sessions, lockout, PIN hashes |
| storage | all | SD logger task, RAM rings, NVS, roster |
| actuator | entrance | door servos, buzzer |
| rooms | 1 and 2 | lighting rules (core.c) and room tasks (runtime.c) |
| entrance | 3 | access rules, RFID driver, keypad menu, LCD, clock, dashboard, coordinator |
| cli | entrance | serial management console |

Files named core.c hold pure C logic with no ESP-IDF calls. The host tests compile them directly.
