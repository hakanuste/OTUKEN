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
kontrol  "UMAY calisma zamani"    "\[UMAY\] model yuklendi" "$LOG"
kontrol  "UMAY DISKTEN yuklendi"  "UMAY-HOST.*diskten okundu" "$LOG"
kontrol  "UMAY Ring3'te calisiyor" "\[UMAY\] kullanici alaninda" "$LOG"
kontrol  "UMAY IPC portu acti"    "port acildi 'umay'"   "$LOG"
kontrol  "kararlilik izleme"      "KARARLILIK"        "$LOG"

mavi "3) x86_64 tani modu (tum oz-sinamalar)"
make clean >/dev/null 2>&1
make CFLAGS="-DOTUKEN_TANI_MODU=1 -mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -Wextra -O2 -Iinclude" >/dev/null 2>&1
make iso >/dev/null 2>&1
cp -f diskfiles.img /tmp/t_d2.img 2>/dev/null
LOGT=/tmp/t_tani.log
timeout 60 qemu-system-x86_64 -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_d2.img,format=raw,if=ide,index=0 \
  -drive file=fat32test.img,format=raw,if=ide,index=1,snapshot=on \
  -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
  -netdev user,id=n0 -device e1000,netdev=n0 \
  -m 128 -display none -serial stdio -no-reboot -boot d > "$LOGT" 2>&1
# SMP adim 6: AYNI tani ISO'su -smp 4 ile (denetimler bolum 13'te).
LOGS4=/tmp/t_tani_smp4.log
cp -f diskfiles.img /tmp/t_d4.img 2>/dev/null
timeout 110 qemu-system-x86_64 -cpu max -cdrom otukenos-mini.iso \
  -drive file=/tmp/t_d4.img,format=raw,if=ide,index=0 \
  -drive file=fat32test.img,format=raw,if=ide,index=1,snapshot=on \
  -netdev user,id=n0 -device e1000,netdev=n0 \
  -m 512 -smp 4 -display none -serial stdio -no-reboot -boot d > "$LOGS4" 2>&1
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
kontrol "iskence: cekirdek-basina veri" "ISKENCE: cekirdek-basina.*SAGLAM" "$LOGT"
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
make -C umay >/dev/null 2>&1        # gercek UMAY (diskteki UMAY.ELF)
gnu_kontrol "UMAY"          umay/kov/umay
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
kontrol "SMP-YARIS tek cekirdek (kesilme altinda)" "SMP-YARIS: GECTI gorev=0x00000006 hata=0x00000000" "$LOGT"
kontrol "kimlik haritasinda delik yok" "KIMLIK-TEST: kimlik haritasinda delik yok" "$LOGT"
kontrol "DEDE cozumlemesi dogru"      "DEDE-TEST: cozumleme dogru" "$LOGT"
kontrol "FAT16 alt dizin calisiyor"   "DIZIN-TEST: alt dizin olusuyor" "$LOGT"
kontrol "VFS alt dizini goruyor"      "VFS-DIZIN-TEST: stat ve listele" "$LOGT"
kontrol "US1 modeli yuklendi"         "US-YEREL: model yüklendi" "$LOGT"
kontrol "gercek motor etkin"          "US: us1-yerel etkin" "$LOGT"
kontrol "ses surucusu takildi"        "Ses: dede-korkut" "$LOGT"
kontrol "UMAY kutuphanesi doldu"      "Bilgi: 21 doktrin, 4 bitig" "$LOGT"

mavi "13) SMP adim 6: 4 cekirdekte is dagitimi + yaris (TANI, -smp 4)"
# (QEMU kosusu yukarida, tani ISO'su yerindeyken yapildi: $LOGS4)
kontrol "AP'ler zamanlayiciya katildi"  "zamanlayiciya katilan AP=3/3" "$LOGS4"
# Is en az bir AP'de kosmali (>= 2 cekirdek). 4 cekirdegin HEPSININ is
# almasi sart degil: 6 kisa gorevle bir cekirdegin bos kalmasi olasidir
# (ilk surumde =4 aranip bir kosuda maske=0xB ile yanlis KALDI verdi).
kontrol "SMP-YARIS 0 hata, is AP'lerde kostu" "SMP-YARIS: GECTI gorev=0x00000006 hata=0x00000000 cekirdek_sayisi=0x0000000[2-9]" "$LOGS4"
kontrol "iskence -smp 4 temiz"          "ISKENCE-TOPLAM.*SAGLAM" "$LOGS4"
kontrol "UMAY -smp 4 ile acildi"        "US: us1-yerel etkin" "$LOGS4"
yok     "-smp 4 panik yok"              "PANIGI\|PAGE FAULT" "$LOGS4"

mavi "14) Depolama: HER disk okunur VE yazilir (ATA / AHCI / NVMe, FAT32 yazma)"
# Her kosuda kernel orta sektoru okur, ters desenle yazar, onbellek disindan
# okuyup dogrular ve eski icerigi geri yazar (disk-yazma-test).
dy(){ # dy ad sure -- qemu bayraklari...
  local ad=$1; shift; shift
  timeout -s KILL 30 qemu-system-x86_64 -cdrom otukenos-mini.iso "$@" \
    -m 256 -display none -serial stdio -no-reboot -boot d > /tmp/t_dy_$ad.log 2>&1
}
for k in ide hdb sata nv sata2 nv2 ds1 ds2 ds3; do cp -f diskfiles.img /tmp/t_dy_$k.img 2>/dev/null; done
dy ata 0 -drive file=/tmp/t_dy_ide.img,format=raw,if=ide,index=0 \
  -drive file=/tmp/t_dy_hdb.img,format=raw,if=ide,index=1
kontrol "ATA disk 0 oku+yaz"   "disk-yazma-test: disk 0x00000000 OKUMA+YAZMA BASARILI" /tmp/t_dy_ata.log
kontrol "ATA disk 1 oku+yaz"   "disk-yazma-test: disk 0x00000001 OKUMA+YAZMA BASARILI" /tmp/t_dy_ata.log
dy ahci 0 -drive file=/tmp/t_dy_ds1.img,format=raw,if=ide,index=0 \
  -device ahci,id=hba -drive id=sd,file=/tmp/t_dy_sata.img,if=none,format=raw -device ide-hd,drive=sd,bus=hba.0
kontrol "AHCI disk 1 (IDE+SATA) oku+yaz" "disk-yazma-test: disk 0x00000001 OKUMA+YAZMA BASARILI" /tmp/t_dy_ahci.log
kontrol "AHCI birimi atandi"   "birim 0x00000001 -> AHCI" /tmp/t_dy_ahci.log
dy ahcio 0 -device ahci,id=hba -drive id=sd,file=/tmp/t_dy_sata2.img,if=none,format=raw -device ide-hd,drive=sd,bus=hba.0
kontrol "SATA-only disk 0 oku+yaz (IDE yok)" "disk-yazma-test: disk 0x00000000 OKUMA+YAZMA BASARILI" /tmp/t_dy_ahcio.log
dy nvme 0 -drive file=/tmp/t_dy_ds2.img,format=raw,if=ide,index=0 \
  -drive id=nv,file=/tmp/t_dy_nv.img,if=none,format=raw -device nvme,drive=nv,serial=OTK1
kontrol "NVMe disk 1 (IDE+NVMe) oku+yaz" "disk-yazma-test: disk 0x00000001 OKUMA+YAZMA BASARILI" /tmp/t_dy_nvme.log
kontrol "NVMe birimi atandi"   "birim 0x00000001 -> NVMe" /tmp/t_dy_nvme.log
dy nvmeo 0 -drive id=nv,file=/tmp/t_dy_nv2.img,if=none,format=raw -device nvme,drive=nv,serial=OTK2
kontrol "NVMe-only disk 0 oku+yaz (IDE yok)" "disk-yazma-test: disk 0x00000000 OKUMA+YAZMA BASARILI" /tmp/t_dy_nvmeo.log
yok     "depolama panik yok"   "PANIGI" /tmp/t_dy_nvmeo.log
cp -f fat32test.img /tmp/t_dy_f32.img
dy f32 0 -drive file=/tmp/t_dy_ds3.img,format=raw,if=ide,index=0 \
  -drive file=/tmp/t_dy_f32.img,format=raw,if=ide,index=1
kontrol "FAT32 alt dizin+yazma+okuma" "fat32-yazma-test: alt dizin + yazma + degistirme + okuma BASARILI" /tmp/t_dy_f32.log
if command -v fsck.fat >/dev/null 2>&1; then
  if fsck.fat -n /tmp/t_dy_f32.img >/tmp/t_dy_fsck.log 2>&1; then ok "Linux fsck.fat yazilan FAT32'yi temiz buldu"
  else hata "fsck.fat hata verdi (bkz /tmp/t_dy_fsck.log)"; fi
fi
if command -v sgdisk >/dev/null 2>&1 && command -v mkfs.fat >/dev/null 2>&1; then
  rm -f /tmp/t_dy_gb.img; truncate -s 3T /tmp/t_dy_gb.img
  BAS=$((5*1024*1024*1024*1024/2/512))
  sgdisk -o -n 1:$BAS:+131072 -t 1:0700 /tmp/t_dy_gb.img >/dev/null 2>&1
  mkfs.fat -F 32 --offset $BAS /tmp/t_dy_gb.img 131072 >/dev/null 2>&1
  cp -f diskfiles.img /tmp/t_dy_ds4.img
  dy gpt 0 -drive file=/tmp/t_dy_ds4.img,format=raw,if=ide,index=0 \
    -drive file=/tmp/t_dy_gb.img,format=raw,if=ide,index=1
  kontrol "GPT bolumu 2.5 TB ofsette baglandi" "bolum_lba=0x000000010x40000000" /tmp/t_dy_gpt.log
  kontrol "2.5 TB ofsette FAT32 yazma"  "fat32-yazma-test: alt dizin + yazma + degistirme + okuma BASARILI" /tmp/t_dy_gpt.log
  rm -f /tmp/t_dy_gb.img
fi

mavi "SONUC"
printf "  GECEN: %s   KALAN: %s\n\n" "$GECEN" "$KALAN"
[ "$KALAN" -eq 0 ] && exit 0 || exit 1
