#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
EGIT_DERLEM — US1 modelini GERCEK derlem uzerinde egitir.

Onceki egitim ~900 karakterlik bir ornek metinle yapilmisti; model
Turkcenin harf istatistigini bile ogrenemezdi. Bu betik kitapligin
tamamini derlem olarak kullanir.

MOTORUN SINIRLARI (us/us1.h) asilmaz:
    d <= 96, katman <= 4, ffn <= 256, sozluk <= 128, baglam <= 96
Bu sinirlar icindeki en buyuk model ~400 bin degiskendir.

DURUST BEKLENTI: 400 bin degiskenli, karakter duzeyinde bir model
Turkcenin harf dizilimini, unlu uyumunu ve sik sozcuk bicimlerini
ogrenir. Anlamli cumle KURMAZ. Tutarli metin ureten en kucuk modeller
yuz milyon degisken dolayindadir — bunun iki yuz katindan fazla.
Buradaki kazanim, egitim hattinin ucdan uca kanitlanmasi ve gercek
bir taban olcumudur.

Kullanim:
    python3 arac/egit_derlem.py <kut_dizini> <cikti_dizini> [adim]
"""
import os, sys, time, json, collections
import numpy as np

BURASI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(BURASI, 'us1'))
from us1 import Kucuk, model_yaz          # noqa: E402
import egit as egitim                      # ileri_ara / geri  # noqa: E402

# Motor sinirlari icindeki en buyuk model
D, KATMAN, BAS, FFN, BAGLAM = 96, 4, 4, 256, 96
SOZLUK_AZAMI = 128
DOGRULAMA_ORAN = 0.02


def derlem_oku(kut):
    """Parca dosyalarinin GOVDESINI toplar; kunye satirlari atilir."""
    metinler = []
    for kok, _, dosyalar in os.walk(kut):
        for d in sorted(dosyalar):
            if not d.endswith('.txt') or d in ('sozluk.txt', 'ayar.txt',
                                               'bengi.txt'):
                continue
            yol = os.path.join(kok, d)
            try:
                t = open(yol, encoding='utf-8').read()
            except Exception:
                continue
            # kunye blogu: bastaki '#' satirlari ve ardindaki bos satir
            satirlar = t.split('\n')
            i = 0
            while i < len(satirlar) and satirlar[i].startswith('#'):
                i += 1
            while i < len(satirlar) and not satirlar[i].strip():
                i += 1
            govde = '\n'.join(satirlar[i:]).strip()
            if len(govde) > 50:
                metinler.append(govde)
    return '\n\n'.join(metinler)


def sozluk_kur(metin):
    """En sik SOZLUK_AZAMI-1 karakter; gerisi tek bir 'bilinmeyen'e."""
    say = collections.Counter(metin)
    sik = [c for c, _ in say.most_common(SOZLUK_AZAMI - 1)]
    sik = sorted(sik)
    harfler = sik + ['\x1a']                  # son yuva: bilinmeyen
    im = {c: i for i, c in enumerate(harfler)}
    bilinmeyen = len(harfler) - 1
    kapsam = sum(say[c] for c in sik) / max(1, len(metin))
    return harfler, im, bilinmeyen, kapsam


def kayip_olc(m, dizi, im_sayi=40, tohum=99):
    """Ortalama capraz-entropi; dogrulama diliminden ornekler."""
    r = np.random.RandomState(tohum)
    toplam = 0.0
    for _ in range(im_sayi):
        i = r.randint(0, len(dizi) - BAGLAM - 1)
        ara = egitim.ileri_ara(m, dizi[i:i + BAGLAM])
        k, _, _ = egitim.geri(m, ara, dizi[i + 1:i + BAGLAM + 1])
        toplam += k
    return toplam / im_sayi


def egit(dizi_egit, dizi_dog, sozluk, adim, cikti, gunluk):
    m = Kucuk(sozluk=sozluk, d=D, katman=KATMAN, bas=BAS,
              ffn=FFN, baglam=BAGLAM)
    par = m.parametreler()
    degisken = sum(p.size for p in par)
    yaz(gunluk, 'model: d=%d katman=%d bas=%d ffn=%d baglam=%d sozluk=%d'
        % (D, KATMAN, BAS, FFN, BAGLAM, sozluk))
    yaz(gunluk, 'degisken sayisi: %d' % degisken)

    mu = [np.zeros_like(p) for p in par]
    vu = [np.zeros_like(p) for p in par]
    r = np.random.RandomState(1)
    t0 = time.time()
    en_iyi = float('inf')

    for t in range(1, adim + 1):
        # ogrenme orani: isinma + kosinus inis
        if t < 2000:
            oran = 1e-3 * t / 2000.0
        else:
            ilerleme = (t - 2000) / max(1, adim - 2000)
            oran = 1e-3 * (0.05 + 0.95 * 0.5 * (1 + np.cos(np.pi * ilerleme)))

        i = r.randint(0, len(dizi_egit) - BAGLAM - 1)
        ara = egitim.ileri_ara(m, dizi_egit[i:i + BAGLAM])
        kayip, g, gk = egitim.geri(m, ara, dizi_egit[i + 1:i + BAGLAM + 1])

        grad = [g['gomme']]
        for j in range(m.katman):
            grad += [gk[j][a] for a in ('n1', 'wq', 'wk', 'wv', 'wo',
                                        'n2', 'w1', 'w2', 'w3')]
        grad += [g['son_norm'], g['cikis']]
        for p, gr, mm, vv in zip(par, grad, mu, vu):
            np.clip(gr, -1.0, 1.0, out=gr)
            mm *= 0.9; mm += 0.1 * gr
            vv *= 0.999; vv += 0.001 * gr * gr
            p -= oran * (mm / (1 - 0.9 ** t)) / (np.sqrt(vv / (1 - 0.999 ** t)) + 1e-8)

        if t % 5000 == 0 or t == adim:
            dk = kayip_olc(m, dizi_dog)
            gecen = time.time() - t0
            yaz(gunluk, 'adim %7d/%d  egitim %.4f  dogrulama %.4f  '
                        'oran %.2e  %.0f im/sn  gecen %.0f sn'
                % (t, adim, kayip, dk, oran, t * BAGLAM / max(gecen, 1e-9), gecen))
            # EN IYI dogrulama kaydedilir: asiri uyum olursa son model
            # degil, en iyisi kalir.
            if dk < en_iyi:
                en_iyi = dk
                boy = model_yaz(m, os.path.join(cikti, 'umay.us1'))
                with open(os.path.join(cikti, 'durum.json'), 'w') as f:
                    json.dump({'adim': t, 'dogrulama': float(dk),
                               'egitim': float(kayip), 'bayt': boy,
                               'degisken': int(degisken)}, f)
    return m, en_iyi


def yaz(gunluk, s):
    print(s, flush=True)
    with open(gunluk, 'a', encoding='utf-8') as f:
        f.write(s + '\n')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('kullanim: egit_derlem.py <kut_dizini> <cikti_dizini> [adim]')
        sys.exit(2)
    kut, cikti = sys.argv[1], sys.argv[2]
    adim = int(sys.argv[3]) if len(sys.argv) > 3 else 400000
    os.makedirs(cikti, exist_ok=True)
    gunluk = os.path.join(cikti, 'egitim.log')

    metin = derlem_oku(kut)
    yaz(gunluk, 'derlem: %d karakter' % len(metin))
    if len(metin) < 10000:
        yaz(gunluk, 'DERLEM COK KUCUK - egitim anlamsiz olur, duruldu.')
        sys.exit(1)

    harfler, im, bilinmeyen, kapsam = sozluk_kur(metin)
    yaz(gunluk, 'sozluk: %d im, kapsam %%%.2f' % (len(harfler), kapsam * 100))
    dizi = np.array([im.get(c, bilinmeyen) for c in metin], dtype=np.int32)

    kes = int(len(dizi) * (1 - DOGRULAMA_ORAN))
    dizi_egit, dizi_dog = dizi[:kes], dizi[kes:]
    yaz(gunluk, 'egitim %d im, dogrulama %d im' % (len(dizi_egit), len(dizi_dog)))
    yaz(gunluk, 'adim %d  (~%.1f tur)' % (adim, adim * BAGLAM / len(dizi_egit)))

    with open(os.path.join(cikti, 'sozluk.txt'), 'w', encoding='utf-8') as f:
        f.write(''.join(harfler))

    m, en_iyi = egit(dizi_egit, dizi_dog, len(harfler), adim, cikti, gunluk)
    yaz(gunluk, 'BITTI. en iyi dogrulama kaybi: %.4f' % en_iyi)
