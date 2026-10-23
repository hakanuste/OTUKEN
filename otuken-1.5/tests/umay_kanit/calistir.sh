#!/bin/bash
# UMAY kanıt karar katmanı ölçümü — tek komut.
#   tests/umay_kanit/calistir.sh [ud_dizini]
# Gerekenler: gcc, python3, mtools (diskfiles.img'deki belgeler için),
# UD Türkçe ağaç bankaları (alan dışı küme; verilmezse /tmp/ud, yoksa indirilir).
set -e
KOK=$(cd "$(dirname "$0")/../.." && pwd)
IS=$(mktemp -d /tmp/umay_kanit.XXXX)
trap 'rm -rf "$IS"' EXIT
UD=${1:-/tmp/ud}
U=$KOK/umay
gcc -std=c11 -O1 -Wall -Wextra -Wshadow -I$U/include -I$KOK/us1/c -o "$IS/kanit_olc" \
  "$KOK/tests/umay_kanit/kanit_olc.c" $U/core/umay_str.c $U/core/umay_ortak.c $U/core/umay_bilgi.c \
  $U/core/umay_ozan.c $U/core/umay_bengi.c $U/core/umay_onay.c $U/core/umay_kanit.c \
  $U/alp/otag.c $U/alp/alp_gozcu.c $U/alp/alp_onarici.c $U/alp/alp_bilge.c $U/alp/alp_yazman.c \
  $U/alp/alp_uzman.c $U/alp/arastirma.c $U/us/us_stub.c $U/us/umay_us.c $U/us/umay_us_ipc.c \
  $U/us/umay_us_yerel.c $KOK/us1/c/us1.c $U/ses/umay_ses.c $U/port/umay_port_host.c
mkdir -p "$IS/belge"
for f in belge1.txt belge2.txt belge3.txt; do
  mtype -i "$KOK/diskfiles.img" ::/$f > "$IS/belge/$f" 2>/dev/null || rm -f "$IS/belge/$f"
done
if [ ! -d "$UD/UD_Turkish-BOUN" ]; then
  mkdir -p "$UD"
  for tb in UD_Turkish-BOUN UD_Turkish-Atis UD_Turkish-Kenet UD_Turkish-Tourism UD_Turkish-PUD UD_Turkish-Penn UD_Turkish-FrameNet UD_Turkish-GB; do
    git clone -q --depth 1 https://github.com/UniversalDependencies/$tb "$UD/$tb"
  done
fi
python3 "$KOK/tests/umay_kanit/olc.py" "$IS/kanit_olc" "$IS/belge" "$UD"
