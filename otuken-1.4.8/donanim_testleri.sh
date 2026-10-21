#!/bin/bash
# ÖTÜKEN donanim uyumluluk testleri (islemci / bellek / disksiz)
set -u
cd "$(dirname "$0")"
GECEN=0; KALAN=0
mavi(){ printf "\n\033[1m== %s ==\033[0m\n" "$1"; }
ok(){   printf "  [GECTI] %s\n" "$1"; GECEN=$((GECEN+1)); }
hata(){ printf "  [KALDI] %s\n" "$1"; KALAN=$((KALAN+1)); }
yok(){ if grep -aq "$2" "$3" 2>/dev/null; then hata "$1 ($2 BULUNDU)"; else ok "$1"; fi; }
kontrol(){ if grep -aq "$2" "$3" 2>/dev/null; then ok "$1"; else hata "$1 (aranan: $2)"; fi; }
temizle(){ pkill -9 -f qemu-system-x86_64 >/dev/null 2>&1; }
trap temizle EXIT

mavi "12) Islemci uyumlulugu (1GB sayfa VAR/YOK yollari)"
for CPUM in "qemu64" "max" "EPYC"; do
  LOGC=/tmp/t_cpu_$CPUM.log
  timeout 13 qemu-system-x86_64 -cpu "$CPUM" -cdrom otukenos-mini.iso \
    -m 512 -display none -serial stdio -no-reboot -boot d > "$LOGC" 2>&1
  if grep -aq "ABCDE" "$LOGC" && grep -aq "OTUKEN 1.0" "$LOGC"; then
    ok "CPU $CPUM onyukleme"
  else
    hata "CPU $CPUM onyukleme"
  fi
  yok "CPU $CPUM panik yok" "PANIGI" "$LOGC"
done

mavi "13) Bellek olceklenmesi"
for MEM in 128 3G; do
  LOGM=/tmp/t_mem_$MEM.log
  timeout 18 qemu-system-x86_64 -cpu max -cdrom otukenos-mini.iso \
    -m "$MEM" -display none -serial stdio -no-reboot -boot d > "$LOGM" 2>&1
  if grep -aq "pmm: toplam bellek" "$LOGM" && ! grep -aq "PANIGI" "$LOGM"; then
    ok "bellek $MEM"
  else
    hata "bellek $MEM"
  fi
done


mavi "14) Disksiz makine (FAT birimi YOK)"
LOGD=/tmp/t_nodisk.log
timeout 14 qemu-system-x86_64 -cpu qemu64 -cdrom otukenos-mini.iso \
  -m 512 -display none -serial stdio -no-reboot -boot d > "$LOGD" 2>&1
yok     "disksiz: panik yok"     "PANIGI"          "$LOGD"
if grep -aq "OTUKEN 1.0 HAZIR" "$LOGD"; then ok "disksiz: sistem acildi"; else hata "disksiz: acilmadi"; fi

mavi "15) Buyuk disk (LBA48 / 8 TB)"
./buyukdisk_hazirla.sh /tmp/hw_buyuk.img
cp -f diskfiles.img /tmp/hw_ana.img 2>/dev/null
LOGB=/tmp/t_buyukdisk.log
timeout 35 qemu-system-x86_64 -cpu max -cdrom otukenos-mini.iso \
  -drive file=/tmp/hw_ana.img,format=raw,if=ide,index=0 \
  -drive file=/tmp/hw_buyuk.img,format=raw,if=ide,index=1 \
  -m 512 -display none -serial stdio -no-reboot -boot d > "$LOGB" 2>&1
if grep -aq "LBA48=VAR" "$LOGB"; then ok "8 TB surucu LBA48 ile tanindi"; else hata "LBA48 taninmadi"; fi
if grep -aq "IMZA DOGRU" "$LOGB"; then ok "6 TB konumundan dogru okuma (LBA28/uint32 tavani asildi)"; else hata "6 TB okuma"; fi
yok "buyuk disk: panik yok"        "PANIGI"          "$LOGB"
yok "buyuk disk: heap bozulmasi yok" "heap bozulmasi" "$LOGB"

mavi "16) 1GB sayfa + ince haritalama birlikte (regresyon)"
LOGS=/tmp/t_split.log
timeout 20 qemu-system-x86_64 -cpu max -cdrom otukenos-mini.iso \
  -drive file=/tmp/hw_ana.img,format=raw,if=ide,index=0 \
  -m 512 -display none -serial stdio -no-reboot -boot d > "$LOGS" 2>&1
yok "1GB sayfa: heap saglam"   "heap bozulmasi"     "$LOGS"
yok "1GB sayfa: haritalama OK" "haritalama basarisiz" "$LOGS"

mavi "17) UEFI onyukleme (multiboot2 + OVMF)"
if [ -f /usr/share/OVMF/OVMF_CODE_4M.fd ]; then
  ./uefi_iso_yap.sh >/dev/null 2>&1
  cp -f /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/t_ovmf_vars.fd 2>/dev/null
  LOGE=/tmp/t_uefi.log
  timeout 40 qemu-system-x86_64 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,unit=1,file=/tmp/t_ovmf_vars.fd \
    -cdrom otukenos-uefi.iso -m 512 -display none -serial stdio \
    -no-reboot -boot d > "$LOGE" 2>&1
  if grep -aq "MULTIBOOT2" "$LOGE"; then ok "UEFI: multiboot2 protokolu"; else hata "UEFI: multiboot2 kullanilmadi"; fi
  if grep -aq "OTUKEN 1.0 HAZIR" "$LOGE"; then ok "UEFI: sistem acildi"; else hata "UEFI: acilmadi"; fi
  if grep -aq "framebuffer adres" "$LOGE"; then ok "UEFI: GOP framebuffer alindi"; else hata "UEFI: framebuffer yok"; fi
  if grep -aq "pmm: toplam bellek(bayt)=0x0000000000000000" "$LOGE"; then
    hata "UEFI: bellek haritasi BOS"
  else ok "UEFI: bellek haritasi cevrildi"; fi
  yok "UEFI: panik yok"        "PANIGI"     "$LOGE"
  yok "UEFI: sayfa hatasi yok" "PAGE FAULT" "$LOGE"
else
  hata "UEFI: OVMF firmware bulunamadi (test atlandi)"
fi

mavi "18) CR0.WP (cekirdek yazma korumasi)"
LOGW=/tmp/t_wp.log
timeout 18 qemu-system-x86_64 -cpu max -cdrom otukenos-mini.iso \
  -m 512 -display none -serial stdio -no-reboot -boot d > "$LOGW" 2>&1
if grep -aq "CR0.WP etkin" "$LOGW"; then ok "CR0.WP etkinlestirildi"; else hata "CR0.WP ayarlanmadi"; fi
yok "WP acikken sayfa hatasi yok" "PAGE FAULT" "$LOGW"

mavi "19) GPT bolum tablosu (4 TB disk)"
if command -v sgdisk >/dev/null 2>&1; then
  ./gpt_disk_hazirla.sh /tmp/hw_gpt.img
  cp -f diskfiles.img /tmp/hw_ana2.img 2>/dev/null
  LOGG=/tmp/t_gpt.log
  timeout 30 qemu-system-x86_64 -cpu max -cdrom otukenos-mini.iso \
    -drive file=/tmp/hw_ana2.img,format=raw,if=ide,index=0 \
    -drive file=/tmp/hw_gpt.img,format=raw,if=ide,index=1 \
    -m 512 -display none -serial stdio -no-reboot -boot d > "$LOGG" 2>&1
  if grep -aq "EFI PART\|tablo bulundu" "$LOGG"; then ok "GPT tablosu okundu"; else hata "GPT tablosu okunamadi"; fi
  if grep -aq "ad='VERI'" "$LOGG"; then ok "bolum adlari cozuldu"; else hata "bolum adlari"; fi
  if grep -aq "ad='ARSIV'" "$LOGG"; then ok "2 TB otesindeki bolum goruldu"; else hata "2 TB otesi bolum"; fi
  yok "GPT: panik yok" "PANIGI" "$LOGG"
else
  hata "GPT: sgdisk yok (test atlandi)"
fi

mavi "20) SMP: APIC + AP baslatma (INIT-SIPI-SIPI)"
LOGS=/tmp/t_smp4.log
timeout 30 qemu-system-x86_64 -cdrom otukenos-mini.iso -m 128 -smp 4 \
  -display none -serial stdio -no-reboot -boot d > "$LOGS" 2>&1
kontrol "SMP: MADT okundu (4 cekirdek)" "etkin cekirdek=4" "$LOGS"
kontrol "SMP: cekirdek 3 uyandi"        "cekirdek 3 uyandi" "$LOGS"
kontrol "SMP: 4/4 calisiyor"            "SMP: 4/4 cekirdek" "$LOGS"
kontrol "SMP: AP GS+TSS kuruldu"       "cekirdek 3 uyandi (APIC=0x3, GS+TSS kuruldu)" "$LOGS"
kontrol "SMP: TLB vurusu (bayat giris temizlendi)" "TLB vurusu sinamasi GECTI (AP=3" "$LOGS"
kontrol "SMP: sistem acildi"            "OTUKEN 1.0 HAZIR"  "$LOGS"
yok     "SMP: uyari/hata yok"           "\[UYARI\]\|\[HATA\]\|PANIGI\|PAGE FAULT" "$LOGS"
LOGS2=/tmp/t_smp_seyrek.log
timeout 30 qemu-system-x86_64 -cdrom otukenos-mini.iso -m 128 -smp 6,sockets=2,cores=3 -cpu qemu64 \
  -display none -serial stdio -no-reboot -boot d > "$LOGS2" 2>&1
kontrol "SMP: seyrek APIC kimligi (0x6) + 2MB yolu" "uyandi (APIC=0x6, GS+TSS kuruldu)" "$LOGS2"
kontrol "SMP: 6/6 calisiyor"            "SMP: 6/6 cekirdek" "$LOGS2"
kontrol "SMP: TLB vurusu, seyrek APIC + 2MB yolu" "TLB vurusu sinamasi GECTI (AP=5" "$LOGS2"
if [ -f /usr/share/OVMF/OVMF_CODE_4M.fd ]; then
  cp -f /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/t_ovmf_smp.fd
  LOGS3=/tmp/t_smp_uefi.log
  timeout 40 qemu-system-x86_64 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,unit=1,file=/tmp/t_ovmf_smp.fd \
    -cdrom otukenos-uefi.iso -m 512 -smp 4 -display none -serial stdio \
    -no-reboot -boot d > "$LOGS3" 2>&1
  kontrol "SMP/UEFI: RSDP multiboot2 etiketinden" "MADT (multiboot2)" "$LOGS3"
  kontrol "SMP/UEFI: 4/4 calisiyor"     "SMP: 4/4 cekirdek" "$LOGS3"
  kontrol "SMP/UEFI: TLB vurusu"        "TLB vurusu sinamasi GECTI" "$LOGS3"
else
  hata "SMP/UEFI: OVMF yok (test atlandi)"
fi
LOGS4=/tmp/t_smp_acpisiz.log
timeout 30 qemu-system-x86_64 -cdrom otukenos-mini.iso -m 128 -smp 2 -machine pc,acpi=off \
  -display none -serial stdio -no-reboot -boot d > "$LOGS4" 2>&1
kontrol "SMP: ACPI yoksa tek cekirdege guvenli donus" "MADT bulunamadi - tek cekirdek" "$LOGS4"
kontrol "SMP: ACPI'siz sistem acildi"  "OTUKEN 1.0 HAZIR"  "$LOGS4"

mavi "SONUC"
printf "  GECEN: %s   KALAN: %s\n\n" "$GECEN" "$KALAN"
[ "$KALAN" -eq 0 ] && exit 0 || exit 1
