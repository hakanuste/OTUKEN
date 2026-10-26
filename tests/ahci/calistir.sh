#!/bin/bash
# 1.4.9: KOK DISK SATA (AHCI) UZERINDE. q35 makinesi: eski IDE yok, disk ICH9
# AHCI port 0'da, CD port 2'de (SATAPI). Cekirdek ATA yoksa kok diski
# AHCI'den baglamali; OKUMA (FAT16, UMAY.ELF) ve YAZMA (WRITE DMA EXT +
# FLUSH: YAZDIM.TXT, UMAY gunlugu) gercek DMA ile.
#   bash tests/ahci/calistir.sh <iso> <cikti_dizini>
set -u
ISO="$1"; D="$2"; mkdir -p "$D"
KOK="$(cd "$(dirname "$0")/../.." && pwd)"
cp "$KOK/diskfiles.img" "$D/s.img"
mdel -i "$D/s.img" ::YAZDIM.TXT 2>/dev/null
timeout 150 qemu-system-x86_64 -machine q35 -cpu max -cdrom "$ISO" \
  -drive file="$D/s.img",format=raw,if=none,id=d0 -device ide-hd,drive=d0,bus=ide.0 \
  -m 1024 -smp 2 -display none -serial file:"$D/seri.log" -no-reboot -boot d -nic none &
QP=$!
for i in $(seq 1 600); do
  grep -aq "yazildi '/kut/umay/gunluk.txt'" "$D/seri.log" 2>/dev/null && break
  kill -0 $QP 2>/dev/null || break; sleep 0.25
done
sleep 2; kill $QP 2>/dev/null; wait $QP 2>/dev/null
d(){ if eval "$2"; then echo "  [GECTI] $1"; else echo "  [KALDI] $1"; fi; }
d "ATA (IDE) yok, kok disk AHCI portu 0'dan baglandi" "grep -aq 'ATA portunda yanit yok' '$D/seri.log' && grep -aq 'kok disk AHCI portu 0x00000000' '$D/seri.log'"
d "CD (SATAPI) disk sanilmadi" "grep -aq 'imza=0xEB140101 (SATAPI/CD)' '$D/seri.log' && ! grep -aq 'kok disk AHCI portu 0x00000002' '$D/seri.log'"
d "FAT16 SATA diskten baglandi, UMAY.ELF okunup calisti" "grep -aq \"vfs-fat16: FAT16 '/' noktasina baglandi\" '$D/seri.log' && grep -aq \"UMAY: model 'umay-userland-v1' yuklendi\" '$D/seri.log'"
mtype -i "$D/s.img" ::YAZDIM.TXT > "$D/yazdim.txt" 2>&1
d "AHCI YAZMA: YAZDIM.TXT diske yazildi ve icerigi dogru" "grep -q 'OTUKEN YAZMA SINAMASI' '$D/yazdim.txt'"
mtype -i "$D/s.img" ::/KUT/UMAY/GUNLUK.TXT > "$D/gunluk.txt" 2>&1
d "AHCI YAZMA: alt dizine (/KUT/UMAY/GUNLUK.TXT) yazildi" "[ -s '$D/gunluk.txt' ] && ! grep -q 'not found' '$D/gunluk.txt'"
fsck.fat -n "$D/s.img" > "$D/fsck.txt" 2>&1; FS=$?
d "SATA diskte FAT tutarli (fsck.fat cikis=0)" "[ $FS -eq 0 ]"
d "panik yok" "! grep -aq 'PANIGI\|PAGE FAULT' '$D/seri.log'"
