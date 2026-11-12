# Repository structure

```
CoCIS_SCMS/
  platformio.ini        sole build environment (esp32dev, ESP-IDF)
  CMakeLists.txt        project file, also works with idf.py
  sdkconfig.defaults    flash size, stack, tick rate
  wokwi.toml            simulator config for PlatformIO builds
  wokwi.idf.toml        simulator config for idf.py builds
  diagram.json          Wokwi circuit
  src/main.c            start-up order
  include/<module>/     public headers
  components/<module>/  implementation, one CMakeLists.txt each
    common/   shared types and helpers
    comms/    terminal messages
    auth/     operator sessions and PINs
    storage/  SD, RAM rings, NVS
    actuator/ doors and buzzer
    rooms/    Terminals 1 and 2
    entrance/ Terminal 3: RFID, rules, keypad, LCD, clock, dashboard
    cli/      serial console
  test/                 host tests and wiring check
  scenarios/            Wokwi automation tests
  scripts/              test runners, web bundle generator
  web/                  generated single-file build for wokwi.com
  sdcard/               notes on the simulated card
  docs/                 report, pins, demo, tests, ownership
```

Generated folders (`.pio`, `build`, `.qa`) stay out of git.
