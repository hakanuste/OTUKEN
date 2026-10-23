#!/bin/bash
# US1 uyum sınaması — tek komut.  ./tests/us1_conformance/calistir.sh
# 1) us1.c'yi iz açık (-DUS1_IZ) paylaşımlı kitaplık olarak derler
# 2) Python (float64, aynı Q8 dosya) ile işlem düzeyinde karşılaştırır
# 3) us1_sina'yı (C tarafı öz-sınaması) koşar
# Çıktı: rapor ekrana + tests/us1_conformance/son_rapor.json. Çıkış kodu 0 = hepsi geçti.
set -u
cd "$(dirname "$0")/../.."
LIB=$(mktemp /tmp/libus1iz_XXXX.so)
gcc -std=c11 -O2 -Wall -Wextra -Wshadow -DUS1_IZ -fPIC -shared us1/c/us1.c -o "$LIB" || exit 2
UYUM_JSON=tests/us1_conformance/son_rapor.json python3 tests/us1_conformance/uyum.py "$LIB"
r1=$?
(cd us1 && gcc -std=c11 -O2 -Wall -Wextra -o us1_sina sinama/us1_sina.c c/us1.c && ./us1_sina | tail -1)
r2=${PIPESTATUS[0]}
rm -f "$LIB"
[ $r1 -eq 0 ] && [ $r2 -eq 0 ]
