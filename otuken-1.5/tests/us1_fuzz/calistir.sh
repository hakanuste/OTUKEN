#!/bin/bash
# US1 ayrıştırıcı fuzz sınaması (ASan + UBSan). ./tests/us1_fuzz/calistir.sh [deneme]
set -u
cd "$(dirname "$0")/../.."
B=$(mktemp /tmp/us1fuzz_XXXX)
gcc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wall -Wextra tests/us1_fuzz/fuzz.c us1/c/us1.c -o "$B" -lm || exit 2
ASAN_OPTIONS=detect_leaks=1 "$B" us1/umay.us1 "${1:-20000}"
r=$?; rm -f "$B"; exit $r
