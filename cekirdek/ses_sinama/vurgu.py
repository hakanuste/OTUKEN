#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VURGU — heceler arasinda siddet karsitligi var mi?

NEDEN: butun olcutler gecerken ses yine anlasilmiyordu. Olculdugunde
sebep ortaya cikti: DEDE'nin butun heceleri AYNI siddette cikiyordu.
En guclu hece ile en zayifi arasindaki oran gercek konusmada 8.3 kat
(Soylev kaydi), yapay seste 6.5 kat, DEDE'de yalnizca 3.5 kat idi.
Ritim ve sure degiskenligi zaten yerindeydi — eksik olan buydu.

Butun heceler esit vurgulanirsa dinleyici sozcugun nerede baslayip
bittigini bulamaz: ses akar ama cozulmez. Turkcede vurgu kural olarak
son hecededir; bu yuzden sozcuk bir butun olarak seslendirilmelidir.

Olcut: en guclu/en zayif hece orani 4 katin uzerinde olmali.
"""
import os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'olcum'))
import numpy as np
import olc

KOK = os.path.dirname(os.path.abspath(__file__))
METIN = ("Ötüken'in yurdunda ocak tütüyor. Bilge Kağan sözünü söyledi, "
         "budun dinledi. Su akar, taş kalır; söz uçar, yazı kalır.")
ESIK = 4.0


def sina(ikili, ayrinti=True):
    wav = os.path.join(KOK, 'cik', 'vurgu.wav')
    os.makedirs(os.path.dirname(wav), exist_ok=True)
    subprocess.run([ikili, METIN, wav], capture_output=True)
    x, hz = olc.wav_oku(wav)
    n = int(hz * 0.01)
    k = len(x) // n
    e = np.sqrt((x[:k * n].reshape(k, n) ** 2).mean(1))
    c = np.hanning(9); c /= c.sum()
    es = np.convolve(e, c, 'same')
    esik = es.max() * 0.12
    t = [i for i in range(1, k - 1)
         if es[i] > es[i - 1] and es[i] >= es[i + 1] and es[i] > esik]
    if len(t) < 5:
        return {'gecti': False, 'oran': 0.0}
    g = np.array([es[i] for i in t])
    oran = float(g.max() / max(g.min(), 1e-9))
    if ayrinti:
        print('hece sayisi %d   en guclu/en zayif %.1f kat (en az %.1f)'
              % (len(t), oran, ESIK))
    return {'gecti': oran >= ESIK, 'oran': oran, 'hece': len(t)}


if __name__ == '__main__':
    ikili = sys.argv[1] if len(sys.argv) > 1 else os.path.join(KOK, 'cik', 'r')
    d = sina(ikili)
    print('SONUÇ: ' + ('GEÇTİ' if d['gecti'] else 'KALDI'))
    sys.exit(0 if d['gecti'] else 1)
