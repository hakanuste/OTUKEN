#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UNSUZ — unsuzler birbirinden ayirt edilebiliyor mu?

NEDEN BU SINAMA VAR: "anlasilirlik" sinamasi yalnizca UNLULERE
bakiyordu ve hepsi gecer not aliyordu. Oysa kullanici "hicbir sey
anlasilmiyor" dedi. Olculdugunde sebep ortaya cikti: b, d ve g BIT BIT
AYNI ses cikiyordu (tayf uzakligi 0.00 dB), p-k 1.99, t-c 1.65, m-n
2.09 dB — hepsi ayirt etme sinirinin altinda. Turkceyi tasiyan
unsuzlerdir; unluler kusursuz olsa bile sozcuk cozulmuyordu.

Bir unsuzun kimligi IKI yerden okunur:
  1. KENDI TAYFI — patlamanin ya da hisirtinin rengi.
  2. KOMSU UNLUYU NASIL BUKTUGU — ikinci formantin gittigi yer
     ("locus"). Dudakta ~750 Hz, diste ~1750, artdamakta ~2200
     (on unlude) ya da ~1150 (arka unlude).

Ilk olcum yalniz (1)'e bakiyordu ve unsuzun ORTASINA pencere aciyordu;
geçisler pencerenin disinda kaliyor, yani asil ipucu olculmuyordu.
Burada ikisi birden olculur.

Olcut: hicbir unsuz cifti esik degerin altinda olmamali.
"""
import os, subprocess, sys, itertools
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'olcum'))
import numpy as np
import olc

KOK = os.path.dirname(os.path.abspath(__file__))
SESLER = ['b', 'd', 'g', 'p', 't', 'k', 'm', 'n', 'l', 'r',
          'y', 'v', 's', 'ş', 'z', 'j', 'f', 'h', 'ç']
ESIK = 1.6            # ortak olcek birimi


def mel_bant(x, hz, n=1024, bant=20):
    k = max(1, len(x) // n)
    if len(x) < n:
        return np.zeros(bant)
    S = np.abs(np.fft.rfft(x[:k * n].reshape(k, n) * np.hanning(n), axis=1)) ** 2
    g = S.mean(0)
    f = np.fft.rfftfreq(n, 1.0 / hz)
    kenar = np.linspace(np.log10(1 + 100 / 700.0), np.log10(1 + 8000 / 700.0), bant + 1)
    hzk = 700 * (10 ** kenar - 1)
    v = np.array([10 * np.log10(g[(f >= hzk[i]) & (f < hzk[i + 1])].sum() + 1e-12)
                  for i in range(bant)])
    return v - v.mean()


def f2_izi(x, hz):
    """Unlu-unsuz-unlu boyunca F2 ve F3. Locus bu izde okunur."""
    from scipy.signal import resample_poly
    from math import gcd
    if hz > 11025:
        g = gcd(int(hz), 10000)
        x = resample_poly(x, 10000 // g, int(hz) // g)
        hz = 10000
    n = int(hz * 0.020)
    h = int(hz * 0.005)
    say = max(0, 1 + (len(x) - n) // h)
    iz = []
    for i in range(say):
        c = x[i * h:i * h + n].copy()
        if c.std() < 1:
            iz.append((np.nan, np.nan)); continue
        c[1:] -= 0.97 * c[:-1]
        a = olc.lpc(c, 12)
        if a is None:
            iz.append((np.nan, np.nan)); continue
        kok = np.roots(a); kok = kok[np.imag(kok) > 0]
        f = np.angle(kok) * hz / (2 * np.pi)
        b = -np.log(np.abs(kok) + 1e-12) * hz / np.pi
        f = np.sort(f[(f > 150) & (f < 4000) & (b < 600)])
        iz.append((f[1], f[2]) if len(f) >= 3 else (np.nan, np.nan))
    return np.array(iz)


def oznitelik(ikili, harf):
    wav = os.path.join(KOK, 'cik', 'u_%s.wav' % harf.encode('ascii', 'replace').decode())
    os.makedirs(os.path.dirname(wav), exist_ok=True)
    subprocess.run([ikili, 'a%sa' % harf, wav], capture_output=True)
    x, hz = olc.wav_oku(wav)
    n = len(x)
    # (1) unsuzun kendi tayfi
    tayf = mel_bant(x[int(n * 0.40):int(n * 0.60)], hz)
    # (2) gecisteki F2/F3: unsuze giris ve cikis
    iz = f2_izi(x, hz)
    m = len(iz)
    giris = np.nanmedian(iz[int(m * 0.28):int(m * 0.40)], axis=0)
    cikis = np.nanmedian(iz[int(m * 0.60):int(m * 0.72)], axis=0)
    gec = np.nan_to_num(np.concatenate([giris, cikis]), nan=1500.0)
    # Hz'i dB olcegine denk bir birime cevir: 100 Hz ~ 1 birim
    return np.concatenate([tayf, gec / 100.0])


def sina(ikili, ayrinti=True):
    oz = {c: oznitelik(ikili, c) for c in SESLER}
    ciftler = sorted((float(np.sqrt(((oz[a] - oz[b]) ** 2).mean())), a, b)
                     for a, b in itertools.combinations(SESLER, 2))
    kotu = [(d, a, b) for d, a, b in ciftler if d < ESIK]
    if ayrinti:
        print('en yakin unsuz ciftleri (tayf + gecis birlikte):')
        for d, a, b in ciftler[:8]:
            print('   %s - %s  %5.2f  %s' % (a, b, d, '<-- AYIRT EDILEMEZ' if d < ESIK else ''))
        print('en yakin %.2f (esik %.1f)   ortalama %.2f'
              % (ciftler[0][0], ESIK, np.mean([c[0] for c in ciftler])))
    return {'gecti': not kotu, 'en_yakin': ciftler[0][0],
            'ortalama': float(np.mean([c[0] for c in ciftler])), 'kotu': kotu}


if __name__ == '__main__':
    ikili = sys.argv[1] if len(sys.argv) > 1 else os.path.join(KOK, 'cik', 'r')
    d = sina(ikili)
    print('SONUÇ: ' + ('GEÇTİ' if d['gecti'] else 'KALDI'))
    sys.exit(0 if d['gecti'] else 1)
