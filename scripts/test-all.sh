#!/bin/sh
# Host checks: pure logic (gcc) + wiring. Run from the repository root.
set -e
mkdir -p .qa
CFLAGS="-std=c11 -Wall -Wextra -Werror -Iinclude"
gcc $CFLAGS test/test_entrance.c components/entrance/core.c components/common/system.c -o .qa/test_entrance
gcc $CFLAGS test/test_rooms.c components/rooms/core.c -o .qa/test_rooms
gcc $CFLAGS test/test_auth_keypad.c components/auth/session.c components/entrance/kpmenu.c components/common/system.c -o .qa/test_auth_keypad
./.qa/test_entrance
./.qa/test_rooms
./.qa/test_auth_keypad
python3 test/test_wiring.py
echo "ALL HOST TESTS PASSED"
