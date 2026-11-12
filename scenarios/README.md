# Wokwi automation scenarios

Run with wokwi-cli after a build, for example:

    wokwi-cli --scenario scenarios/rfid-entry-exit.yaml --timeout 60000 .

| File | Checks |
| --- | --- |
| boot-check.yaml | all three terminals start |
| rfid-entry-exit.yaml | security NFC tag enters and leaves (any hour) |
| anti-passback.yaml | second entry with the same card is denied |
| failed-scan-lockout.yaml | 3 invalid cards lock the reader |
| student-scan.yaml | student card read (result depends on the clock) |
