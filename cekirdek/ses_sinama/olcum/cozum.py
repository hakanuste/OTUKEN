#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
COZUM — tini kati ile ses dengesini BIRLIKTE cozer.

NEDEN IC ICE: cikistaki varlik (presence) yukseltmesi her seyi
etkiler, surtunmeli sesleri unlulerden cok daha fazla. Yani tini
degisince ses dengesi bozulur; dengeyi duzeltince tayf bandi
degisir. Ikisini ayri ayri taramak donup duruyor.

Burada her tini adayi icin:
  1. genlik degerleri OLCUMLE cozulur (kalibre),
  2. elde edilen yapida bantlar, anlasilirlik ve denge olculur,
  3. yalniz o zaman puanlanir.
"""
import os, subprocess, sys, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import olc, denge, anlasilirlik, tara

KOK = os.environ.get('DEDE_KOK', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEDEF_GEN = {'s': -15, 'ş': -15, 'z': -15, 'j': -15,
             'f': -20, 'h': -20, 'p': -20, 't': -20, 'k': -20, 'ç': -20,
             'm': -7, 'n': -7}
ANAHTAR = {'s': 'GEN_S', 'ş': 'GEN_SH', 'f': 'GEN_F', 'h': 'GEN_H',
           'z': 'GEN_Z', 'j': 'GEN_J', 'p': 'GEN_P', 't': 'GEN_T',
           'k': 'GEN_K', 'ç': 'GEN_C', 'm': 'GEN_MN', 'n': 'GEN_MN'}


def derle(sab, cikti):
    b = ['-D%s=%s' % (k, v) for k, v in sab.items()]
    r = subprocess.run(['gcc', '-O2', '-DDEDE_OLCUM',
                        '-DDEDE_KAYNAK="%s/src/dede.c"' % os.environ.get('OT_KOK', '/home/claude/ot'),
                        '-I', KOK + '/kabuk',
                        '-I', os.environ.get('OT_KOK','/home/claude/ot') + '/include'] + b +
                       ['-o', cikti, KOK + '/kabuk/render.c', '-lm'],
                       capture_output=True, text=True)
    return r.returncode == 0


def kalibre(tini, ikili, tur=3):
    gen = {v: 1000 for v in set(ANAHTAR.values())}
    for _ in range(tur):
        sab = dict(tini); sab.update(gen)
        if not derle(sab, ikili):
            return None
        taban = np.mean([denge.duzey(ikili, u) for u in 'aeiou'])
        yeni = dict(gen)
        for harf, hed in HEDEF_GEN.items():
            d = denge.duzey(ikili, harf) - taban
            a = ANAHTAR[harf]
            yeni[a] = max(1, min(60000, int(round(gen[a] * 10 ** ((hed - d) / 20.0)))))
        gen = yeni
    return gen


def degerlendir(tini, etiket='c'):
    ikili = '%s/cik/rc_%s' % (KOK, etiket)
    gen = kalibre(tini, ikili)
    if gen is None:
        return None
    sab = dict(tini); sab.update(gen)
    if not derle(sab, ikili):
        return None
    wav = '%s/cik/wc_%s.wav' % (KOK, etiket)
    kirpma, oran = tara.kos(ikili, wav)
    d = olc.olc(wav)
    tepe = tara.tepe_oku(wav)
    p, ham = tara.puan(d, kirpma, tepe, oran)
    a = anlasilirlik.sina(ikili, ayrinti=False)
    g = denge.sina(ikili, ayrinti=False)
    if not a['gecti']:
        p += 100
    p += g['ceza']
    os.remove(wav)
    try:
        os.remove(ikili)
    except OSError:
        pass
    return {'puan': p, 'sapma': ham, 'tini': tini, 'gen': gen, 'olcum': d,
            'anla': a, 'denge': g, 'tepe': tepe, 'oran': oran}


def tara_tini(izgara, kac=6):
    adlar = list(izgara)
    sonuc = []
    for i, dg in enumerate(itertools.product(*[izgara[a] for a in adlar])):
        r = degerlendir(dict(zip(adlar, dg)), 'x%d' % i)
        if r:
            sonuc.append(r)
    sonuc.sort(key=lambda r: r['puan'])
    for r in sonuc[:kac]:
        print('%7.1f  sapma %5.1f  tepe %5d  anla %-5s denge %-5s  %s'
              % (r['puan'], r['sapma'], r['tepe'],
                 'ok' if r['anla']['gecti'] else 'KALDI',
                 'ok' if r['denge']['gecti'] else 'KALDI',
                 ' '.join('%s=%s' % kv for kv in r['tini'].items())))
    return sonuc
