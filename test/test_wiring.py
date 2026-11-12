#!/usr/bin/env python3
"""Checks diagram.json against include/common/config.h: every pin in the
code is wired to the right part, and no GPIO drives two different jobs."""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cfg = open(os.path.join(ROOT, "include/common/config.h")).read()
pins = {k: int(v) for k, v in re.findall(r"#define\s+(PIN_\w+)\s+(\d+)", cfg)}
diagram = json.load(open(os.path.join(ROOT, "diagram.json")))
NAME = {36: "VP", 39: "VN"}

wired = {}
for a, b, *_ in diagram["connections"]:
    for x, y in ((a, b), (b, a)):
        if x.startswith("esp:"):
            wired.setdefault(x[4:], set()).add(y)

EXPECT = {  # config name -> part:pin that must be on that GPIO
    "PIN_SPI_SCK": ["entry_rfid:SCK", "exit_rfid:SCK", "sd:SCK"],
    "PIN_SPI_MOSI": ["entry_rfid:MOSI", "exit_rfid:MOSI", "sd:DI"],
    "PIN_SPI_MISO": ["entry_rfid:MISO", "exit_rfid:MISO", "sd:DO"],
    "PIN_RFID_ENTRY_SS": ["entry_rfid:SDA"], "PIN_RFID_EXIT_SS": ["exit_rfid:SDA"],
    "PIN_SD_CS": ["sd:CS"], "PIN_I2C_SDA": ["lcd:SDA", "rtc:SDA"], "PIN_I2C_SCL": ["lcd:SCL", "rtc:SCL"],
    "PIN_PIR_ROOM1": ["pir1:OUT"], "PIN_PIR_ROOM2": ["pir2:OUT"], "PIN_LDR_DAYLIGHT": ["ldr:AO"],
    "PIN_LIGHT_ROOM1": ["light1:A"], "PIN_LIGHT_ROOM2": ["light2:A"],
    "PIN_SERVO_ENTRY": ["door_entry:PWM"], "PIN_SERVO_EXIT": ["door_exit:PWM"], "PIN_BUZZER": ["buzzer:2"],
    "PIN_EMERGENCY_BTN": ["btn_emergency:1.l", "emerg_pullup:1"],
    "PIN_KP_R1": ["keypad:R1"], "PIN_KP_R2": ["keypad:R2"], "PIN_KP_R3": ["keypad:R3"], "PIN_KP_R4": ["keypad:R4"],
    "PIN_KP_C1": ["keypad:C1"], "PIN_KP_C2": ["keypad:C2"], "PIN_KP_C3": ["keypad:C3"],
}
fails = 0
for name, need in EXPECT.items():
    gpio = pins[name]
    label = NAME.get(gpio, str(gpio))
    for n in need:
        if n not in wired.get(label, set()):
            print(f"  FAIL {name}={gpio}: {n} not wired to esp:{label}"); fails += 1
gpios = [v for k, v in pins.items()]
shared_ok = {pins["PIN_SPI_SCK"], pins["PIN_SPI_MOSI"], pins["PIN_SPI_MISO"], pins["PIN_I2C_SDA"], pins["PIN_I2C_SCL"]}
for g in set(gpios):
    users = [k for k, v in pins.items() if v == g]
    if len(users) > 1 and g not in shared_ok:
        print(f"  FAIL GPIO{g} used by {users}"); fails += 1
for g in gpios:
    if 6 <= g <= 11:
        print(f"  FAIL GPIO{g} is a flash pin"); fails += 1
print(f"wiring: {len(EXPECT)} signals checked, {fails} failed")
sys.exit(1 if fails else 0)
