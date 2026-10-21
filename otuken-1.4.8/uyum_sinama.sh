#!/bin/bash
# uyum_sinama.sh - OTUKEN <-> UMAY UYUM sinamasi (yuk altinda).
#
# Tani cekirdegini OTUKEN_UMAY_UYUM=1 (+ istege bagli AGIR) ile derler.
# Cekirdekte 3 istemci gorev, KENDI portlarindan Ring3 UMAY'a ayni anda
# sirayla soru sorar ve her sorunun TAM yanitini bekler. Yanit YANLIS
# istemciye giderse (zaman asimi + fazla ileti), parca kaybolursa
# (eksik) ya da yanit bos gelirse sinama KALDI verir.
#
#   1) KONTROL GRUBU: UMAY eski yonlendirmeyle (-DUMAY_ESKI_YONLENDIRME,
#      'son gonderene yanit') derlenir; sinama bunu YAKALAMALI.
#   2) Taban: yalniz uyum (AGIR yok), -smp 4.
#   3) Matris: uyum + AGIR (32 tur) AYNI ANDA, -smp 1/2/4/8 x TEKRAR.
#   4) Kabuk yolu (Ring3 kabuk -> SYS_IPC_GONDER -> UMAY): kabuk_testi.py
#      -smp 1 ve -smp 4.
#
# Kullanim: ./uyum_sinama.sh [TEKRAR=2]
# Cikti: kanitlar/uyum/ozet.txt + her kosunun seri kaydi
# Cikis kodu: 0 yalniz her kosu GECTI ve kontrol grubu YAKALANDI ise.

set -u
cd "$(dirname "$0")"
TEKRAR=${1:-2}
K=kanitlar/uyum
mkdir -p "$K"; rm -f "$K"/*.log "$K"/ozet.txt
TEMEL="-mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -Wextra -O2 -Iinclude"

derle() {   # $1 ek CFLAGS, $2 iso adi
  make clean >/dev/null 2>&1
  if make CFLAGS="$1 $TEMEL" 2>&1 | grep -Ei "warning|error"; then echo "DERLEME UYARI/HATA"; exit 2; fi
  make iso >/dev/null 2>&1 || { echo "ISO HATASI"; exit 2; }
  cp otukenos-mini.iso "$2"
}

# --- UMAY diskleri: normal + kontrol grubu ---
make -C umay temiz >/dev/null 2>&1
make -C umay CFLAGS_ORTAK="-std=c11 -Wall -Wextra -Wshadow -Iinclude -I../us1/c -DUMAY_ESKI_YONLENDIRME" 2>&1 | grep -Ei "warning|error" && exit 2
cp diskfiles.img /tmp/uyum_disk_kontrol.img
mcopy -o -i /tmp/uyum_disk_kontrol.img umay/kov/umay ::UMAY.ELF
make -C umay temiz >/dev/null 2>&1
make -C umay 2>&1 | grep -Ei "warning|error" && exit 2
make umay-disk >/dev/null 2>&1
cp diskfiles.img /tmp/uyum_disk.img

toplam=0; gecti=0; kaldi=0; takildi=0
kos() {     # $1 etiket $2 iso $3 disk $4 smp $5 sure $6 agir(0/1)
  local L="$K/$1.log" img; img=$(mktemp /tmp/uyum_XXXX.img)
  cp "$3" "$img"
  local bas=$SECONDS
  timeout "$5" qemu-system-x86_64 -cpu max -cdrom "$2" \
    -drive file="$img",format=raw,if=ide,index=0 \
    -drive file=fat32test.img,format=raw,if=ide,index=1,snapshot=on \
    -netdev user,id=n0 -device e1000,netdev=n0 -audiodev none,id=ses0 -device AC97,audiodev=ses0 \
    -m 512 -smp "$4" -display none -serial file:"$L" -no-reboot -boot d &
  local pid=$!
  while kill -0 $pid 2>/dev/null; do
    if grep -aq "^UMAY-UYUM: " "$L" 2>/dev/null; then
      if [ "$6" = 0 ] || grep -aq "^AGIR-SINAMA: \(GECTI\|KALDI\)" "$L"; then sleep 1; kill $pid 2>/dev/null; break; fi
    fi
    sleep 1
  done
  wait $pid 2>/dev/null; rm -f "$img"
  local sure=$((SECONDS-bas)) durum
  local panik; panik=$(grep -ac "PANIGI\|PAGE FAULT\|GENERAL PROTECTION" "$L")
  if [ "$panik" -gt 0 ]; then durum="PANIK"
  elif ! grep -aq "^UMAY-UYUM: " "$L"; then durum="TAKILDI"
  elif grep -aq "^UMAY-UYUM: GECTI" "$L" && { [ "$6" = 0 ] || grep -aq "^AGIR-SINAMA: GECTI" "$L"; }; then durum="GECTI"
  else durum="KALDI"; fi
  if [ "${KONTROL:-0}" = 0 ]; then
    toplam=$((toplam+1))
    case $durum in GECTI) gecti=$((gecti+1));; TAKILDI) takildi=$((takildi+1));; *) kaldi=$((kaldi+1));; esac
  fi
  local uy; uy=$(grep -ao "^UMAY-UYUM: .*" "$L" | head -1 | sed 's/UMAY-UYUM: [A-Z]* //')
  local ag; ag=$(grep -ao "^AGIR-SINAMA: [A-Z]*" "$L" | tail -1)
  local gk; gk=$(grep -ao "en_gecikme_tik=0x[0-9A-F]*" "$L" | sed 's/.*=//' | sort | tail -1)
  printf "%-22s smp=%-2s agir=%s %-7s sure=%3ss en_gecikme_tik=%s %s %s\n" \
    "$1" "$4" "$6" "$durum" "$sure" "${gk:-?}" "${ag:-}" "$uy" | tee -a "$K/ozet.txt"
  LAST=$durum
}

echo "== derleme: tani + uyum"
derle "-DOTUKEN_TANI_MODU=1 -DOTUKEN_UMAY_UYUM=1" /tmp/uyum.iso
echo "== derleme: tani + uyum + agir (32 tur)"
derle "-DOTUKEN_TANI_MODU=1 -DOTUKEN_UMAY_UYUM=1 -DOTUKEN_AGIR_SINAMA=1 -DOTUKEN_AGIR_TUR=32" /tmp/uyum_agir.iso

echo "== KONTROL GRUBU: eski yonlendirmeli UMAY -> KALDI beklenir" | tee -a "$K/ozet.txt"
yakalandi=0
for t in 1 2; do
  KONTROL=1 kos "kontrol_eski_yon_t$t" /tmp/uyum.iso /tmp/uyum_disk_kontrol.img 4 600 0
  [ "$LAST" = KALDI ] && yakalandi=$((yakalandi+1))
done
if [ $yakalandi -ge 1 ]; then echo "KONTROL: sinama eski yonlendirme hatasini YAKALADI ($yakalandi/2)" | tee -a "$K/ozet.txt"
else echo "KONTROL: sinama eski hatayi YAKALAYAMADI -> sinama GECERSIZ" | tee -a "$K/ozet.txt"; kaldi=$((kaldi+1)); fi

echo "== TABAN: yalniz uyum" | tee -a "$K/ozet.txt"
kos "taban_s4_t1" /tmp/uyum.iso /tmp/uyum_disk.img 4 600 0
kos "taban_s4_t2" /tmp/uyum.iso /tmp/uyum_disk.img 4 600 0

echo "== MATRIS: uyum + AGIR ayni anda (tekrar=$TEKRAR)" | tee -a "$K/ozet.txt"
for smp in 1 2 4 8; do
  for t in $(seq 1 "$TEKRAR"); do
    kos "yuk_s${smp}_t${t}" /tmp/uyum_agir.iso /tmp/uyum_disk.img $smp 900 1
  done
done

echo "== TOPLAM kosu=$toplam gecti=$gecti kaldi=$kaldi takildi=$takildi" | tee -a "$K/ozet.txt"
make clean >/dev/null 2>&1; make >/dev/null 2>&1; make iso >/dev/null 2>&1
[ "$gecti" -eq "$toplam" ]
