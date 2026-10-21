#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TARA — dede.c sabitlerini hedef sese karsi tarar.

Her bilesim icin cekirdek kaynagi YENIDEN DERLENIR; olculen ses,
cekirdekte calisacak kodun ta kendisidir. Tahminle sabit secmek
bu projede bir kez pahaliya mal oldu (85 Hz'lik "alcak gecirenin"
aslinda dar bir cinlayici oldugu ancak olcumle goruldu).
"""
import itertools, os, subprocess, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import olc, anlasilirlik, denge

KOK = os.environ.get('DEDE_KOK', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
METIN = ("Ötüken'in yurdunda ocak tütüyor. Bilge Kağan sözünü söyledi, "
         "budun dinledi. Su akar, taş kalır; söz uçar, yazı kalır.")

# 3-5 kHz BILEREK DISARIDA.
# Hedef kayit "AudioCleaner" ile islenmis: tayfinda 3.5-5 kHz'de bir
# yukselti, hemen ardindan 5 kHz'de bir UCURUM var (5 kHz'den 6 kHz'e
# 23 dB dusuyor). Bu akustik degil, sureç: bir exciter ve bir kesim
# suzgeci. Insan sesinin uzun donem tayfi oralarda duzgun iner.
# O bolgeyi taklit etmek, kaydin islemesini taklit etmek olurdu.
HEDEF = {
    'bant_60-200': -1.2, 'bant_200-500': -9.9, 'bant_0.5-1k': -14.9,
    'bant_1-2k': -14.9, 'bant_2-3k': -17.6,
}
HEDEF_EGIM = -13.9


def derle(sabitler, cikti):
    bayrak = ['-D%s=%s' % (k, v) for k, v in sabitler.items()]
    r = subprocess.run(
        ['gcc', '-O2', '-DDEDE_OLCUM',
                        '-DDEDE_KAYNAK="%s/src/dede.c"' % os.environ.get('OT_KOK', '/home/claude/ot'),
                        '-I', KOK + '/kabuk',
         '-I', os.environ.get('OT_KOK','/home/claude/ot') + '/include'] + bayrak +
        ['-o', cikti, KOK + '/kabuk/render.c', '-lm'],
        capture_output=True, text=True)
    return r.returncode == 0, r.stderr


def kos(ikili, wav, metin=METIN):
    r = subprocess.run([ikili, metin, wav], capture_output=True, text=True)
    kirpma, oran = 0, 0.0
    for parca in r.stdout.split():
        if parca.startswith('kirpma='):
            kirpma = int(parca.split('=')[1])
        if parca.startswith('oran='):
            oran = float(parca.split('=')[1])
    return kirpma, oran


def puan(d, kirpma, tepe, oran=0.0):
    s = 0.0
    for k, h in HEDEF.items():
        s += abs(d[k] - h)
    # Tayf EGIMI puana girmiyor: hedefin tayfi tek bir dogruyla
    # iyi anlatilmiyor (cok agir bir pes + duzce bir orta-tiz).
    # Alti bant seviyesi sekli dogru yakaliyor; egim yalniz rapor
    # icin olculuyor.
    ceza = 0.0
    if kirpma:
        ceza += 50
    if tepe < 8000:
        ceza += (8000 - tepe) / 400.0      # cok kisik
    # Cikista tasma: %0.1'in ustu duyulur bozulmadir.
    if oran > 0.001:
        ceza += 200 * (oran - 0.001) + 10
    return s + ceza, s


def tepe_oku(wav):
    import wave, numpy as np
    w = wave.open(wav); n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype='<i2')
    return int(abs(x).max())


def tara(izgara, etiket='tara'):
    adlar = list(izgara)
    sonuc = []
    for i, degerler in enumerate(itertools.product(*[izgara[a] for a in adlar])):
        sab = dict(zip(adlar, degerler))
        ikili = '%s/cik/r_%s_%d' % (KOK, etiket, i)
        tamam, hata = derle(sab, ikili)
        if not tamam:
            print('DERLEME HATASI', sab, hata[:200]); continue
        wav = '%s/cik/w_%s_%d.wav' % (KOK, etiket, i)
        kirpma, oran = kos(ikili, wav)
        d = olc.olc(wav)
        tepe = tepe_oku(wav)
        p, ham = puan(d, kirpma, tepe, oran)
        # ANLASILIRLIK SERT KOSULDUR: tini ayari onu bozamaz.
        # Onceki surum tam da burada kaybedilmisti - bant olcutleri
        # tutuyor ama unluler ayirt edilmiyordu.
        a = anlasilirlik.sina(ikili, ayrinti=False)
        if not a['gecti']:
            p += 100
        # SES DENGESI de sert kosul: 3-5 kHz bandini "s"yi bagirtarak
        # doldurmak ucuz ama yikici bir cozumdur; tarama onu secmesin.
        g = denge.sina(ikili, ayrinti=False)
        p += g['ceza']
        a['denge'] = g['gecti']
        sonuc.append((p, ham, sab, d, kirpma, tepe, oran, a))
        os.remove(ikili); os.remove(wav)
    sonuc.sort(key=lambda t: t[0])
    return sonuc


def yaz(sonuc, kac=8):
    print('%-7s %-7s %-7s %-8s %-6s %s'
          % ('puan','sapma','tepe','tasma','anla','sabitler'))
    for p, ham, sab, d, kirpma, tepe, oran, a in sonuc[:kac]:
        print('%-7.1f %-7.1f %-7d %-8.4f %-6s %s  F0=%.0f egim=%.1f'
              % (p, ham, tepe, oran, ('ok' if a['gecti'] else 'ANLA') + ('' if a['denge'] else '/DENGE'),
                 ' '.join('%s=%s' % kv for kv in sab.items()),
                 d['f0_orta'], d['egim']))
