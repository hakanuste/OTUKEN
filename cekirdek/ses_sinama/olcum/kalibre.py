#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""KALIBRE — her sesin genlik yuzdesini olcup cozer.

Gurultu genligi ile cikis duzeyi dogrusaldir, bu yuzden tek adimda
cozulur: genlik = 100 * 10^((hedef - olculen)/20). Dogrulama icin
iki tur kosulur."""
import subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, olc, denge

KOK = os.environ.get('DEDE_KOK', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEDEF = {'s': -15, 'ş': -15, 'z': -15, 'j': -15,
         'f': -20, 'h': -20,
         'p': -20, 't': -20, 'k': -20, 'ç': -20,
         'm': -7, 'n': -7}
ANAHTAR = {'s': 'GEN_S', 'ş': 'GEN_SH', 'f': 'GEN_F', 'h': 'GEN_H',
           'z': 'GEN_Z', 'j': 'GEN_J', 'p': 'GEN_P', 't': 'GEN_T',
           'k': 'GEN_K', 'ç': 'GEN_C', 'm': 'GEN_MN', 'n': 'GEN_MN'}


def derle(gen, cikti):
    b = ['-D%s=%d' % (k, v) for k, v in gen.items()]
    r = subprocess.run(['gcc', '-O2', '-DDEDE_OLCUM',
                        '-DDEDE_KAYNAK="%s/src/dede.c"' % os.environ.get('OT_KOK', '/home/claude/ot'),
                        '-I', KOK + '/kabuk',
                        '-I', os.environ.get('OT_KOK','/home/claude/ot') + '/include'] + b +
                       ['-o', cikti, KOK + '/kabuk/render.c', '-lm'],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:400]); sys.exit(1)


gen = {v: 100 for v in set(ANAHTAR.values())}
for tur in range(3):
    ikili = KOK + '/cik/rk'
    derle(gen, ikili)
    taban = np.mean([denge.duzey(ikili, u) for u in 'aeiou'])
    print('--- tur %d ---' % (tur + 1))
    yeni = dict(gen)
    for harf, hed in HEDEF.items():
        d = denge.duzey(ikili, harf) - taban
        a = ANAHTAR[harf]
        duzelt = 10 ** ((hed - d) / 20.0)
        # m ve n ayni anahtari paylasir: ortalama al
        yeni[a] = max(1, min(4000, int(round(gen[a] * duzelt))))
        print('  %s %+7.1f dB -> hedef %+d   %s %d -> %d'
              % (harf, d, hed, a, gen[a], yeni[a]))
    gen = yeni
print()
print('SONUC:', ' '.join('%s=%d' % kv for kv in sorted(gen.items())))
