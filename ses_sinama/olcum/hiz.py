#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HIZ — hece hizi ve hece yaylarinin gucu olculur.

NEDEN BU SINAMA VAR
  Butun tayf olcutleri (unlu formantlari, unsuz ayrimi, vurgu, denge,
  sessizlik, tasma) GECERKEN kullanici "hicbir sey anlasilmiyor" dedi.
  Eksik olcut buydu.

  Konusma kulakta HECE YAYLARI olarak cozulur. Isaretin zarfinin kendi
  tayfina (kiplenim tayfi) bakildiginda saglikli konusma 3-8 Hz'de tepe
  yapar. Olgun bir formant sentezleyicisiyle (espeak-ng, Turkce) ayni
  cumle karsilastirildi:
        espeak-ng : hece hizi 3.6 Hz, 3-8 Hz payi %20.8
        DEDE      : hece hizi 2.1 Hz, 3-8 Hz payi %13.2
  Ayni metin espeak'te 8.13 sn, DEDE'de 9.51 sn; 27 hece icin saniyede
  2.8 hece. Turkcede bu sayi 5-6'dir. DEDE iki kat yavas konusuyordu ve
  hece yaylari olagan araligin ALTINDA kaliyordu.

YANLIS OLCUMDEN DERS
  Bundan once "tepe sayisi" olculmustu ve DEDE hece basina 2.07, espeak
  1.15 tepe cikiyordu. O olcum HATALIYDI: zarf ornege gore duzlestirilmis,
  en az tepe araligi konmamis, espeak'in dusuk ornekleme hizi hesaba
  katilmamisti. Duzgun olculdugunde DEDE 46-54, espeak 54-58 tepe veriyor
  — yani DEDE'nin tepesi DAHA AZ. Uzerine kurulan "kesiklik bayraklari"
  cozumu de bu yuzden ise yaramadi ve cikisi tasirdi; geri alindi.
  Olcum aracini olcmek, olcumu kullanmaktan once gelir.
"""
import os, subprocess, sys
import numpy as np

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
METIN = ("Ötüken'in yurdunda ocak tütüyor. "
         "Bilge Kağan sözünü söyledi, budun dinledi.")
HECE = 27                 # metindeki unlu sayisi

HIZ_ALT, HIZ_UST = 3.0, 8.0     # hece hizi bu aralikta olmali
PAY_ALT = 0.16                  # 3-8 Hz bandinin en az payi


def wav_oku(yol):
    import wave
    w = wave.open(yol)
    n, k = w.getnframes(), w.getnchannels()
    x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64)
    if k > 1:
        x = x.reshape(-1, k).mean(1)
    return x, w.getframerate()


def zarf(x, hz, kesim=100.0, hedef_hz=400.0):
    """Tam dalga dogrultma + tek kutuplu alcak gecirem, sonra seyreltme.

    Kesim ZAMANA gore verilir (Hz), ornege gore DEGIL: yoksa farkli
    ornekleme hizindaki iki kayit farkli duzlestirilir ve
    karsilastirma anlamsiz olur. Onceki yanlis olcum tam buydu.
    """
    y = np.abs(x)
    a = np.exp(-2.0 * np.pi * kesim / hz)
    z = np.empty_like(y)
    s = 0.0
    for i in range(len(y)):
        s = a * s + (1.0 - a) * y[i]
        z[i] = s
    adim = max(1, int(round(hz / hedef_hz)))
    return z[::adim], hz / adim


def kiplenim(x, hz):
    z, zhz = zarf(x, hz)
    z = z - z.mean()
    if len(z) < 64:
        return 0.0, 0.0
    n = 1 << int(np.ceil(np.log2(len(z))))
    S = np.abs(np.fft.rfft(z * np.hanning(len(z)), n)) ** 2
    fr = np.fft.rfftfreq(n, 1.0 / zhz)
    toplam = S[(fr >= 1) & (fr < 200)].sum() + 1e-12
    pay = S[(fr >= HIZ_ALT) & (fr < HIZ_UST)].sum() / toplam
    sec = (fr >= 1.5) & (fr < 10)
    hiz = float(fr[sec][np.argmax(S[sec])])
    return hiz, float(pay)


def sina(ikili, ayrinti=True):
    wav = os.path.join(KOK, 'cik', 'hiz.wav')
    os.makedirs(os.path.dirname(wav), exist_ok=True)
    subprocess.run([ikili, METIN, wav], capture_output=True)
    x, hz = wav_oku(wav)
    sure = len(x) / hz
    hiz, pay = kiplenim(x, hz)
    hece_sn = HECE / sure if sure > 0 else 0.0

    gecti = (HIZ_ALT <= hiz <= HIZ_UST) and pay >= PAY_ALT
    if ayrinti:
        print('sure            : %.2f sn  (%d hece -> saniyede %.1f hece)'
              % (sure, HECE, hece_sn))
        print('hece hizi       : %.1f Hz   (olmasi gereken %.0f-%.0f Hz)'
              % (hiz, HIZ_ALT, HIZ_UST))
        print('3-8 Hz payi     : %%%.1f     (en az %%%.0f)'
              % (pay * 100, PAY_ALT * 100))
        print('karsilastirma   : espeak-ng Turkce 3.6 Hz, %20.8')
    return {'gecti': gecti, 'hiz': hiz, 'pay': pay,
            'sure': sure, 'hece_sn': hece_sn}


if __name__ == '__main__':
    ikili = sys.argv[1] if len(sys.argv) > 1 else os.path.join(KOK, 'cik', 'r')
    d = sina(ikili)
    print('SONUÇ: ' + ('GEÇTİ' if d['gecti'] else 'KALDI'))
    sys.exit(0 if d['gecti'] else 1)
