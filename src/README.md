# src

`main.c` starts the system in this order: NVS, storage (SPI bus, SD card, logger), message bus, actuators, rooms, entrance, console. Storage comes first because it owns the SPI bus the RFID readers join later.
