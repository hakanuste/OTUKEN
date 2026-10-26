#!/bin/bash
# 1.4.9: KOK DISK NVMe UZERINDE. IDE/SATA disk yok; disk QEMU NVMe
# denetleyicisinde (ad alani 1, 512 bayt blok). Cekirdek ATA ve AHCI disk
# bulamazsa kok diski NVMe'den baglamali: OKUMA (FAT16, UMAY.ELF) ve YAZMA
# (WRITE + FLUSH: YAZDIM.TXT, UMAY gunlugu) gercek G/C kuyrugu ile.
#   bash tests/nvme/calistir.sh <iso> <cikti_dizini>
set -u
ISO="$1"; D="$2"; mkdir -p "$D"
KOK="$(cd "$(dirname "$0")/../.." && pwd)"
cp "$KOK/diskfiles.img" "$D/n.img"
mdel -i "$D/n.img" ::YAZDIM.TXT 2>/dev/null
timeout 150 qemu-system-x86_64 -cpu max -cdrom "$ISO" \
  -drive file="$D/n.img",format=raw,if=none,id=n0 -device nvme,drive=n0,serial=OTUKEN01 \
  -m 1024 -smp 2 -display none -serial file:"$D/seri.log" -no-reboot -boot d -nic none &
QP=$!
for i in $(seq 1 600); do
  grep -aq "yazildi '/kut/umay/gunluk.txt'" "$D/seri.log" 2>/dev/null && break
  kill -0 $QP 2>/dev/null || break; sleep 0.25
done
sleep 2; kill $QP 2>/dev/null; wait $QP 2>/dev/null
d(){ if eval "$2"; then echo "  [GECTI] $1"; else echo "  [KALDI] $1"; fi; }
d "NVMe denetleyici kuruldu (yonetim + G/C kuyruk cifti)" "grep -aq 'nvme: veri yolu hazir' '$D/seri.log'"
d "IDENTIFY: ad alani 1, 512 bayt blok, dogru sektor sayisi" "grep -aq 'nvme: ad alani 1: blok=0x00000200 sayi=0x000000000x00008000' '$D/seri.log'"
d "ATA ve SATA disk yok, kok disk NVMe'den baglandi" "grep -aq 'kok disk NVMe ad alani 1' '$D/seri.log'"
d "FAT16 NVMe'den baglandi, UMAY.ELF okunup calisti" "grep -aq \"vfs-fat16: FAT16 '/' noktasina baglandi\" '$D/seri.log' && grep -aq \"UMAY: model 'umay-userland-v1' yuklendi\" '$D/seri.log'"
mtype -i "$D/n.img" ::YAZDIM.TXT > "$D/yazdim.txt" 2>&1
d "NVMe YAZMA: YAZDIM.TXT diske yazildi ve icerigi dogru" "grep -q 'OTUKEN YAZMA SINAMASI' '$D/yazdim.txt'"
mtype -i "$D/n.img" ::/KUT/UMAY/GUNLUK.TXT > "$D/gunluk.txt" 2>&1
d "NVMe YAZMA: alt dizine (/KUT/UMAY/GUNLUK.TXT) yazildi" "[ -s '$D/gunluk.txt' ] && ! grep -q 'not found' '$D/gunluk.txt'"
fsck.fat -n "$D/n.img" > "$D/fsck.txt" 2>&1; FS=$?
d "NVMe diskte FAT tutarli (fsck.fat cikis=0)" "[ $FS -eq 0 ]"
d "komut hatasi / zaman asimi yok" "! grep -aq 'nvme: komut hatasi\|nvme: komut zaman asimi' '$D/seri.log'"
d "panik yok" "! grep -aq 'PANIGI\|PAGE FAULT' '$D/seri.log'"
