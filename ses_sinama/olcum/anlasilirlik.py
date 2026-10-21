#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ANLASILIRLIK — sentezlenen ünlülerden formantlar geri okunabiliyor mu?

NEDEN BU SINAMA VAR: önceki sürüm tayf bandı ölçütlerini tutturuyordu
ama KONUŞMUYORDU. Bant enerjisi yanlış bir hedefti; bir ses "doğru
renkte" olup yine de hiçbir ünlüyü ayırt ettirmeyebilir.

Ünlü kimliği F1 ile F2'nin KONUMUNDADIR. Sentezlenen sesten bu iki
formant geri okunamıyorsa o ünlü ayırt edilemez. Burada ölçülen budur:

  1. Her ünlü tek tek seslendirilir, LPC ile F1/F2 kestirilir ve
     AMAÇLANAN değerle karşılaştırılır.
  2. Ünlü çiftlerinin uzaklığı MEL ölçeğinde ölçülür: sekiz ünlü
     birbirine çökmüş mü, yoksa ayrı duruyor mu? Hz cinsinden uzaklık
     yanıltıcıdır — kulak alçak frekanslarda çok daha ince ayrım yapar,
     1500 ile 1600 Hz arası 300 ile 400 Hz arasından çok daha yakındır.

Geçme ölçütü:
  - F1 ve F2 sapması %15'in altında (işitsel ayırt etme sınırı bu
    büyüklüktedir),
  - en yakın ünlü çiftinin uzaklığı 60 mel'in üstünde (ünlü ayrımı
    için bildirilen fark eşiği 20-30 mel dolayındadır; iki katı pay).
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import olc
import numpy as np


def mel(f):
    """Hz -> mel. Kulagin cozunurlugu alcak frekansta daha incedir."""
    return 2595.0 * np.log10(1.0 + f / 700.0)

KOK = os.environ.get('DEDE_KOK', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# harf, amaçlanan F1, F2 (dede.c'deki SESLI tablosu x ölçek)
UNLULER = [
    ('a', 730, 1250), ('e', 500, 1900), ('ı', 350, 1300), ('i', 290, 2250),
    ('o', 480,  850), ('ö', 440, 1550), ('u', 320,  750), ('ü', 300, 1800),
]
F1_OLCEK, F2_OLCEK = 0.98, 0.97


def sesle(ikili, harf, wav):
    subprocess.run([ikili, harf * 8, wav], capture_output=True)


def tek_unlu(wav):
    """Ünlünün KARARLI bölümünden F1/F2. Geçişler atlanır."""
    x, hz = olc.wav_oku(wav)
    n = len(x)
    x = x[int(n * 0.35):int(n * 0.85)]        # orta bölüm
    F = olc.formantlar(x, hz, derece=12, en_cok=3)
    return F


def sina(ikili, ayrinti=True):
    os.makedirs(KOK + '/cik/un', exist_ok=True)
    olculen = {}
    satir = []
    for harf, af1, af2 in UNLULER:
        hf1, hf2 = af1 * F1_OLCEK, af2 * F2_OLCEK
        wav = '%s/cik/un/%s.wav' % (KOK, harf.encode('ascii', 'replace').decode())
        sesle(ikili, harf, wav)
        F = tek_unlu(wav)
        f1 = F[0] if len(F) > 0 else float('nan')
        f2 = F[1] if len(F) > 1 else float('nan')
        olculen[harf] = (f1, f2)
        s1 = abs(f1 - hf1) / hf1 * 100 if f1 == f1 else 999
        s2 = abs(f2 - hf2) / hf2 * 100 if f2 == f2 else 999
        satir.append((harf, hf1, f1, s1, hf2, f2, s2))

    # en yakın ünlü çifti
    enyakin, cift = 1e9, None
    adlar = [u[0] for u in UNLULER]
    for i in range(len(adlar)):
        for j in range(i + 1, len(adlar)):
            a, b = olculen[adlar[i]], olculen[adlar[j]]
            if a[0] != a[0] or b[0] != b[0]:
                continue
            d = np.hypot(mel(a[0]) - mel(b[0]), mel(a[1]) - mel(b[1]))
            if d < enyakin:
                enyakin, cift = d, (adlar[i], adlar[j])

    if ayrinti:
        print('ünlü  amaç F1  ölçülen  sapma   amaç F2  ölçülen  sapma')
        for harf, hf1, f1, s1, hf2, f2, s2 in satir:
            print('  %s   %7.0f %8.0f %6.1f%%  %7.0f %8.0f %6.1f%%'
                  % (harf, hf1, f1, s1, hf2, f2, s2))
        print('en yakın ünlü çifti: %s-%s, uzaklık %.0f mel'
              % (cift[0], cift[1], enyakin) if cift else 'çift yok')

    sapmalar = [max(r[3], r[6]) for r in satir]
    gecti = (max(sapmalar) < 15.0) and (enyakin > 60)
    return {
        'gecti': gecti,
        'en_buyuk_sapma': max(sapmalar),
        'ortalama_sapma': float(np.mean([min(r[3], 100) for r in satir] +
                                        [min(r[6], 100) for r in satir])),
        'en_yakin': enyakin,
        'satir': satir,
    }


if __name__ == '__main__':
    ikili = sys.argv[1] if len(sys.argv) > 1 else KOK + '/cik/r'
    d = sina(ikili)
    print()
    print('en büyük sapma %.1f%%   ortalama %.1f%%   en yakın çift %.0f mel'
          % (d['en_buyuk_sapma'], d['ortalama_sapma'], d['en_yakin']))
    print('SONUÇ: ' + ('GEÇTİ' if d['gecti'] else 'KALDI'))
    sys.exit(0 if d['gecti'] else 1)
