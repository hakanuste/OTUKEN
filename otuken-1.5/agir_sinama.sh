#!/bin/bash
# agir_sinama.sh - OTUKEN AGIR SINAMA matrisi (stres + soak + sizinti).
#
# Tani cekirdegini OTUKEN_AGIR_SINAMA=1 ile derler ve QEMU'da su eksenlerde
# tekrar tekrar acar:
#   - cekirdek sayisi (-smp), CPU modeli (-cpu), bellek (-m)
#   - her hucre TEKRAR kez (arada kalan kilitlenme/yaris icin oran olcer)
#   - sonda uzun SOAK (cok tur) + sizinti egimi
# Her kosu: SMP-YARIS + AGIR (8 gorev x N tur; yigin, dosya, boru, kuyruk,
# mutex) ve sonunda "AGIR-SINAMA: GECTI" bekler. Takilma = zaman asimi.
#
# Kullanim: ./agir_sinama.sh [TEKRAR=3] [SOAK_TUR=64]
# Cikti: kanitlar/agir/ altinda her kosunun seri kaydi + ozet.txt
# Cikis kodu: 0 yalniz her kosu GECTI ise.

set -u
cd "$(dirname "$0")"
TEKRAR=${1:-3}
SOAK_TUR=${2:-64}
K=kanitlar/agir
mkdir -p "$K"; rm -f "$K"/*.log "$K"/ozet.txt
TEMEL="-mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -Wextra -O2 -Iinclude"

derle() {   # $1 = ek CFLAGS ; iso'yu $2 adina kopyalar
  make clean >/dev/null 2>&1
  if make CFLAGS="$1 $TEMEL" 2>&1 | grep -Ei "warning|error"; then echo "DERLEME UYARI/HATA"; exit 2; fi
  make iso >/dev/null 2>&1 || { echo "ISO HATASI"; exit 2; }
  cp otukenos-mini.iso "$2"
}

toplam=0; gecti=0; takildi=0; kaldi=0
kos() {     # $1 etiket $2 iso $3 smp $4 cpu $5 mem $6 sure
  local L="$K/$1.log" img; img=$(mktemp /tmp/agir_XXXX.img)
  cp diskfiles.img "$img"
  local bas=$SECONDS
  timeout "$6" qemu-system-x86_64 -cpu "$4" -cdrom "$2" \
    -drive file="$img",format=raw,if=ide,index=0 \
    -drive file=fat32test.img,format=raw,if=ide,index=1,snapshot=on \
    -netdev user,id=n0 -device e1000,netdev=n0 -audiodev none,id=ses0 -device AC97,audiodev=ses0 \
    -m "$5" -smp "$3" -display none -serial file:"$L" -no-reboot -boot d &
  local pid=$!
  # AGIR-SINAMA sonucu gorulunce erken kapat
  while kill -0 $pid 2>/dev/null; do
    grep -aq "^AGIR-SINAMA: \(GECTI\|KALDI\)" "$L" 2>/dev/null && { sleep 1; kill $pid 2>/dev/null; break; }
    sleep 1
  done
  wait $pid 2>/dev/null; rm -f "$img"
  local sure=$((SECONDS-bas)) durum
  local panik; panik=$(grep -ac "PANIGI\|PAGE FAULT\|GENERAL PROTECTION" "$L")
  if [ "$panik" -gt 0 ]; then durum="PANIK"; kaldi=$((kaldi+1))
  elif grep -aq "^AGIR-SINAMA: GECTI" "$L"; then durum="GECTI"; gecti=$((gecti+1))
  elif grep -aq "^AGIR-SINAMA: KALDI" "$L"; then durum="KALDI"; kaldi=$((kaldi+1))
  else durum="TAKILDI"; takildi=$((takildi+1)); fi
  toplam=$((toplam+1))
  local yaris; yaris=$(grep -ao "SMP-YARIS: [A-Z]*" "$L" | head -1)
  local siz; siz=$(grep -ao "SIZINTI [A-Z]*" "$L" | head -1)
  local turs; turs=$(grep -ac "^AGIR-TUR GECTI\|AGIR-TUR GECTI" "$L")
  printf "%-28s smp=%-2s cpu=%-6s m=%-4s %-7s sure=%3ss tur_gecti=%s %s %s\n" \
    "$1" "$3" "$4" "$5" "$durum" "$sure" "$turs" "${yaris:-YARIS-yok}" "${siz:-}" | tee -a "$K/ozet.txt"
}

echo "== derleme: tani + agir (8 tur)"
derle "-DOTUKEN_TANI_MODU=1 -DOTUKEN_AGIR_SINAMA=1" /tmp/agir_8.iso
echo "== derleme: tani + agir (soak $SOAK_TUR tur)"
derle "-DOTUKEN_TANI_MODU=1 -DOTUKEN_AGIR_SINAMA=1 -DOTUKEN_AGIR_TUR=$SOAK_TUR" /tmp/agir_soak.iso

echo "== derleme: KONTROL GRUBU (bilerek sizinti)"
derle "-DOTUKEN_TANI_MODU=1 -DOTUKEN_AGIR_SINAMA=1 -DAGIR_KONTROL_SIZINTI" /tmp/agir_kontrol.iso

echo "== KONTROL GRUBU: KALDI (SIZINTI VAR) beklenir" | tee -a "$K/ozet.txt"
kos "kontrol_sizinti_s4" /tmp/agir_kontrol.iso 4 max 512 240
if grep -aq "SIZINTI VAR" "$K/kontrol_sizinti_s4.log"; then
  echo "KONTROL: olcut bilerek konan sizintiyi YAKALADI (dogru)" | tee -a "$K/ozet.txt"
  toplam=$((toplam-1)); kaldi=$((kaldi-1))       # beklenen KALDI sayilmaz
else
  echo "KONTROL: olcut sizintiyi YAKALAYAMADI -> olcut GECERSIZ" | tee -a "$K/ozet.txt"; kaldi=$((kaldi+1))
fi

echo "== MATRIS (tekrar=$TEKRAR)" | tee -a "$K/ozet.txt"
for smp in 1 2 4 8 16; do
  for cpu in max qemu64 EPYC; do
    for t in $(seq 1 "$TEKRAR"); do
      kos "m_s${smp}_${cpu}_t${t}" /tmp/agir_8.iso $smp $cpu 512 240
    done
  done
done
echo "== BELLEK kenarlari" | tee -a "$K/ozet.txt"
kos "bellek_128_s4" /tmp/agir_8.iso 4 max 128 240
kos "bellek_2048_s4" /tmp/agir_8.iso 4 max 2048 240
echo "== SOAK ($SOAK_TUR tur)" | tee -a "$K/ozet.txt"
kos "soak_s4" /tmp/agir_soak.iso 4 max 512 1200
kos "soak_s8" /tmp/agir_soak.iso 8 max 512 1200

echo "== TOPLAM kosu=$toplam gecti=$gecti kaldi=$kaldi takildi=$takildi" | tee -a "$K/ozet.txt"
# uretim cekirdegini geri kur
make clean >/dev/null 2>&1; make >/dev/null 2>&1; make iso >/dev/null 2>&1
[ "$gecti" -eq "$toplam" ]
