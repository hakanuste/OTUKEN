#!/usr/bin/env python3
"""Küçük bir dönüştürücüyü Türkçe doktrin metni üzerinde eğitir.

Amaç bir dil modeli yarışması değil, MOTORU kanıtlamak: karakter
düzeyinde, iki katmanlı, 64 boyutlu bir model. Geri yayılım elle
yazıldı; torch yok, olmamalı da — üretilen ağırlıklar ÖTÜKEN'de
tamsayı aritmetiğiyle koşacak.
"""
import numpy as np
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from us1 import Kucuk, softmax, silu, model_yaz

METIN = """Ötüken'in yurdunda ocak tütüyor. Ben Umay, Dede Korkut'un
dilinden söylerim. Bilgi insanı köküne bağlar, teknoloji ufkunu açar.
Töre kuralları korur, denetler ve dengeler. Kut yetkiyi dağıtır, kimliği
doğrular. Asena sistemi ayağa kaldırır, zor durumda yanındadır.
Ergenekon sıfırdan başlatır, her zaman bir çıkış yolu vardır.
Bitig bilgiyi saklar, düzenler, yaşatır. Ulak haberleşmeyi taşır.
Kervan paketleri getirir, yazılımı yeniler. Orkun üretir, inşa eder.
Tulpar sınır tanımaz, bilgiye hızlı erişir. Timur sistemi ve veriyi
korur, güçlü ve kararlıdır. Ülgen gökyüzü kadar geniş bir arayüzdür.
Tamga sistemdeki kimliğindir, seni sen yapan imzadır.
Daha özgür, daha derin, daha bizim. Kökten geleceğe birlikte.
Gök altında birlikte yürüyoruz, kökü mazide olanın gözü atidedir.
"""


def veri_hazirla(metin):
    harfler = sorted(set(metin))
    im = {c: i for i, c in enumerate(harfler)}
    return harfler, im, np.array([im[c] for c in metin], dtype=np.int32)


def ileri_ara(m, imler):
    """İleri geçiş; geri yayılım için gereken ara değerleri saklar."""
    T = len(imler)
    ara = {"imler": imler, "T": T, "kat": []}
    x = m.gomme[imler].copy()
    bs = m.d // m.bas
    maske = np.tril(np.ones((T, T), np.float32))
    for k in m.kat:
        a = {"girdi": x.copy()}
        a["rms1"] = np.sqrt((x * x).mean(-1, keepdims=True) + 1e-5)
        h = x / a["rms1"] * k["n1"]
        a["h1"] = h.copy()
        q, kk, v = h @ k["wq"].T, h @ k["wk"].T, h @ k["wv"].T
        a["q"], a["k"], a["v"] = q, kk, v
        cik = np.zeros_like(q)
        a["p"] = []
        for b in range(m.bas):
            d0, d1 = b * bs, (b + 1) * bs
            s = (q[:, d0:d1] @ kk[:, d0:d1].T) / np.sqrt(bs)
            s = np.where(maske > 0, s, -1e9)
            p = softmax(s)
            a["p"].append(p)
            cik[:, d0:d1] = p @ v[:, d0:d1]
        a["cik"] = cik.copy()
        x = x + cik @ k["wo"].T
        a["orta"] = x.copy()
        a["rms2"] = np.sqrt((x * x).mean(-1, keepdims=True) + 1e-5)
        h2 = x / a["rms2"] * k["n2"]
        a["h2"] = h2.copy()
        g1, g3 = h2 @ k["w1"].T, h2 @ k["w3"].T
        a["g1"], a["g3"] = g1, g3
        a["sg"] = silu(g1)
        x = x + (a["sg"] * g3) @ k["w2"].T
        ara["kat"].append(a)
    ara["son_girdi"] = x.copy()
    ara["rmsS"] = np.sqrt((x * x).mean(-1, keepdims=True) + 1e-5)
    ara["hS"] = x / ara["rmsS"] * m.son_norm
    ara["mantik"] = ara["hS"] @ m.cikis.T
    return ara


def geri(m, ara, hedef):
    """Elle geri yayılım; çapraz entropi kaybı."""
    T = ara["T"]
    p = softmax(ara["mantik"])
    kayip = -np.log(p[np.arange(T), hedef] + 1e-9).mean()
    dl = p.copy()
    dl[np.arange(T), hedef] -= 1.0
    dl /= T

    g = {"gomme": np.zeros_like(m.gomme),
         "son_norm": np.zeros_like(m.son_norm),
         "cikis": np.zeros_like(m.cikis)}
    gk = [{a: np.zeros_like(v) for a, v in kk.items()} for kk in m.kat]

    g["cikis"] += dl.T @ ara["hS"]
    dh = dl @ m.cikis
    g["son_norm"] += (dh * (ara["son_girdi"] / ara["rmsS"])).sum(0)
    dn = dh * m.son_norm
    x, r = ara["son_girdi"], ara["rmsS"]
    dx = dn / r - (x / (r ** 3)) * ((dn * x).mean(-1, keepdims=True))

    bs = m.d // m.bas
    for i in range(m.katman - 1, -1, -1):
        k, a, gg = m.kat[i], ara["kat"][i], gk[i]
        gg["w2"] += dx.T @ (a["sg"] * a["g3"])
        dsg3 = dx @ k["w2"]
        dsg, dg3 = dsg3 * a["g3"], dsg3 * a["sg"]
        s = 1.0 / (1.0 + np.exp(-a["g1"]))
        dg1 = dsg * (s * (1 + a["g1"] * (1 - s)))
        gg["w1"] += dg1.T @ a["h2"]
        gg["w3"] += dg3.T @ a["h2"]
        dh2 = dg1 @ k["w1"] + dg3 @ k["w3"]
        gg["n2"] += (dh2 * (a["orta"] / a["rms2"])).sum(0)
        dn2 = dh2 * k["n2"]
        xo, ro = a["orta"], a["rms2"]
        dx = dx + dn2 / ro - (xo / (ro ** 3)) * ((dn2 * xo).mean(-1, keepdims=True))

        dcik = dx @ k["wo"]
        gg["wo"] += dx.T @ a["cik"]
        dq = np.zeros_like(a["q"]); dkk = np.zeros_like(a["k"]); dv = np.zeros_like(a["v"])
        for b in range(m.bas):
            d0, d1 = b * bs, (b + 1) * bs
            pb = a["p"][b]
            dv[:, d0:d1] += pb.T @ dcik[:, d0:d1]
            dp = dcik[:, d0:d1] @ a["v"][:, d0:d1].T
            ds = pb * (dp - (dp * pb).sum(-1, keepdims=True)) / np.sqrt(bs)
            dq[:, d0:d1] += ds @ a["k"][:, d0:d1]
            dkk[:, d0:d1] += ds.T @ a["q"][:, d0:d1]
        gg["wq"] += dq.T @ a["h1"]
        gg["wk"] += dkk.T @ a["h1"]
        gg["wv"] += dv.T @ a["h1"]
        dh1 = dq @ k["wq"] + dkk @ k["wk"] + dv @ k["wv"]
        gg["n1"] += (dh1 * (a["girdi"] / a["rms1"])).sum(0)
        dn1 = dh1 * k["n1"]
        xi, ri = a["girdi"], a["rms1"]
        dx = dx + dn1 / ri - (xi / (ri ** 3)) * ((dn1 * xi).mean(-1, keepdims=True))

    np.add.at(g["gomme"], ara["imler"], dx)
    return kayip, g, gk


def egit(adim=6000, baglam=64, oran=3e-3):
    harfler, im, dizi = veri_hazirla(METIN)
    m = Kucuk(sozluk=len(harfler), baglam=baglam)
    par = m.parametreler()
    mu = [np.zeros_like(p) for p in par]
    vu = [np.zeros_like(p) for p in par]
    r = np.random.RandomState(1)
    for t in range(1, adim + 1):
        i = r.randint(0, len(dizi) - baglam - 1)
        ara = ileri_ara(m, dizi[i:i + baglam])
        kayip, g, gk = geri(m, ara, dizi[i + 1:i + baglam + 1])
        grad = [g["gomme"]]
        for j in range(m.katman):
            grad += [gk[j][a] for a in ("n1", "wq", "wk", "wv", "wo",
                                        "n2", "w1", "w2", "w3")]
        grad += [g["son_norm"], g["cikis"]]
        for p, gr, mm, vv in zip(par, grad, mu, vu):
            np.clip(gr, -1.0, 1.0, out=gr)
            mm *= 0.9; mm += 0.1 * gr
            vv *= 0.999; vv += 0.001 * gr * gr
            p -= oran * (mm / (1 - 0.9 ** t)) / (np.sqrt(vv / (1 - 0.999 ** t)) + 1e-8)
        if t % 500 == 0:
            print(f"  adim {t:5d}  kayip {kayip:.4f}", flush=True)
    return m, harfler


if __name__ == "__main__":
    adim = int(sys.argv[1]) if len(sys.argv) > 1 else 6000
    m, harfler = egit(adim)
    kok = "/home/claude/us1"
    boy = model_yaz(m, kok + "/umay.us1")
    open(kok + "/sozluk.txt", "w").write("".join(harfler))
    print("model:", boy, "bayt, sozluk:", len(harfler))
