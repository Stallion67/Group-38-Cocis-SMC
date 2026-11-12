# Simulated SD card

The firmware formats a blank card on first mount and writes daily CSV files (see docs/INTERFACES.md).
To preload a roster, place a USERS.CSV with lines `uid,role,blocked,name` (role 0 student, 1 staff, 2 security, 3 admin). The firmware imports it once when NVS holds no roster.
Use `files` and `export yymmdd` on the console to read the card during a simulation.
