#!/bin/bash
# 1.4.9: XHCI aktarim katmani + USB HID. PS/2 KAPALI (QEMU i8042=off): klavye
# ve fare YALNIZ USB'den. Iki kosu: usb-kbd + usb-tablet (mutlak) ve
# usb-kbd + usb-mouse (goreli).
#   bash tests/xhci/calistir.sh <iso> <cikti_dizini>
set -u
ISO="$1"; D="$2"; mkdir -p "$D"
KOK="$(cd "$(dirname "$0")/../.." && pwd)"
d(){ if eval "$2"; then echo "  [GECTI] $1"; else echo "  [KALDI] $1"; fi; }
for K in tablet fare; do
  mkdir -p "$D/$K"; cp "$KOK/diskfiles.img" "$D/$K/d.img"
  timeout 300 python3 "$KOK/tests/xhci/sur.py" "$ISO" "$D/$K/d.img" "$D/$K" $K
  L="$D/$K/seri.log"
  d "[$K] XHCI kuruldu, USB klavye hazir" "grep -aq 'xhci: USB klavye hazir' '$L'"
  if [ $K = tablet ]; then
    d "[$K] USB tablet hazir (rapor tanimlayicisi: mutlak)" "grep -aq 'xhci: USB fare/tablet hazir.*mutlak' '$L'"
  else
    d "[$K] USB fare hazir (rapor tanimlayicisi: goreli)" "grep -aq 'xhci: USB fare/tablet hazir.*goreli' '$L'"
  fi
  # TR-Q: a g-breve (0xF0) I-dot (0xDD) S-cedilla (0xDE) @ : ISO-8859-9 baytlariyla
  d "[$K] USB klavye TR-Q: 'ağİŞ@' kabuga ulasti (Shift, AltGr)" "LC_ALL=C grep -aq \$'Bilinmeyen komut: a\\xf0\\xdd\\xde@' '$L'"
  d "[$K] basili tutulan tus yinelendi (500 ms sonra)" "LC_ALL=C grep -aqE 'Bilinmeyen komut: z{10,}' '$L'"
  d "[$K] USB fare/tablet tiklamasi rihtimdaki ULGEN'i acti (GORUNUM.ELF)" "grep -aq 'GORUNUM.ELF Ring3' '$L'"
  d "[$K] PS/2 yok: i8042 kapali iken panik yok" "! grep -aq 'PANIGI\|PAGE FAULT' '$L'"
done
