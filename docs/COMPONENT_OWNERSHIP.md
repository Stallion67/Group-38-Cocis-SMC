# Component ownership

Fill in the names before submission. The split below follows the Member 1 and Member 2 labels from the earlier three-board version.

| Area | Folder | Owner |
| --- | --- | --- |
| Access rules, schedule, anti-passback, lockout | `components/entrance/core.c` | Member 1 |
| RFID driver and polling | `components/entrance/mfrc522.c`, `runtime.c` (rfid task) | Member 1 |
| Operator login and PIN storage | `components/auth/` | Member 1 |
| Serial console | `components/cli/` | Member 1 |
| Keypad menu | `components/entrance/kpmenu.c`, `runtime.c` (keypad task) | Member 1 |
| Room lighting and energy | `components/rooms/` | Member 2 |
| Terminal messages and heartbeats | `components/comms/`, `runtime.c` (coordinator) | Member 2 |
| SD logging, NVS, roster | `components/storage/` | Member 2 |
| Doors, buzzer, emergency outputs | `components/actuator/` | Member 2 |
| LCD, RTC, SNTP, dashboard | `components/entrance/display.c`, `clock.c`, `dashboard.c` | Member 2 |
| Circuit, scenarios, tests | `diagram.json`, `scenarios/`, `test/` | Shared |

Each member should demonstrate their own rows in their video and be ready to explain the code line by line.
