#!/bin/bash
# =====================================================================
# UMAY kurulumu
#
# Derler, kurar, istege bagli olarak sesi de hazirlar.
# Kurulmus bir programin kurulu oldugu sistemde sorunsuz calismasi
# icin ne gerekiyorsa burada denetlenir: derleyici, make, veri yeri,
# PATH. Eksik olan sessizce gecilmez, soylenir.
# =====================================================================
set -u
ONEK="${ONEK:-$HOME/.local}"
SES=0
for a in "$@"; do
  case "$a" in
    --ses) SES=1 ;;
    --onek=*) ONEK="${a#--onek=}" ;;
    --yardim|-y|-h)
      echo "kullanim: ./kur.sh [--ses] [--onek=DIZIN]"
      echo "  --ses        Piper ses arka ucunu da kurar"
      echo "  --onek=DIZIN varsayilan: \$HOME/.local"
      exit 0 ;;
    *) echo "bilinmeyen secenek: $a"; exit 2 ;;
  esac
done

yaz(){ printf '  %s\n' "$1"; }
hata(){ printf '  HATA: %s\n' "$1"; exit 1; }

echo "UMAY kurulumu  (onek: $ONEK)"
echo

# --- 1) gereksinimler ---
command -v make >/dev/null 2>&1 || hata "make yok. Debian/Ubuntu: sudo apt install build-essential"
if   command -v cc  >/dev/null 2>&1; then DERLEYICI=cc
elif command -v gcc >/dev/null 2>&1; then DERLEYICI=gcc
elif command -v clang >/dev/null 2>&1; then DERLEYICI=clang
else hata "C derleyicisi yok. Debian/Ubuntu: sudo apt install build-essential"; fi
yaz "derleyici: $DERLEYICI"

# --- 2) derle ---
yaz "derleniyor..."
make CC="$DERLEYICI" >/dev/null || hata "derleme basarisiz. Ayrinti icin: make"
yaz "derlendi: kov/umay"

# --- 3) sina ---
yaz "sinamalar kosuluyor..."
if make CC="$DERLEYICI" sina >/tmp/umay_sina.txt 2>&1; then
  yaz "$(grep -o '[0-9]* gecti, [0-9]* kaldi' /tmp/umay_sina.txt | tail -1)"
else
  yaz "UYARI: sinamalar gecmedi, ayrinti: /tmp/umay_sina.txt"
fi

# --- 4) kur ---
make CC="$DERLEYICI" kur ONEK="$ONEK" >/dev/null || hata "kurulum basarisiz"
yaz "kuruldu: $ONEK/bin/umay"
yaz "veri    : $ONEK/share/umay/kut"

# --- 5) PATH ---
case ":$PATH:" in
  *":$ONEK/bin:"*) yaz "PATH tamam" ;;
  *) echo
     yaz "UYARI: $ONEK/bin PATH'te degil. Kabuk ayarina ekleyin:"
     yaz "  echo 'export PATH=\"$ONEK/bin:\$PATH\"' >> ~/.bashrc" ;;
esac

# --- 6) ses ---
if [ "$SES" = 1 ]; then
  echo
  ./ses/piper_kur.sh || yaz "UYARI: ses kurulamadi; UMAY yaziyla calisir."
fi

# --- 7) dogrula ---
echo
yaz "dogrulama:"
if "$ONEK/bin/umay" --surum >/dev/null 2>&1; then
  yaz "  surum : $("$ONEK/bin/umay" --surum)"
else
  hata "kurulan program calismiyor"
fi
cik=$(cd /tmp && "$ONEK/bin/umay" "merhaba" </dev/null 2>&1 | grep -c "UMAY  ")
if [ "$cik" -gt 0 ]; then
  yaz "  baska dizinden calisiyor ve yanit veriyor"
else
  yaz "  UYARI: baska dizinden yanit alinamadi (veri yeri?)"
fi

echo
yaz "hazir.  umay            -> konusma kipi"
yaz "        umay \"soru\"     -> tek soru"
yaz "        umay --yardim"
