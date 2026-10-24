#!/bin/bash
# UEFI (multiboot2) ISO uretir. BIOS ISO'su icin: make iso
set -e
cd "$(dirname "$0")"
mkdir -p iso_uefi/boot/grub
cp otukenos-mini.elf iso_uefi/boot/
# 1.4.9: ERGENEKON dizge imaji. Imza anahtari yoksa (temiz kopya) imajsiz.
MODUL=""
if [ ! -f ergenekon.krv ] && { [ -f anahtarlar/kervan_uretim.pem ] || [ -f tests/kervan/sinama_anahtari.pem ]; }; then
    make ergenekon.krv >/dev/null
fi
if [ -f ergenekon.krv ]; then
    cp ergenekon.krv iso_uefi/boot/
    MODUL="    module2 /boot/ergenekon.krv ergenekon"
else
    echo "UYARI: imza anahtari yok: UEFI ISO ERGENEKON dizge imajsiz (fabrika sifirlamasi devre disi)"
    rm -f iso_uefi/boot/ergenekon.krv
fi
cat > iso_uefi/boot/grub/grub.cfg <<CFG
set timeout=0
set default=0
menuentry "OTUKEN 1.0 (UEFI)" {
    insmod all_video
    multiboot2 /boot/otukenos-mini.elf
$MODUL
    boot
}
CFG
grub-mkrescue -o otukenos-uefi.iso iso_uefi >/dev/null 2>&1
echo "otukenos-uefi.iso hazir"
