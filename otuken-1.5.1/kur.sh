#!/usr/bin/env bash
# =====================================================================
# ÖTÜKEN + UMAY — kurulum ve derleme betiği
#
# Bu betik sisteminizde gerekli araçları denetler, eksikse kurar ve
# üç bileşeni de kaynaktan derler: çekirdek, UMAY ve US1 motoru.
#
# Kullanım:
#   bash kur.sh            — araçları denetle, hepsini derle
#   bash kur.sh --arac     — yalnız araçları kur
#   bash kur.sh --derle    — yalnız derle (araçlar zaten varsayılır)
#   bash kur.sh --sina     — derle ve tam sınama takımını koştur
#   bash kur.sh --calistir — derle ve QEMU'da başlat
# =====================================================================
set -u

KOK="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Tek agac: cekirdek kok dizinin kendisidir (paketteki cekirdek/ alt
# dizini ÖTÜKEN 1.2 ile birlestirildi - SMP ve kilitler korunarak).
CEK="$KOK"
UMY="$KOK/umay"
US1="$KOK/us1"

kirmizi() { printf '\033[31m%s\033[0m\n' "$*"; }
yesil()   { printf '\033[32m%s\033[0m\n' "$*"; }
mavi()    { printf '\033[1;36m%s\033[0m\n' "$*"; }
sari()    { printf '\033[33m%s\033[0m\n' "$*"; }

# --- Gerekli araçlar ---
# nasm       : önyükleyici (boot.asm)
# gcc, ld    : çekirdek ve kullanıcı programları
# grub-mkrescue + xorriso : önyüklenebilir ISO
# mtools     : disk imajına dosya kopyalama (mcopy/mdir)
# qemu       : çalıştırma ve sınama
# python3 + numpy : US1 model eğitimi (yalnız model üretmek için)
ARAC_KOMUT=(nasm gcc ld grub-mkrescue xorriso mcopy qemu-system-x86_64 python3)
ARAC_PAKET=(nasm gcc binutils grub-pc-bin xorriso mtools qemu-system-x86 python3)

arac_denetle() {
    mavi "1) Araç denetimi"
    local eksik=()
    for i in "${!ARAC_KOMUT[@]}"; do
        if command -v "${ARAC_KOMUT[$i]}" >/dev/null 2>&1; then
            yesil "  var    ${ARAC_KOMUT[$i]}"
        else
            kirmizi "  EKSIK  ${ARAC_KOMUT[$i]}  (paket: ${ARAC_PAKET[$i]})"
            eksik+=("${ARAC_PAKET[$i]}")
        fi
    done
    if [ ${#eksik[@]} -eq 0 ]; then
        yesil "  tüm araçlar hazır"
        return 0
    fi
    sari "  eksik paketler: ${eksik[*]}"
    if command -v apt-get >/dev/null 2>&1; then
        sari "  kuruluyor (yönetici yetkisi gerekebilir)..."
        sudo apt-get update -qq && \
        sudo apt-get install -y --no-install-recommends "${eksik[@]}"
    elif command -v dnf >/dev/null 2>&1; then
        sari "  Fedora/RHEL: sudo dnf install nasm gcc binutils grub2-tools \\"
        sari "               xorriso mtools qemu-system-x86 python3"
        return 1
    elif command -v pacman >/dev/null 2>&1; then
        sari "  Arch: sudo pacman -S nasm gcc binutils grub libisoburn \\"
        sari "        mtools qemu-system-x86 python"
        return 1
    else
        kirmizi "  paket yöneticisi tanınmadı; araçları elle kurun"
        return 1
    fi
}

sinama_imaji() {
    # FAT32 sınama imajı pakette YOKTUR (64 MB, çoğu sıfır).
    # Sınama takımı bunu ister; yoksa burada üretilir.
    if [ -f "$CEK/fat32test.img" ]; then return 0; fi
    sari "  fat32test.img üretiliyor (sınama takımı için, 64 MB)"
    dd if=/dev/zero of="$CEK/fat32test.img" bs=1M count=64 status=none
    if command -v mkfs.vfat >/dev/null 2>&1; then
        mkfs.vfat -F 32 "$CEK/fat32test.img" >/dev/null 2>&1
    else
        sari "  mkfs.vfat yok — FAT32 sınamaları atlanacak"
    fi
}

derle() {
    mavi "2) Derleme"

    printf '  çekirdek... '
    if ( cd "$CEK" && make >/dev/null 2>&1 ); then
        yesil "tamam ($(stat -c%s "$CEK/otukenos-mini.elf" 2>/dev/null) bayt)"
    else
        kirmizi "BAŞARISIZ"
        ( cd "$CEK" && make 2>&1 | grep -E "error" | head -5 )
        return 1
    fi

    printf '  kabuk (Ring3)... '
    if ( cd "$CEK" && make userland >/dev/null 2>&1 ); then yesil "tamam"
    else sari "atlandı"; fi

    printf '  UMAY... '
    mkdir -p "$UMY/kov"
    if ( cd "$UMY" && make >/dev/null 2>&1 ); then
        yesil "tamam ($(stat -c%s "$UMY/kov/umay" 2>/dev/null) bayt)"
    else
        kirmizi "BAŞARISIZ"
        ( cd "$UMY" && make 2>&1 | grep -E " error" | head -5 )
        return 1
    fi

    printf '  UMAY sınama ikilisi... '
    if ( cd "$UMY" && make sina >/dev/null 2>&1 ); then yesil "tamam"
    else sari "atlandı"; fi

    printf '  US1 motoru... '
    if gcc -std=c11 -O2 -o "$US1/us1_sina" \
           "$US1/sinama/us1_sina.c" "$US1/c/us1.c" 2>/dev/null; then
        yesil "tamam"
    else
        sari "atlandı"
    fi

    # --- Disk imajını güncelle ---
    # UMAY.ELF, model ve sözlük diskten okunur; çekirdeğe GÖMÜLÜ DEĞİL.
    # Modeli değiştirmek için çekirdeği yeniden derlemek gerekmez.
    printf '  disk imajı... '
    if [ -f "$CEK/diskfiles.img" ] && command -v mcopy >/dev/null 2>&1; then
        mcopy -i "$CEK/diskfiles.img" -o "$UMY/kov/umay"      ::UMAY.ELF   2>/dev/null
        mcopy -i "$CEK/diskfiles.img" -o "$US1/umay.us1"      ::UMAY.US1   2>/dev/null
        mcopy -i "$CEK/diskfiles.img" -o "$US1/sozluk.txt"    ::SOZLUK.TXT 2>/dev/null
        yesil "güncellendi"
    else
        sari "atlandı (mcopy ya da imaj yok)"
    fi

    printf '  önyüklenebilir ISO... '
    rm -f "$CEK/otukenos-mini.iso"
    if ( cd "$CEK" && make iso >/dev/null 2>&1 ) && [ -f "$CEK/otukenos-mini.iso" ]; then
        yesil "tamam ($(stat -c%s "$CEK/otukenos-mini.iso") bayt)"
    else
        kirmizi "BAŞARISIZ — grub-mkrescue ve xorriso kurulu mu?"
        return 1
    fi
    return 0
}

calistir() {
    mavi "3) Çalıştırma"
    sari "  QEMU açılıyor. Kapatmak için pencereyi kapatın ya da Ctrl+C."
    qemu-system-x86_64 \
        -device AC97 \
        -cdrom "$CEK/otukenos-mini.iso" \
        -drive file="$CEK/diskfiles.img",format=raw,if=ide,index=0 \
        -m 256 -smp 4 -boot d
}

sina() {
    mavi "3) Sınama takımı"
    sinama_imaji
    ( cd "$CEK" && bash testler.sh )
    printf '\n'
    mavi "3b) Donanım + SMP takımı"
    ( cd "$CEK" && bash donanim_testleri.sh | tail -3 )
    printf '\n'
    mavi "3c) Kabuk oturumu (1 ve 4 çekirdek)"
    ( cd "$CEK" && python3 kabuk_testi.py | tail -5 )
    ( cd "$CEK" && KABUK_SMP=4 python3 kabuk_testi.py | tail -5 )
    printf '\n'
    mavi "4) UMAY kendi sınaması"
    ( cd "$UMY" && ./kov/umay_sina 2>&1 | tail -3 )
    printf '\n'
    mavi "5) US1 motor sınaması"
    if [ -x "$US1/us1_sina" ]; then
        ( cd "$US1" && ./us1_sina 2>&1 | tail -4 )
    else
        sari "  us1_sina derlenmemiş"
    fi
}

# --- Ana akış ---
printf '\n'
mavi "=== ÖTÜKEN + UMAY kurulumu ==="
printf 'kök dizin: %s\n\n' "$KOK"

case "${1:-}" in
    --arac)     arac_denetle ;;
    --derle)    derle ;;
    --sina)     derle && sina ;;
    --calistir) derle && calistir ;;
    *)
        arac_denetle || exit 1
        printf '\n'
        derle || exit 1
        printf '\n'
        yesil "=== Kurulum tamam ==="
        printf '\n'
        printf 'Çalıştırmak için:  bash kur.sh --calistir\n'
        printf 'Sınamak için:      bash kur.sh --sina\n'
        printf '\n'
        ;;
esac
