#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OLC — ses olcum araci.

NEDEN AYRI BIR ARAC: "ses iyi mi" sorusu kulakla cevaplanamiyor (bu
ortamda ses kartı yok). Olculebilir buyuklukler uzerinden konusmak
gerekiyor. Burada olculenler:

  F0        temel perde (otokorelasyon, otumlu cerceveler)
  LTAS      uzun donem ortalama tayf, bantlara bolunmus
  egim      tayf egimi dB/onkat
  hece      enerji zarfindaki tepe sayisindan hece hizi
  sessizlik sessiz cerceve orani
  F1..F3    LPC kutuplarindan formant kestirimi

ONEMLI: formant kestirimi ANLASILIRLIGIN olcutudur. Sentezlenen bir
sesliden F1/F2 geri okunamiyorsa o sesli ayirt edilemez; ses "konusmaz",
ugultu olur. Tayf bantlari hedefe uysa bile bu bozulabilir.
"""
import sys, wave
import numpy as np

HZ = 48000


def wav_oku(yol):
    w = wave.open(yol)
    hz = w.getframerate()
    k = w.getnchannels()
    n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    if k > 1:
        x = x.reshape(-1, k).mean(axis=1)
    return x, hz


def cerceveler(x, hz, ms=32, atlama_ms=10):
    n = int(hz * ms / 1000)
    h = int(hz * atlama_ms / 1000)
    say = max(0, 1 + (len(x) - n) // h)
    return np.lib.stride_tricks.as_strided(
        x, shape=(say, n), strides=(x.strides[0] * h, x.strides[0])
    ), n


def f0_izi(x, hz, esik_oran=0.08):
    """Otokorelasyon ile perde izi. Sessiz cerceveler atlanir."""
    C, n = cerceveler(x, hz)
    if len(C) == 0:
        return np.array([]), 1.0
    rms = C.std(axis=1)
    esik = max(rms.max() * esik_oran, 1.0)
    canli = rms > esik
    # Erkek sesi araligi. Genis tutmak sekizli hatasi uretiyordu:
    # 80 Hz'lik bir ses 320 Hz olarak okunabiliyordu.
    asgari = int(hz / 220)      # 220 Hz ust sinir
    azami = int(hz / 55)        # 55 Hz alt sinir
    perde = []
    for i in np.where(canli)[0]:
        c = C[i] - C[i].mean()
        r = np.correlate(c, c, 'full')[n - 1:]
        if r[0] <= 0:
            continue
        r = r / r[0]
        bolge = r[asgari:azami]
        if len(bolge) == 0:
            continue
        j = int(np.argmax(bolge)) + asgari
        if r[j] < 0.30:          # otumsuz
            continue
        # parabolik ince ayar
        if 0 < j < len(r) - 1:
            a, b, c2 = r[j - 1], r[j], r[j + 1]
            d = a - 2 * b + c2
            if d != 0:
                j = j + 0.5 * (a - c2) / d
        perde.append(hz / j)
    return np.array(perde), float(1.0 - canli.mean())


def ltas(x, hz, bantlar):
    C, n = cerceveler(x, hz, ms=32, atlama_ms=16)
    if len(C) == 0:
        return {}, 0.0
    rms = C.std(axis=1)
    canli = rms > max(rms.max() * 0.08, 1.0)
    if canli.sum() < 4:
        canli = np.ones(len(C), bool)
    pencere = np.hanning(n)
    S = np.abs(np.fft.rfft(C[canli] * pencere, axis=1)) ** 2
    guc = S.mean(axis=0)
    frk = np.fft.rfftfreq(n, 1.0 / hz)
    toplam = guc.sum()
    out = {}
    for ad, (a, b) in bantlar.items():
        m = (frk >= a) & (frk < b)
        out[ad] = 10 * np.log10(max(guc[m].sum() / toplam, 1e-12))
    # egim: 150 Hz - 5 kHz arasi log-log dogru uydurma
    m = (frk >= 150) & (frk <= 5000) & (guc > 0)
    egim = np.polyfit(np.log10(frk[m]), 10 * np.log10(guc[m]), 1)[0]
    return out, float(egim)


def hece_hizi(x, hz):
    """Konusma ritmi: enerji zarfinin BASKIN kipleme frekansi (Hz).

    Tepe saymak kirilgan cikti: az duzlestirirsek her SES bir tepe
    verir (sentezlenen seste hiz 2.7 yerine 6.9 olculuyordu), cok
    duzlestirirsek duraklamalar hizi asagi ceker (hedefte 2.75 yerine
    1.60). Bunun yerine zarfin 1-10 Hz bandindaki tayfina bakiliyor:
    konusmanin hece ritmi orada baskin bir tepe olarak durur ve ayni
    olcu her iki kayitta da ayni seyi olcer."""
    n = int(hz * 0.01)                     # 100 Hz zarf
    k = len(x) // n
    if k < 64:
        return float('nan')
    e = np.sqrt((x[:k * n].reshape(k, n) ** 2).mean(axis=1))
    e = e - e.mean()
    S = np.abs(np.fft.rfft(e * np.hanning(len(e)))) ** 2
    f = np.fft.rfftfreq(len(e), 0.01)
    m = (f >= 1.0) & (f <= 10.0)
    return float(f[m][np.argmax(S[m])])


def lpc(c, p):
    """Levinson-Durbin. c: cerceve, p: derece."""
    c = c - c.mean()
    c = c * np.hanning(len(c))
    r = np.correlate(c, c, 'full')[len(c) - 1:len(c) - 1 + p + 1]
    if r[0] <= 0:
        return None
    a = np.zeros(p + 1); a[0] = 1.0
    e = r[0]
    for i in range(1, p + 1):
        k = -(a[:i] @ r[i:0:-1]) / e
        a[:i + 1] = a[:i + 1] + k * a[i::-1]
        e *= (1 - k * k)
        if e <= 0:
            return None
    return a


def formantlar(x, hz, derece=12, en_cok=4):
    """Otumlu cercevelerden LPC kutuplari ile formant kestirimi.

    LPC'yi 10 kHz'e indirilmis sinyalde kosuyoruz: formantlar 5 kHz
    altinda, derece 12 tam o bolgeyi kapsar. 48 kHz'de ayni derece
    kutuplari yukari savuruyor ve F1 yerine 2 kHz civari okunuyordu."""
    from scipy.signal import resample_poly
    if hz > 11025:
        from math import gcd
        g = gcd(int(hz), 10000)
        x = resample_poly(x, 10000 // g, int(hz) // g)
        hz = 10000
    C, n = cerceveler(x, hz, ms=32, atlama_ms=16)
    if len(C) == 0:
        return []
    rms = C.std(axis=1)
    canli = rms > max(rms.max() * 0.15, 1.0)
    biriken = [[] for _ in range(en_cok)]
    for i in np.where(canli)[0]:
        # on-vurgu: dengeli (0.97). Tam turev LPC'yi tize kapitiriyordu.
        c = C[i].copy()
        c[1:] -= 0.97 * c[:-1]
        a = lpc(c, derece)
        if a is None:
            continue
        kok = np.roots(a)
        kok = kok[np.imag(kok) > 0]
        if len(kok) == 0:
            continue
        f = np.angle(kok) * hz / (2 * np.pi)
        b = -np.log(np.abs(kok) + 1e-12) * hz / np.pi
        iyi = (f > 150) & (f < 5500) & (b < 700)
        f = np.sort(f[iyi])
        for j in range(min(en_cok, len(f))):
            biriken[j].append(f[j])
    return [float(np.median(v)) if len(v) > 20 else float('nan')
            for v in biriken]


BANTLAR = {
    '60-200':  (60, 200),
    '200-500': (200, 500),
    '0.5-1k':  (500, 1000),
    '1-2k':    (1000, 2000),
    '2-3k':    (2000, 3000),
    '3-5k':    (3000, 5000),
}


def olc(yol, ad=None):
    x, hz = wav_oku(yol)
    perde, sessiz = f0_izi(x, hz)
    bant, egim = ltas(x, hz, BANTLAR)
    hece = hece_hizi(x, hz)
    F = formantlar(x, hz)
    d = {
        'ad': ad or yol,
        'sure': len(x) / hz,
        'f0_orta': float(np.median(perde)) if len(perde) else float('nan'),
        'f0_10': float(np.percentile(perde, 10)) if len(perde) else float('nan'),
        'f0_90': float(np.percentile(perde, 90)) if len(perde) else float('nan'),
        'otumlu_oran': float(len(perde)) / max(1, int(len(x) / (hz * 0.01))),
        'sessizlik': sessiz,
        'egim': egim,
        'hece': hece,
        'F': F,
    }
    d.update({('bant_' + k): v for k, v in bant.items()})
    return d


def yaz(d):
    print('--- %s ---' % d['ad'])
    print('  sure          %7.2f sn' % d['sure'])
    print('  F0 ortanca    %7.1f Hz   (%%10-%%90: %.0f - %.0f)'
          % (d['f0_orta'], d['f0_10'], d['f0_90']))
    print('  otumlu oran   %7.2f' % d['otumlu_oran'])
    print('  sessizlik     %7.2f' % d['sessizlik'])
    print('  tayf egimi    %7.1f dB/onkat' % d['egim'])
    print('  hece hizi     %7.2f /sn' % d['hece'])
    for k in BANTLAR:
        print('  %-9s     %7.1f dB' % (k, d['bant_' + k]))
    print('  formantlar    ' + '  '.join(
        ('%.0f' % f) if f == f else '--' for f in d['F']))


if __name__ == '__main__':
    for yol in sys.argv[1:]:
        yaz(olc(yol))
