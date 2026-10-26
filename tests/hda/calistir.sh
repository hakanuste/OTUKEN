#!/bin/bash
# 1.4.9: SES KARTLARI — Intel HDA (QEMU intel-hda + hda-output) ve AC'97.
# TANI cekirdegi: UYG-TEST DEDE sozunu soyletir (kart hepsini calmali),
# SES-TON-TEST bilinen kare dalgayi (1000 Hz, +-8000, 300 ms) calar; ana
# makine WAV kaydinda dalgayi ORNEK ORNEK arar (tests/hda/kare_ara.py).
#   bash tests/hda/calistir.sh <tani_iso> <cikti_dizini>
set -u
ISO="$1"; D="$2"; mkdir -p "$D"
KOK="$(cd "$(dirname "$0")/../.." && pwd)"
d(){ if eval "$2"; then echo "  [GECTI] $1"; else echo "  [KALDI] $1"; fi; }
for K in hda ac97; do
  cp "$KOK/diskfiles.img" "$D/$K.img"; rm -f "$D/$K.wav"
  if [ $K = hda ]; then DEV="-device intel-hda -device hda-output,audiodev=a0,mixer=off"; else DEV="-device AC97,audiodev=a0"; fi
  timeout 110 qemu-system-x86_64 -cdrom "$ISO" -drive file="$D/$K.img",format=raw,if=ide,index=0 \
    -netdev user,id=n0 -device e1000,netdev=n0 \
    -audiodev wav,id=a0,path="$D/$K.wav",out.frequency=48000,out.channels=2,out.format=s16 $DEV \
    -m 256 -display none -serial file:"$D/$K.log" -no-reboot -boot d &
  QP=$!
  for i in $(seq 1 440); do grep -aq "UYG-TEST: Uygulamalar menusu" "$D/$K.log" 2>/dev/null && break; kill -0 $QP 2>/dev/null || break; sleep 0.25; done
  sleep 3; kill $QP 2>/dev/null; wait $QP 2>/dev/null
  L="$D/$K.log"
  python3 "$KOK/tests/hda/kare_ara.py" "$D/$K.wav" > "$D/$K.kare.txt" 2>&1
  E=$(sed -n 's/.*en_uzun_tam_eslesme=\([0-9]*\).*/\1/p' "$D/$K.kare.txt"); E=${E:-0}
  if [ $K = hda ]; then
    d "[hda] HDA denetleyici, kodek yolu (igne <- DAC) kuruldu" "grep -aq 'HDA: bulundu' '$L' && grep -aq 'HDA: kodek=.*DAC=' '$L'"
    d "[hda] DEDE penceresi 'Ses kartı: var (HDA)' dedi, soz calindi" "grep -aq 'UYG-TEST: DEDE hata=0' '$L' && grep -aq 'dede kare=[1-9][0-9]* .*bosaldi=1' '$L'"
  else
    d "[ac97] AC'97 bulundu, DEDE sozu calindi" "grep -aq 'SES: AC97 bulundu' '$L' && grep -aq 'UYG-TEST: DEDE hata=0' '$L'"
  fi
  d "[$K] SES-TON-TEST kuyruga verildi ve bosaldi" "grep -aq 'SES-TON-TEST: .*sonuc=1 bosaldi=1' '$L'"
  d "[$K] WAV kaydinda kare dalga ornek ornek ($E / 14352 kare)" "[ $E -ge 14352 ]"
  d "[$K] panik yok" "! grep -aq 'PANIGI\|PAGE FAULT' '$L'"
done
