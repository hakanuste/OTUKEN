#!/bin/bash
# ÖTÜKEN otomatik test kosum takimi (Asama 36)
# Her iki mimariyi (x86_64 ve ARM64) derler, calistirir ve
# beklenen sonuclari dogrular. Tek komutla tam regresyon testi.

set -u
KOK="$(cd "$(dirname "$0")" && pwd)"
cd "$KOK"
GECEN=0; KALAN=0
# --- Denetim duzeltmesi: her bolumden ONCE artik QEMU/sunucu
# sureclerini temizle. Takili kalan QEMU, disk imajini kilitleyip
# sonraki testlerin "Failed to get write lock" ile patlamasina
# yol aciyordu. ---
temizle(){
  pkill -9 -f qemu-system-x86_64 >/dev/null 2>&1
  pkill -9 -f qemu-system-aarch64 >/dev/null 2>&1
  pkill -9 -f "http.server" >/dev/null 2>&1
  sleep 1
}
trap temizle EXIT

mavi(){ temizle; printf "\n\033[1m== %s ==\033[0m\n" "$1"; }
ok(){   printf "  [GECTI] %s\n" "$1"; GECEN=$((GECEN+1)); }
hata(){ printf "  [KALDI] %s\n" "$1"; KALAN=$((KALAN+1)); }
kontrol(){ # kontrol "ad" "desen" "dosya"
  if grep -aq "$2" "$3" 2>/dev/null; then ok "$1"; else hata "$1 (aranan: $2)"; fi
}
yok(){     # yok "ad" "olmamasi gereken desen" "dosya"
  if grep -aq "$2" "$3" 2>/dev/null; then hata "$1 ($2 BULUNDU)"; else ok "$1"; fi
}

mavi "1) x86_64 derleme"
if make clean >/dev/null 2>&1 && make >/dev/null 2>&1 && make iso >/dev/null 2>&1; then
  ok "x86_64 derlendi"
else hata "x86_64 derleme"; fi

UYARI=$(make 2>&1 | grep -c "warning:" || true)
[ "$UYARI" -eq 0 ] && ok "derleyici uyarisi yok" || hata "derleyici uyarisi: $UYARI"

mavi "2) x86_64 uretim modu"
cp -f diskfiles.img /tmp/t_d1.img 2>/dev/null
LOG=/tmp/t_uretim.log
timeout 20 qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_d1.img,format=raw,if=ide,index=0 \
  -netdev user,id=n0 -device e1000,netdev=n0 \
  -m 128 -display none -serial stdio -no-reboot -boot d > "$LOG" 2>&1
yok      "panik yok"              "PANIGI"            "$LOG"
yok      "sayfa hatasi yok"       "PAGE FAULT"        "$LOG"
yok      "uyari/hata kaydi yok"   "\[UYARI\]\|\[HATA\]" "$LOG"
kontrol  "cekirdek acildi"        "otukenOS baslatma" "$LOG"
kontrol  "surucular aktif"        "driver: toplam"    "$LOG"
kontrol  "BITIG dosya sistemi"    "BITIG"             "$LOG"
kontrol  "DHCP yapilandirmasi"    "adres yapilandirmasi alindi" "$LOG"
kontrol  "DNS cozumlemesi"        "ad cozumlemesi basarili"     "$LOG"

kontrol  "servis yoneticisi"      "servis yoneticisi" "$LOG"
kontrol  "ULGEN compositor"       "compositor"        "$LOG"
kontrol  "KERVAN paket sistemi"   "paket kuruldu"     "$LOG"
kontrol  "kabuk baslatildi"       "Kabugu 2.0"        "$LOG"
kontrol  "surum kimligi"          "OTUKEN 1.0 HAZIR"  "$LOG"
# UMAY varsayilan derlemede BAGLI DEGIL (OTUKEN_UMAY_BAGLI=0).
# Burada UMAY'in kosmasini degil, BILEREK baglanmadigini dogruluyoruz:
# sessizce yok olmasi ile kapali olmasi ayri seylerdir.
kontrol  "UMAY baglantisi kapali" "UMAY-HOST: baglanti kapali" "$LOG"
yok      "UMAY yuklenmedi"        "UMAY-HOST.*diskten okundu"  "$LOG"
kontrol  "UMAY destek katmani ayakta" "\[UMAY\] model yuklendi" "$LOG"
kontrol  "kararlilik izleme"      "KARARLILIK"        "$LOG"

mavi "3) x86_64 tani modu (tum oz-sinamalar)"
make clean >/dev/null 2>&1
make CFLAGS="-DOTUKEN_TANI_MODU=1 -DOTUKEN_UMAY_BAGLI=1 -mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -Wextra -O2 -Iinclude" >/dev/null 2>&1
make iso >/dev/null 2>&1
cp -f diskfiles.img /tmp/t_d2.img 2>/dev/null
LOGT=/tmp/t_tani.log
timeout 60 qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_d2.img,format=raw,if=ide,index=0 \
  -drive file=fat32test.img,format=raw,if=ide,index=1 \
  -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
  -netdev user,id=n0 -device e1000,netdev=n0 \
  -m 128 -display none -serial stdio -no-reboot -boot d > "$LOGT" 2>&1
BASARILI=$(grep -ac "BASARILI" "$LOGT" || true)
SINAMA_HATA=$(grep -a "\-TEST:" "$LOGT" | grep -ac "HATA" || true)
printf "  sinama sonucu: %s BASARILI, %s HATA\n" "$BASARILI" "$SINAMA_HATA"
[ "$BASARILI" -ge 20 ] && ok "en az 20 sinama gecti" || hata "yetersiz sinama: $BASARILI"
[ "$SINAMA_HATA" -eq 0 ] && ok "basarisiz sinama yok" || hata "basarisiz sinama: $SINAMA_HATA"
yok "tani modunda panik yok" "PANIGI" "$LOGT"
kontrol "baski sinamasi temiz"   "BASKI-TEST.*BASARILI" "$LOGT"
kontrol "harf duyarsiz dosya"    "HARF-TEST.*BASARILI"  "$LOGT"
kontrol "iskence: syscall"       "ISKENCE: syscall.*SAGLAM"   "$LOGT"
kontrol "iskence: bellek"        "ISKENCE: bellek.*SAGLAM"    "$LOGT"
kontrol "iskence: dosya"         "ISKENCE: dosya.*SAGLAM"     "$LOGT"
kontrol "iskence: ag"            "ISKENCE: ag.*SAGLAM"        "$LOGT"
kontrol "iskence: ipc"           "ISKENCE: ipc.*SAGLAM"       "$LOGT"
kontrol "iskence: gorev"         "ISKENCE: gorev.*SAGLAM"     "$LOGT"
kontrol "iskence: isaretci saldirisi" "ISKENCE: isaretci.*SAGLAM" "$LOGT"
kontrol "iskence: CR3 degisimi guvenligi" "ISKENCE: cr3.*SAGLAM" "$LOGT"
kontrol "iskence toplam temiz"   "ISKENCE-TOPLAM.*SAGLAM"     "$LOGT"
kontrol "GUVENLIK: tum korumalar" "GUVENLIK-OZET.*TUM KORUMALAR CALISIYOR" "$LOGT"
kontrol "GUVENLIK: kernel ELF redi" "GUVENLIK-1.*REDDEDILDI"  "$LOGT"
kontrol "GUVENLIK: tasma redi"      "GUVENLIK-2.*REDDEDILDI"  "$LOGT"
kontrol "GUVENLIK: kernel ptr redi" "GUVENLIK-3.*REDDEDILDI"  "$LOGT"
kontrol "GUVENLIK: PML4[511] kapali" "GUVENLIK-5.*KAPALI"     "$LOGT"
kontrol "surec adres alani atandi"  "adres alanina baglandi"  "$LOGT"
kontrol "surec basina heap"         "heap penceresi"          "$LOGT"
kontrol "UMAY sozlesme uygulandi" "UMAY-TEST.*BASARILI"  "$LOGT"
if grep -aq "TCP-SENTETIK.*BASARILI" "$LOGT"; then
  printf "  (bilgi) TCP durum makinesi sentetik testi gecti - ag kaniti DEGIL\n"
fi
yok     "bellek sizintisi yok"   "sizinti suphesi -> VAR" "$LOGT"

mavi "4) USB denetleyici tespiti"
LOGU=/tmp/t_usb.log
timeout 18 qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_d2.img,format=raw,if=ide,index=0 \
  -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
  -m 128 -display none -serial stdio -no-reboot -boot d > "$LOGU" 2>&1
kontrol "XHCI bulundu" "USB: XHCI" "$LOGU"
kontrol "bagli cihaz algilandi" "bagli=0x00000001" "$LOGU"

mavi "5) ARM64 TEMEL port (tam ozellik degil - kapsam sinirli)"
if (cd arm64 && make clean >/dev/null 2>&1 && make >/dev/null 2>&1); then
  ok "ARM64 (temel port) derlendi"
  LOGA=/tmp/t_arm.log
  timeout 15 qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 \
    -nographic -kernel arm64/otuken-arm64.img > "$LOGA" 2>&1
  kontrol "ARM64 cekirdegi acildi" "OTUKEN (ARM64" "$LOGA"
kontrol "ARM64 kapsam durust bildiriliyor" "TEMEL seviyededir" "$LOGA"
  kontrol "ARM64 EL1 seviyesi"     "EL1"           "$LOGA"
  kontrol "ARM64 PMM sinamasi"     "PMM-SINAMA.*BASARILI" "$LOGA"
  kontrol "ARM64 heap sinamasi"    "HEAP-SINAMA.*BASARILI" "$LOGA"
  kontrol "ARM64 64-bit sinamasi"  "64BIT-SINAMA.*BASARILI" "$LOGA"
else hata "ARM64 derleme"; fi

# uretim moduna geri don
make clean >/dev/null 2>&1; make >/dev/null 2>&1; make iso >/dev/null 2>&1

mavi "7) AHCI gercek veri okuma (DMA)"
cp -f diskfiles.img /tmp/t_sata.img 2>/dev/null
cp -f diskfiles.img /tmp/t_ide.img 2>/dev/null
LOGH=/tmp/t_ahci.log
timeout 20 qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_ide.img,format=raw,if=ide,index=0 \
  -device ahci,id=hba -drive id=sdrv,file=/tmp/t_sata.img,if=none,format=raw \
  -device ide-hd,drive=sdrv,bus=hba.0 \
  -m 128 -display none -serial stdio -no-reboot -boot d > "$LOGH" 2>&1
kontrol "AHCI port hazirlandi" "komut yapilari hazir" "$LOGH"
kontrol "AHCI sektor okundu"   "AHCI-OKUMA.*BASARILI" "$LOGH"
yok     "AHCI okuma panik yok" "PANIGI" "$LOGH"

mavi "8) USB aktarim katmani (UHCI + HID klavye)"
cp -f diskfiles.img /tmp/t_usb.img 2>/dev/null
LOGU2=/tmp/t_uhci.log
timeout 22 qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_usb.img,format=raw,if=ide,index=0 \
  -device piix3-usb-uhci,id=uh -device usb-kbd,bus=uh.0 \
  -m 128 -display none -serial stdio -no-reboot -boot d > "$LOGU2" 2>&1
kontrol "UHCI denetleyici hazir"  "UHCI: denetleyici hazir" "$LOGU2"
kontrol "USB cihaz algilandi"     "cihaz bagli"             "$LOGU2"
kontrol "USB klavye numaralandi"  "USB klavye numaralandirildi" "$LOGU2"
yok     "USB panik yok"           "PANIGI"                  "$LOGU2"

mavi "9) TCP: GERCEK ag el sikismasi (dinleyen sunucuya karsi)"
# Sentetik durum makinesi testi PASS SAYILMAZ; burada gercek bir
# TCP sunucusu ayaga kaldirilip SYN -> SYN-ACK -> ACK dizisi
# ag uzerinde dogrulanir.
pkill -9 -f "http.server" >/dev/null 2>&1
python3 -m http.server 18099 --bind 127.0.0.1 >/dev/null 2>&1 </dev/null &
HTTPPID=$!
sleep 1
cp -f diskfiles.img /tmp/t_tcp.img 2>/dev/null
LOGTCP=/tmp/t_tcp.log
timeout 24 qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_tcp.img,format=raw,if=ide,index=0 \
  -netdev "user,id=n0,guestfwd=tcp:10.0.2.100:8080-tcp:127.0.0.1:18099" \
  -device e1000,netdev=n0 \
  -m 128 -display none -serial stdio -no-reboot -boot d > "$LOGTCP" 2>&1
kill -9 $HTTPPID >/dev/null 2>&1
if grep -aq "GERCEK AG handshake tamam" "$LOGTCP"; then
  ok "TCP gercek ag el sikismasi (SYN/SYN-ACK/ACK)"
else
  hata "TCP gercek ag el sikismasi DOGRULANAMADI"
  grep -a "ULAK-TCP" "$LOGTCP" | tail -3 | sed 's/^/    /'
fi
yok "TCP testinde panik yok" "PANIGI" "$LOGTCP"

mavi "11) ELF guvenlik oznitelikleri (GNU_STACK calistirilamaz olmali)"
gnu_kontrol() {  # gnu_kontrol "ad" "dosya"
  if [ ! -f "$2" ]; then hata "$1 (dosya yok)"; return; fi
  SATIR=$(readelf -l "$2" 2>/dev/null | grep -A1 "GNU_STACK" | tail -1)
  if [ -z "$SATIR" ]; then hata "$1 (GNU_STACK basligi YOK)"; return; fi
  if echo "$SATIR" | grep -q "RWE"; then hata "$1 (GNU_STACK RWE - calistirilabilir yigin)"
  else ok "$1 (GNU_STACK calistirilamaz)"; fi
}
gnu_kontrol "cekirdek"      otukenos-mini.elf
gnu_kontrol "kabuk"         userprog/shell.elf
gnu_kontrol "UMAY"          userprog/umay.elf
gnu_kontrol "ARM64"         arm64/otuken-arm64.elf

mavi "10) Yol haritasi asama kaniti (TCP icin GERCEK ag logu kullanilir)"
if python3 asama_dogrula.py "$LOGT" "$LOGTCP" > /tmp/asama_sonuc.txt 2>&1; then
  ok "tum asamalarin kaniti bulundu"
  grep -c "GECTI" /tmp/asama_sonuc.txt | while read n; do printf "  (%s asama kaniti dogrulandi)\n" "$n"; done
else
  hata "bazi asamalarin kaniti yok"
  grep "KALDI" /tmp/asama_sonuc.txt | head -5
fi


mavi "12) Asama 42: US1 motoru, ses ve UMAY"
kontrol "DEDE cozumlemesi dogru"      "DEDE-TEST: cozumleme dogru" "$LOGT"
kontrol "FAT16 alt dizin calisiyor"   "DIZIN-TEST: alt dizin olusuyor" "$LOGT"
kontrol "VFS alt dizini goruyor"      "VFS-DIZIN-TEST: stat ve listele" "$LOGT"
kontrol "US1 modeli yuklendi"         "US-YEREL: model yüklendi" "$LOGT"
kontrol "gercek motor etkin"          "US: us1-yerel etkin" "$LOGT"
# Ses su an BILEREK KAPALI (UMAY_SES_ACIK=0): sentezleyici konusuyor
# ama sozcukler net degil, bu yuzden UMAY yalniz yaziyla cevap veriyor.
# Sinama "ses yok"u degil, KAPALI OLDUGUNUN acikca bildirildigini
# dogruluyor: sessizce atlanan bir ses katmani ile acilamayan bir ses
# katmani ayni goruntuyu verirdi.
# NOT: desende ASCII disi harf KULLANMA. Turkce harfler UTF-8'de iki
# bayttir; "kapal." gibi bir desen "ı"nin yalniz ilk baytini yakalar
# ve eslesme tutmaz. Bu tuzaga bir kez dusuldu.
kontrol "ses kapali oldugu bildiriliyor" "Ses: kapal" "$LOGT"
kontrol "UMAY yaziyla yanit veriyor"     "UMAY_SES_ACIK=0" "$LOGT"
kontrol "UMAY kutuphanesi doldu"      "Bilgi: 21 doktrin, 4 bitig" "$LOGT"
kontrol "UMAY diskten yuklendi"       "UMAY-HOST.*diskten okundu"  "$LOGT"
kontrol "UMAY Ring3'te calisiyor"     "UMAY-HOST: Ring3.e geciliyor" "$LOGT"

mavi "13) DEDE sesi: uretilen ses OLCULEREK sinanir"
# NEDEN BURADA: ses kalitesi uzun sure "kulakla bakariz" diye
# birakilmisti ve bu ortamda kulak yok. Sonuc: sentezleyici aylarca
# yanlis formant frekanslarinda calisti (kosinus yaklasimindaki hata
# butun formantlari %25 yukari kaydiriyordu) ve kimse fark etmedi.
# Artik cekirdegin kendi dede.c'si ana bilgisayarda derleniyor,
# uretilen dalga olculuyor ve unluler ayirt edilebiliyor mu diye
# bakiliyor. Ses artik gerileme sinamasina giriyor.
if [ -x ses_sinama/sina.sh ]; then
  while IFS= read -r satir; do
    case "$satir" in
      *"[GECTI]"*) ok "${satir#*\[GECTI\] }" ;;
      *"[KALDI]"*) hata "${satir#*\[KALDI\] }" ;;
    esac
  done < <(bash ses_sinama/sina.sh "$KOK" 2>&1)
else
  hata "ses_sinama/sina.sh yok"
fi

mavi "SONUC"
printf "  GECEN: %s   KALAN: %s\n\n" "$GECEN" "$KALAN"
[ "$KALAN" -eq 0 ] && exit 0 || exit 1
