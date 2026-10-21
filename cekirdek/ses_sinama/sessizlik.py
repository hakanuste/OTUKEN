#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SESSIZLIK — duraklar gercekten susuyor mu?

NEDEN BU SINAMA VAR: cikis katindaki tek kutuplu suzgeclerin durumu
TAMSAYI tutuluyordu. Pes rafinin katsayisi 0.0092 kadar kucuk oldugu
icin guncelleme siklikla sifira yuvarlaniyor, durum bir OLU BOLGEye
takiliyor ve orada kaliyordu. Sonuc: cumleler arasi duraklarda cikis
hic susmuyor, sabit bir DC basamaginda kilitleniyordu — olculen deger
tepe duzeyinin yalnizca 3.5 dB altiydi.

Soz boylece hic kesilmeyen bir basamagin ustunden geliyordu; duraklar
duyulmuyor, sozcukler birbirinden ayrilmiyordu. Hicbir tayf olcutu
bunu gostermedi: bant dengesi de, formant kestirimi de gecer not
veriyordu. Dogrudan bakmak gerekiyordu.

Olcut: duraklarda tepe duzeyinin en az 40 dB altina inilmeli ve
kaydin DC kacigi ihmal edilebilir olmali.
"""
import subprocess, sys, os, wave
import numpy as np

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
METIN = "Ötüken. Ata. Kuş."
SESSIZ_ESIK_DB = -40.0
DC_ESIK = 50          # 16 bitte ihmal edilebilir


def sina(ikili, ayrinti=True):
    wav = os.path.join(KOK, 'cik', 'sessizlik.wav')
    os.makedirs(os.path.dirname(wav), exist_ok=True)
    subprocess.run([ikili, METIN, wav], capture_output=True)
    w = wave.open(wav)
    hz = w.getframerate(); k = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float)
    if k > 1:
        x = x.reshape(-1, k).mean(1)

    n = int(hz * 0.005)
    kk = len(x) // n
    e = np.sqrt((x[:kk * n].reshape(kk, n) ** 2).mean(1))
    db = 20 * np.log10(e / max(e.max(), 1e-9) + 1e-12)

    # En uzun "durgun" bolge: ardisik, komsularina gore degismeyen dilimler.
    # Duraklar metindeki nokta sayisindan bilinir ama konumu sabit degil;
    # bu yuzden en sessiz %20'lik dilimlerin medyanina bakiliyor.
    en_sessiz = np.sort(db)[:max(1, kk // 5)]
    durak_db = float(np.median(en_sessiz))
    dc = float(abs(x.mean()))

    tamam_sessiz = durak_db <= SESSIZ_ESIK_DB
    tamam_dc = dc <= DC_ESIK
    if ayrinti:
        print('durak duzeyi   %7.1f dB  (en cok %.0f)   %s'
              % (durak_db, SESSIZ_ESIK_DB, 'tamam' if tamam_sessiz else 'DISARIDA'))
        print('DC kacigi      %7.1f     (en cok %d)    %s'
              % (dc, DC_ESIK, 'tamam' if tamam_dc else 'DISARIDA'))
    return {'gecti': tamam_sessiz and tamam_dc,
            'durak_db': durak_db, 'dc': dc}


if __name__ == '__main__':
    ikili = sys.argv[1] if len(sys.argv) > 1 else os.path.join(KOK, 'cik', 'r')
    d = sina(ikili)
    print('SONUÇ: ' + ('GEÇTİ' if d['gecti'] else 'KALDI'))
    sys.exit(0 if d['gecti'] else 1)
