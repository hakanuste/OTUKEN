#!/bin/bash
# USB HID klavye ceviricisi (ana makine, gercek src/hid.c, ASan/UBSan)
set -u
KOK=$(cd "$(dirname "$0")/../.." && pwd)
IS=$(mktemp -d "${TMPDIR:-/tmp}/hid_sina.XXXX"); trap 'rm -rf "$IS"' EXIT
gcc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -I"$KOK/include" -o "$IS/h" \
    "$KOK/tests/hid/hid_sina.c" "$KOK/src/hid.c" || { echo "DERLEME HATASI"; exit 2; }
"$IS/h"
