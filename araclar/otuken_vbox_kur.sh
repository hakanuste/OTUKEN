#!/bin/bash
# =====================================================================
# ÖTÜKEN + UMAY — VirtualBox sanal makinesini kurar ve başlatır (macOS)
#
#   bash otuken_vbox_kur.sh [makine_adı] [çekirdek_sayısı] [bellek_MB]
#   Varsayılan: OTUKEN 1 1024
#
# Donanım, ÖTÜKEN'in sınandığı donanımla AYNI seçilir:
#   IDE (PIIX4) disk + CD, PS/2 klavye/fare, Intel PRO/1000 MT (82540EM),
#   BIOS açılışı, VBE ekran (VBoxVGA), x2APIC KAPALI (o yol sınanmadı).
# Seri kapı (COM1) bu dizindeki otuken_seri.log dosyasına yazılır:
#   çekirdeğin bütün açılış günlüğü oradadır.
# =====================================================================
set -e
DIZIN="$(cd "$(dirname "$0")" && pwd)"
AD="${1:-OTUKEN}"
CPU="${2:-1}"
BELLEK="${3:-1024}"

VBM="$(command -v VBoxManage 2>/dev/null || true)"
[ -z "$VBM" ] && VBM="/Applications/VirtualBox.app/Contents/MacOS/VBoxManage"
if [ ! -x "$VBM" ]; then
  echo "VirtualBox bulunamadı. Önce VirtualBox'ı kurun (Intel Mac sürümü)."
  exit 1
fi

for f in otukenos-mini.iso otuken_disk.vdi; do
  [ -f "$DIZIN/$f" ] || { echo "Eksik dosya: $DIZIN/$f"; exit 1; }
done

if "$VBM" showvminfo "$AD" >/dev/null 2>&1; then
  echo "'$AD' adlı makine zaten var."
  echo "  Başlatmak için:  \"$VBM\" startvm \"$AD\""
  echo "  Silmek için:     \"$VBM\" unregistervm \"$AD\" --delete"
  exit 1
fi

# Özgün diske dokunulmaz: makine bir KOPYA ile çalışır (yeni kimlikle).
CALISAN="$DIZIN/${AD}_disk.vdi"
cp "$DIZIN/otuken_disk.vdi" "$CALISAN"
"$VBM" internalcommands sethduuid "$CALISAN" >/dev/null

LOG="$DIZIN/otuken_seri.log"
rm -f "$LOG"

echo "Makine oluşturuluyor: $AD ($CPU çekirdek, $BELLEK MB)"
"$VBM" createvm --name "$AD" --ostype Other_64 --register >/dev/null
"$VBM" modifyvm "$AD" \
  --memory "$BELLEK" --vram 16 --cpus "$CPU" \
  --chipset piix3 --firmware bios --ioapic on --x2apic off \
  --graphicscontroller vboxvga \
  --keyboard ps2 --mouse ps2 \
  --nic1 nat --nictype1 82540EM --cableconnected1 on \
  --uart1 0x3F8 4 --uartmode1 file "$LOG" \
  --rtcuseutc on \
  --boot1 dvd --boot2 disk --boot3 none --boot4 none
# Ses aygıtı gerekmez; seçenek adı sürüme göre değişir (hata verirse geçilir).
"$VBM" modifyvm "$AD" --audio-driver none >/dev/null 2>&1 || \
  "$VBM" modifyvm "$AD" --audio none >/dev/null 2>&1 || true

"$VBM" storagectl "$AD" --name IDE --add ide --controller PIIX4
# Birincil ana: FAT16 disk (UMAY.ELF, model, sözlük). İkincil ana: CD (ISO).
"$VBM" storageattach "$AD" --storagectl IDE --port 0 --device 0 --type hdd --medium "$CALISAN"
"$VBM" storageattach "$AD" --storagectl IDE --port 1 --device 0 --type dvddrive --medium "$DIZIN/otukenos-mini.iso"

echo "Başlatılıyor... (seri günlük: $LOG)"
"$VBM" startvm "$AD" --type gui
