#!/bin/bash
# UEFI (multiboot2) ISO uretir. BIOS ISO'su icin: make iso
set -e
cd "$(dirname "$0")"
mkdir -p iso_uefi/boot/grub
cp otukenos-mini.elf iso_uefi/boot/
cat > iso_uefi/boot/grub/grub.cfg <<'CFG'
set timeout=0
set default=0
menuentry "OTUKEN 1.0 (UEFI)" {
    insmod all_video
    multiboot2 /boot/otukenos-mini.elf
    boot
}
CFG
grub-mkrescue -o otukenos-uefi.iso iso_uefi >/dev/null 2>&1
echo "otukenos-uefi.iso hazir"
