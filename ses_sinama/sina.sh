#!/bin/bash
# DEDE ses sinamalari — cekirdek kaynagini ana bilgisayarda derleyip
# uretilen sesi OLCER. Ses kalitesi boylece gerileme sinamasina girer;
# "kulakla bakariz" diye bir sey yok.
set -u
KOK="$(cd "$(dirname "$0")" && pwd)"
OT="${1:-$(cd "$KOK/.." && pwd)}"
export OT_KOK="$OT"
export DEDE_KOK="$KOK"
mkdir -p "$KOK/cik"
G=0; K=0
gec(){ echo "  [GECTI] $1"; G=$((G+1)); }
kal(){ echo "  [KALDI] $1"; K=$((K+1)); }

gcc -O2 -DDEDE_OLCUM -DDEDE_KAYNAK="\"$OT/src/dede.c\"" \
    -I "$KOK/kabuk" -I "$OT/include" \
    -o "$KOK/cik/r_sina" "$KOK/kabuk/render.c" -lm 2>/dev/null \
  && gec "ana bilgisayar render'i derlendi" || { kal "render derlenmedi"; exit 1; }

# 1) ANLASILIRLIK: sentezlenen unlulerden F1/F2 geri okunuyor mu
if python3 -I "$KOK/olcum/anlasilirlik.py" "$KOK/cik/r_sina" >/tmp/dede_anla.txt 2>&1; then
  gec "anlasilirlik: unluler amaclanan formantlarda ($(grep -o 'ortalama [0-9.]*%' /tmp/dede_anla.txt))"
else
  kal "anlasilirlik: unluler ayirt edilmiyor"; tail -3 /tmp/dede_anla.txt
fi

# 2) DENGE: sesler arasi duzey dogal mi
if python3 -I "$KOK/olcum/denge.py" "$KOK/cik/r_sina" >/tmp/dede_denge.txt 2>&1; then
  gec "ses dengesi dogal araliklarda"
else
  kal "ses dengesi bozuk"; tail -3 /tmp/dede_denge.txt
fi

# 3) UNSUZ AYRIMI: unsuzler birbirinden ayirt edilebiliyor mu
if python3 -I "$KOK/unsuz.py" "$KOK/cik/r_sina" >/tmp/dede_unsuz.txt 2>&1; then
  gec "unsuzler ayirt ediliyor ($(grep -o "en yakin [0-9.]*" /tmp/dede_unsuz.txt | tail -1))"
else
  kal "unsuzler ayirt edilmiyor"; grep "AYIRT" /tmp/dede_unsuz.txt | head -4
fi

# 4) VURGU: heceler arasinda siddet karsitligi
if python3 -I "$KOK/vurgu.py" "$KOK/cik/r_sina" >/tmp/dede_vurgu.txt 2>&1; then
  gec "hece vurgusu var ($(grep -o '[0-9.]* kat' /tmp/dede_vurgu.txt | head -1))"
else
  kal "heceler esit siddette - vurgu yok"; head -2 /tmp/dede_vurgu.txt
fi

# 5) SESSIZLIK: duraklarda cikis gercekten susuyor mu
if python3 -I "$KOK/sessizlik.py" "$KOK/cik/r_sina" >/tmp/dede_sessiz.txt 2>&1; then
  gec "duraklar susuyor ($(grep -o 'durak duzeyi *[-0-9.]* dB' /tmp/dede_sessiz.txt))"
else
  kal "duraklar susmuyor"; sed -n '1,3p' /tmp/dede_sessiz.txt
fi

# 6) HIZ: hece yaylari olagan araliginda mi (kiplenim tayfi)
if python3 -I "$KOK/olcum/hiz.py" "$KOK/cik/r_sina" >/tmp/dede_hiz.txt 2>&1; then
  gec "hece hizi dogal ($(grep -o 'hece hizi *: *[0-9.]* Hz' /tmp/dede_hiz.txt), $(grep -o 'saniyede [0-9.]* hece' /tmp/dede_hiz.txt))"
else
  kal "hece yaylari olagan araligin disinda"; sed -n '1,3p' /tmp/dede_hiz.txt
fi

# 7) AC'97 SURUCUSU: taklit kartla kosulur (cekirdek ses.c'si)
# NEDEN: ses.c donanima I/O kapilariyla konusur ve hic sinanmiyordu;
# icinde dort hata birden duruyordu (sayfa tasmasi, motorun her
# cagrida bastan baslamasi, 32 girise 16 tampon, ses_ton(0) hatasi).
SES_C="$OT/src/ses.c"
if [ -f "$SES_C" ] && [ -f "$KOK/kabuk/ses_kosum.c" ]; then
  if gcc -O1 -DSES_KAYNAK="\"$SES_C\"" -I "$KOK/kabuk/sahte" -I "$OT/include" \
         -o "$KOK/cik/ses_kosum" "$KOK/kabuk/ses_kosum.c" 2>/dev/null; then
    if "$KOK/cik/ses_kosum" >/tmp/dede_ses.txt 2>&1; then
      gec "AC'97 surucusu: $(grep -o 'SONUC: .*' /tmp/dede_ses.txt)"
    else
      kal "AC'97 surucusu kosumu basarisiz"; grep KALDI /tmp/dede_ses.txt | head -4
    fi
  else
    kal "AC'97 kosumu derlenmedi"
  fi
else
  kal "ses_kosum.c ya da src/ses.c yok"
fi

# 8) TASMA: cikis sinirlayiciya dayaniyor mu
TAS=0
for T in "Ötüken'in yurdunda ocak tütüyor." \
         "Su akar, taş kalır; söz uçar, yazı kalır." \
         "Şşşt! Pşt. Kkk. Fffff. Sssss." \
         "Aaaa eeee iiii oooo uuuu"; do
  O=$("$KOK/cik/r_sina" "$T" "$KOK/cik/sina.wav" 2>/dev/null | grep -o 'tasma=[0-9]*' | cut -d= -f2)
  TAS=$((TAS + ${O:-0}))
done
[ "$TAS" -eq 0 ] && gec "cikis tasmasi yok (4 metin)" || kal "cikis tasiyor: $TAS ornek"

echo "  --- DEDE: GECEN $G  KALAN $K ---"
exit $([ "$K" -eq 0 ] && echo 0 || echo 1)
