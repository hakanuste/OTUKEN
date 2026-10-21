#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DENGE — sesler arası düzey dengesi doğal mı?

NEDEN: tayf bandı hedeflerini tutturmanın UCUZ bir yolu var — "s"
sesini bağırtmak. 3-5 kHz bandı anında dolar, ölçüt yeşile döner ve
ses bozulur: cümlenin yarısı tıslamaya gömülür. Tarama bunu kendi
basina bulur ve secer; nitekim bir kez secti ("s", unlulerin 9 dB
USTUNE cikmisti).

Bu yüzden ses düzeyleri de ÖLÇÜLEN bir koşul. Doğal konuşmada
bildirilen oranlar (ünlüye göre, dB):

  sürtünmeli (s ş z j)   -20 ... -10
  sürtünmeli (f h)       -28 ... -12
  patlamalı  (p t k ç)   -30 ... -10
  genizsi    (m n)       -12 ...  -2
  akıcı      (l r y v)    -8 ...  +1

Aralıklar geniş tutuldu: amaç ince ayar değil, sesin bariz biçimde
dengesizleşmesini engellemek.
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import olc

KOK = os.environ.get('DEDE_KOK', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

ARALIK = {
    's': (-20, -10), 'ş': (-20, -10), 'z': (-20, -10), 'j': (-20, -10),
    'f': (-28, -12), 'h': (-28, -12),
    'p': (-30, -10), 't': (-30, -10), 'k': (-30, -10), 'ç': (-30, -10),
    'm': (-12, -2),  'n': (-12, -2),
    'l': (-8, 1),    'r': (-8, 1),    'y': (-8, 1),    'v': (-8, 1),
}


def duzey(ikili, harf):
    wav = '%s/cik/dn.wav' % KOK
    subprocess.run([ikili, harf * 6, wav], capture_output=True)
    x, hz = olc.wav_oku(wav)
    return 20 * np.log10(x.std() + 1e-9)


def sina(ikili, ayrinti=True):
    taban = np.mean([duzey(ikili, u) for u in 'aeiou'])
    satir, kotu = [], []
    for harf, (alt, ust) in ARALIK.items():
        d = duzey(ikili, harf) - taban
        ok = alt <= d <= ust
        satir.append((harf, d, alt, ust, ok))
        if not ok:
            kotu.append((harf, d, alt, ust))
    if ayrinti:
        print('ses  ünlüye göre   beklenen aralık')
        for harf, d, alt, ust, ok in satir:
            print('  %s   %+7.1f dB    %+4d .. %+4d  %s'
                  % (harf, d, alt, ust, '' if ok else '  <-- DIŞARIDA'))
    # aralık dışılığın toplam büyüklüğü
    ceza = sum(max(alt - d, d - ust) for _, d, alt, ust in kotu)
    return {'gecti': not kotu, 'ceza': float(ceza), 'kotu': kotu,
            'satir': satir}


if __name__ == '__main__':
    ikili = sys.argv[1] if len(sys.argv) > 1 else KOK + '/cik/r'
    d = sina(ikili)
    print()
    print('SONUÇ: ' + ('GEÇTİ' if d['gecti'] else
                       'KALDI (toplam sapma %.1f dB)' % d['ceza']))
    sys.exit(0 if d['gecti'] else 1)
