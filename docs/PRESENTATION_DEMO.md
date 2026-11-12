# Presentation demo script (about 10 minutes)

Before you start: build, start the simulator, wait for `[ENTRANCE] terminal ready`. If the LCD clock shows the wrong time, run `login admin 1234` then `time set YYYY-MM-DD HH:MM`.

1. Boot and status. Type `status` and `rooms`. Point at the three terminals and the heartbeat column.
2. Normal entry. Left reader, Green card, tag on. The entry servo opens, the LCD says WELCOME, `inside=1`.
3. Anti-passback. Tag off, wait 2 s, tag on again. Denied with ANTI_PASSBACK_IN and an alert.
4. Lights. Move the Room 1 PIR. Lights go to 100% (or less with a bright LDR). Stop motion and watch the 20% grace then off.
5. Exit. Right reader, Green card. Exit servo opens, `inside=0`.
6. Intrusion. With nobody inside, move a PIR. INTRUSION alert, lights stay off.
7. Failed scans. Red card, Key fob, Red card on the left reader. Siren, reader locked. Show `alarm off` after login.
8. Schedule. `login admin 1234`, `schedule set 07:00 08:00` (any window which excludes now). Green card on entry: OUTSIDE_HOURS. NFC tag: allowed (security).
9. Exit hold. Restore the schedule, let Green in, close the window again, try Green on exit: EXIT_LOCKED_HOURS. Then `release 11223344`.
10. Emergency. Press BREAK GLASS. Both doors open, both rooms at 100%, siren. Clear with `emergency off`.
11. Keypad. `#`, `1234`, `#`, then `1`, `0730`, `#`. Show the NOTICE line on the console.
12. Fault drill. `drill 2 12`. Room 2 goes OFFLINE after 8 s and recovers.
13. Reports. `energy`, `who`, `logs 10`, `alerts 10`, `files`.
