#!/bin/bash
# =====================================================================
# UMAY — Piper ses arka ucu kurulumu (ana bilgisayar)
#
# Piper: sinir agi tabanli (VITS) konusma sentezleyicisi. CPU'da
# gercek zamanli kosar, cevrim disidir, Turkce sesleri vardir.
#
# Bu betik ucunu de hazirlar: piper, Turkce ses modeli, ses calici.
# Sonra "make yerel" ile derlenen UMAY butun yanitlarini sesle soyler.
# =====================================================================
set -u
SES_DIZIN="${UMAY_SES_DIZIN:-$HOME/.local/share/umay/ses}"
SES_ADI="${UMAY_SES_ADI:-tr_TR-fahrettin-medium}"
KAYNAK="https://huggingface.co/rhasspy/piper-voices/resolve/main/tr/tr_TR"

yaz(){ printf '  %s\n' "$1"; }

echo "UMAY — Piper kurulumu"
echo

# 1) piper
if command -v piper >/dev/null 2>&1; then
  yaz "piper zaten kurulu: $(command -v piper)"
else
  yaz "piper kuruluyor (pip)..."
  pip install --user piper-tts || { yaz "KURULAMADI: pip install piper-tts"; exit 1; }
  command -v piper >/dev/null 2>&1 || yaz "UYARI: piper PATH'te gorunmuyor; ~/.local/bin'i PATH'e ekleyin."
fi

# 2) ses modeli
mkdir -p "$SES_DIZIN"
if [ -f "$SES_DIZIN/$SES_ADI.onnx" ]; then
  yaz "ses modeli zaten var: $SES_DIZIN/$SES_ADI.onnx"
else
  # tr_TR-fahrettin-medium -> fahrettin/medium
  KISI=$(echo "$SES_ADI" | sed 's/^tr_TR-//; s/-[a-z]*$//')
  KALITE=$(echo "$SES_ADI" | sed 's/.*-//')
  yaz "ses modeli iniyor: $SES_ADI"
  for uzanti in onnx onnx.json; do
    curl -fL --progress-bar -o "$SES_DIZIN/$SES_ADI.$uzanti" \
         "$KAYNAK/$KISI/$KALITE/$SES_ADI.$uzanti" \
      || { yaz "INDIRILEMEDI: $SES_ADI.$uzanti"; exit 1; }
  done
fi

# 3) calici
if command -v paplay >/dev/null 2>&1 || command -v aplay >/dev/null 2>&1 \
   || command -v afplay >/dev/null 2>&1 || command -v ffplay >/dev/null 2>&1; then
  yaz "ses calici bulundu"
else
  yaz "ses calici YOK. Debian/Ubuntu: sudo apt install alsa-utils"
  exit 1
fi

echo
yaz "hazir. Deneme:"
yaz "  echo 'Ötüken uyandı.' | piper --model $SES_DIZIN/$SES_ADI.onnx --output_file /tmp/d.wav && aplay /tmp/d.wav"
echo
yaz "UMAY artik sesle yanitlar:  make yerel && ./kov/umay-yerel"
yaz "Baska ses istersen:  UMAY_PIPER_SES=/yol/ses.onnx ./kov/umay-yerel"
