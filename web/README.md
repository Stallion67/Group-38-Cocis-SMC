# Single-file build for wokwi.com

wokwi.com's online ESP-IDF editor compiles one main.c. `scripts/make_web_bundle.py` merges every header and source into `web/main.c`.

1. Run `python3 scripts/make_web_bundle.py` after any code change.
2. On wokwi.com open a new ESP32 ESP-IDF project.
3. Replace the main.c tab with `web/main.c` and the diagram.json tab with `../diagram.json`.
4. Start the simulation and open the serial monitor.

The bundle tested at 987 KB against a 1 MB app partition. If a newer builder reports the app is too large, set `SCMS_ENABLE_NETWORK` to 0 near the top of main.c. That drops Wi-Fi, SNTP and the web dashboard. The RTC still keeps time.
