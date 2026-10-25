#!/usr/bin/env python3
"""UMAY'ın US1 modelini eğitir.

Mimari C motoruyla (us1/c/us1.c) BİREBİR aynıdır: RMSNorm, nedensel çok
başlı dikkat, SwiGLU ileri besleme, konum kodlaması YOK. Geri yayılım
elle yazılmıştır (torch yok): üretilen ağırlıklar ÖTÜKEN'de tamsayı
aritmetiğiyle koşar. Geri yayılımın doğruluğu 'gradyan_denetimi' ile
sayısal türeve karşı sınanır.

Sözlük BAYT düzeyindedir (bkz. derlem.sozluk_kur).

Kullanım:
  python3 egit.py denetim                 # geri yayılımı sayısal türevle sına
  python3 egit.py secim [adim]            # SEÇİM koşusu: sızıntısız ayrım + erken durdurma
                                          #   (us1/kov/secim/: kayit.npz, en_iyi.npz, secim.json)
  python3 egit.py dagitim <adim> --plan <seçim boyu>   # DAĞITIM: sınama dışı her şey,
                                          #   seçimdeki öğrenme hızı yörüngesi, seçilen adımda dur
  python3 egit.py aktar <kayit.npz> <cikti.us1>   # Q8'e yaz (umay.us1'e DOKUNMAZ)
  python3 egit.py ornek "soru"            # Q8 dosyadan üret (C motorunun gördüğü)
"""
import os, sys, time, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from us1 import Kucuk, model_yaz, model_oku, ileri as ref_ileri
from derlem import derlem_kur, sozluk_kur

KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))  # us1/
KOV = os.path.join(KOK, "kov")

# --- Model boyu: C motoru sınırları içinde (US1_AZAMI_*: d 96, katman 4,
# ffn 256, sözlük 128, bağlam 96). ---
AYAR = dict(d=96, katman=4, bas=4, ffn=256, baglam=96)


# ============================ ileri / geri ============================
def ileri_ara(m, imler):
    """imler: (B,T). Geri yayılım için ara değerleri saklar."""
    B, T = imler.shape
    H, d = m.bas, m.d
    bs = d // H
    ara = {"imler": imler, "B": B, "T": T, "kat": []}
    x = m.gomme[imler]                                     # B,T,d
    ust = np.triu(np.ones((T, T), bool), 1)
    for k in m.kat:
        a = {"girdi": x}
        a["rms1"] = np.sqrt((x * x).mean(-1, keepdims=True) + 1e-5)
        h = x / a["rms1"] * k["n1"]
        a["h1"] = h
        q = (h @ k["wq"].T).reshape(B, T, H, bs).transpose(0, 2, 1, 3)
        kk = (h @ k["wk"].T).reshape(B, T, H, bs).transpose(0, 2, 1, 3)
        v = (h @ k["wv"].T).reshape(B, T, H, bs).transpose(0, 2, 1, 3)
        s = (q @ kk.transpose(0, 1, 3, 2)) / np.sqrt(bs)   # B,H,T,T
        s = np.where(ust, -1e9, s)
        s = s - s.max(-1, keepdims=True)
        p = np.exp(s); p /= p.sum(-1, keepdims=True)
        a["q"], a["k"], a["v"], a["p"] = q, kk, v, p
        cik = (p @ v).transpose(0, 2, 1, 3).reshape(B, T, d)
        a["cik"] = cik
        x = x + cik @ k["wo"].T
        a["orta"] = x
        a["rms2"] = np.sqrt((x * x).mean(-1, keepdims=True) + 1e-5)
        h2 = x / a["rms2"] * k["n2"]
        a["h2"] = h2
        g1, g3 = h2 @ k["w1"].T, h2 @ k["w3"].T
        sig = 1.0 / (1.0 + np.exp(-g1))
        a["g1"], a["g3"], a["sig"] = g1, g3, sig
        a["sg"] = g1 * sig
        x = x + (a["sg"] * g3) @ k["w2"].T
        ara["kat"].append(a)
    ara["son_girdi"] = x
    ara["rmsS"] = np.sqrt((x * x).mean(-1, keepdims=True) + 1e-5)
    ara["hS"] = x / ara["rmsS"] * m.son_norm
    ara["mantik"] = ara["hS"] @ m.cikis.T                  # B,T,V
    return ara


def _w(dy, x):
    """Ağırlık gradyanı: sum_bt dy[bt,:]^T x[bt,:]"""
    return dy.reshape(-1, dy.shape[-1]).T @ x.reshape(-1, x.shape[-1])


def _rms_geri(dn, x, r):
    return dn / r - (x / (r ** 3)) * ((dn * x).mean(-1, keepdims=True))


def geri(m, ara, hedef, agirlik=None):
    """Çapraz entropi (ortalama). agirlik: (B,T) konum ağırlığı ya da None."""
    B, T = ara["B"], ara["T"]
    H, d = m.bas, m.d
    bs = d // H
    z = ara["mantik"]
    z = z - z.max(-1, keepdims=True)
    p = np.exp(z); p /= p.sum(-1, keepdims=True)
    bi, ti = np.meshgrid(np.arange(B), np.arange(T), indexing="ij")
    lp = -np.log(p[bi, ti, hedef] + 1e-12)
    w = np.ones((B, T), p.dtype) if agirlik is None else agirlik.astype(p.dtype)
    wt = w.sum()
    kayip = float((lp * w).sum() / wt)
    dl = p.copy()
    dl[bi, ti, hedef] -= 1.0
    dl *= (w / wt)[..., None]

    g = {"gomme": np.zeros_like(m.gomme)}
    gk = [dict() for _ in m.kat]
    g["cikis"] = _w(dl, ara["hS"])
    dh = dl @ m.cikis
    g["son_norm"] = (dh * (ara["son_girdi"] / ara["rmsS"])).reshape(-1, d).sum(0)
    dx = _rms_geri(dh * m.son_norm, ara["son_girdi"], ara["rmsS"])

    for i in range(m.katman - 1, -1, -1):
        k, a, gg = m.kat[i], ara["kat"][i], gk[i]
        u = a["sg"] * a["g3"]
        gg["w2"] = _w(dx, u)
        du = dx @ k["w2"]
        dsg, dg3 = du * a["g3"], du * a["sg"]
        s = a["sig"]
        dg1 = dsg * (s * (1 + a["g1"] * (1 - s)))
        gg["w1"] = _w(dg1, a["h2"])
        gg["w3"] = _w(dg3, a["h2"])
        dh2 = dg1 @ k["w1"] + dg3 @ k["w3"]
        gg["n2"] = (dh2 * (a["orta"] / a["rms2"])).reshape(-1, d).sum(0)
        dx = dx + _rms_geri(dh2 * k["n2"], a["orta"], a["rms2"])

        gg["wo"] = _w(dx, a["cik"])
        dcik = (dx @ k["wo"]).reshape(B, T, H, bs).transpose(0, 2, 1, 3)
        pb = a["p"]
        dv = pb.transpose(0, 1, 3, 2) @ dcik
        dp = dcik @ a["v"].transpose(0, 1, 3, 2)
        ds = pb * (dp - (dp * pb).sum(-1, keepdims=True)) / np.sqrt(bs)
        dq = ds @ a["k"]
        dkk = ds.transpose(0, 1, 3, 2) @ a["q"]
        birles = lambda t: t.transpose(0, 2, 1, 3).reshape(B, T, d)
        dq, dkk, dv = birles(dq), birles(dkk), birles(dv)
        gg["wq"] = _w(dq, a["h1"])
        gg["wk"] = _w(dkk, a["h1"])
        gg["wv"] = _w(dv, a["h1"])
        dh1 = dq @ k["wq"] + dkk @ k["wk"] + dv @ k["wv"]
        gg["n1"] = (dh1 * (a["girdi"] / a["rms1"])).reshape(-1, d).sum(0)
        dx = dx + _rms_geri(dh1 * k["n1"], a["girdi"], a["rms1"])

    np.add.at(g["gomme"], ara["imler"].reshape(-1), dx.reshape(-1, d))
    return kayip, g, gk


ADLAR = ("n1", "wq", "wk", "wv", "wo", "n2", "w1", "w2", "w3")


def gradyan_listesi(m, g, gk):
    out = [g["gomme"]]
    for j in range(m.katman):
        out += [gk[j][a] for a in ADLAR]
    return out + [g["son_norm"], g["cikis"]]


# ============================ gradyan denetimi ============================
def gradyan_denetimi():
    """Elle yazılmış geri yayılımı merkezi farkla sayısal türeve karşı sınar
    (float64, küçük model). Göreli hata < 1e-5 beklenir."""
    r = np.random.RandomState(3)
    m = Kucuk(sozluk=7, d=8, katman=2, bas=2, ffn=12, baglam=6, tohum=5)
    for p in m.parametreler():
        p[...] = p.astype(np.float64)
    m.gomme = m.gomme.astype(np.float64); m.cikis = m.cikis.astype(np.float64)
    m.son_norm = m.son_norm.astype(np.float64) + r.randn(8) * 0.1
    for k in m.kat:
        for a in ADLAR:
            k[a] = k[a].astype(np.float64)
        k["n1"] += r.randn(8) * 0.1; k["n2"] += r.randn(8) * 0.1
    imler = r.randint(0, 7, (2, 5)); hedef = r.randint(0, 7, (2, 5))
    agirlik = r.rand(2, 5) + 0.5
    _, g, gk = geri(m, ileri_ara(m, imler), hedef, agirlik)
    analitik = gradyan_listesi(m, g, gk)
    en_kotu = 0.0
    for p, ga in zip(m.parametreler(), analitik):
        duz = p.reshape(-1); gd = ga.reshape(-1)
        for idx in r.choice(duz.size, min(6, duz.size), replace=False):
            eski = duz[idx]; e = 1e-6
            duz[idx] = eski + e; l1 = geri(m, ileri_ara(m, imler), hedef, agirlik)[0]
            duz[idx] = eski - e; l2 = geri(m, ileri_ara(m, imler), hedef, agirlik)[0]
            duz[idx] = eski
            sayisal = (l1 - l2) / (2 * e)
            hata = abs(sayisal - gd[idx]) / max(1e-8, abs(sayisal) + abs(gd[idx]))
            en_kotu = max(en_kotu, hata)
    # ileri geçiş referansla aynı mı (tek dizi)
    y1 = ileri_ara(m, imler[:1])["mantik"][0]
    y2 = ref_ileri(m, list(imler[0]))
    ileri_fark = float(np.abs(y1 - y2).max())
    return en_kotu, ileri_fark


# ============================ veri ============================
class Veri:
    def __init__(self, sozluk, d, baglam, tohum=1):
        self.sozluk = sozluk
        self.im = np.full(256, -1, np.int64)
        for i, b in enumerate(sozluk):
            self.im[b] = i
        self.T = baglam
        self.r = np.random.RandomState(tohum)
        kod = lambda b: self.im[np.frombuffer(b, np.uint8)]
        self.duz = kod(b"\n".join(d["egit_duz"]) + b"\n")
        self.cift = [kod(c) for c in d["egit_cift"]]
        self.dog_duz = kod(b"\n".join(d["dog_duz"]) + b"\n") if d["dog_duz"] else None
        self.dog_cift = [kod(c) for c in d["dog_cift"]]
        yb = self.im[ord("U")]
        self.u_im = yb

    def _cift_akisi(self, n, liste):
        """Çiftleri rastgele sırada uç uca ekler, n+1 im döndürür; baştaki
        çift soru satırıyla başlar."""
        parca, top = [], 0
        while top < n + 1:
            c = liste[self.r.randint(len(liste))]
            parca.append(c); top += len(c)
        return np.concatenate(parca)[: n + 1]

    def yigin(self, B, cift_orani=0.7):
        X = np.zeros((B, self.T), np.int64); Y = np.zeros((B, self.T), np.int64)
        W = np.ones((B, self.T), np.float32)
        for b in range(B):
            if self.r.rand() < cift_orani:
                s = self._cift_akisi(self.T, self.cift)
            else:
                i = self.r.randint(0, len(self.duz) - self.T - 1)
                s = self.duz[i:i + self.T + 1]
            X[b], Y[b] = s[:-1], s[1:]
        return X, Y, W

    def sabit_dogrulama_kur(self, d):
        """Belirlenimci doğrulama pencereleri (her değerlendirmede AYNI):
          cevap: her doğrulama çiftinin başından açılan pencere; kayıp YALNIZ
                 o çiftin yanıt baytlarında (+ yanıtı bitiren satır sonu):
                 soru bağlamda, yanıt tahmin ediliyor
          metin: ayrılmış cümlelerden (dog_duz) örtüşmesiz pencereler."""
        T = self.T
        self.pen_cevap = []
        if d.get("dog_cift"):
            akis, bolge = [], []
            for c in d["dog_cift"]:
                on = c.index(b"\nU: ") + len(b"\nU: ")
                b0 = len(akis)
                bolge.append((b0, b0 + on, b0 + len(c) - 1))   # başlangıç, yanıt başı, yanıt sonu (\n)
                akis.extend(self.im[np.frombuffer(c, np.uint8)].tolist())
            akis = np.array(akis + akis[: T + 1])
            for b0, y0, y1 in bolge:
                s0 = akis[b0:b0 + T + 1]
                k = b0 + 1 + np.arange(T)                     # hedef konumları
                ww = ((k >= y0) & (k <= y1)).astype(np.float32)
                if ww.sum() > 0:
                    self.pen_cevap.append((s0[:-1], s0[1:], ww))
        self.pen_metin = []
        if self.dog_duz is not None:
            for i in range(0, len(self.dog_duz) - T - 1, T):
                s0 = self.dog_duz[i:i + T + 1]
                self.pen_metin.append((s0[:-1], s0[1:], np.ones(T, np.float32)))

    def sabit_dogrulama(self, m, B=32):
        sonuc = {}
        for ad, pen in (("cevap", self.pen_cevap), ("metin", self.pen_metin)):
            if not pen:
                continue
            top = ag = 0.0
            for i in range(0, len(pen), B):
                X = np.stack([p[0] for p in pen[i:i + B]]); Y = np.stack([p[1] for p in pen[i:i + B]])
                W = np.stack([p[2] for p in pen[i:i + B]])
                k = geri(m, ileri_ara(m, X), Y, W)[0]
                top += k * W.sum(); ag += W.sum()
            sonuc[ad] = float(top / ag)
        return sonuc

    def dogrulama(self, m, B=16):
        """Sabit tohumlu doğrulama: çift kaybı ve düzyazı kaybı ayrı."""
        eski = self.r; self.r = np.random.RandomState(99)
        sonuc = {}
        if self.dog_cift:
            X = np.zeros((B, self.T), np.int64); Y = np.zeros((B, self.T), np.int64)
            for b in range(B):
                s = self._cift_akisi(self.T, self.dog_cift); X[b], Y[b] = s[:-1], s[1:]
            sonuc["cift"] = geri(m, ileri_ara(m, X), Y)[0]
        if self.dog_duz is not None and len(self.dog_duz) > self.T + 1:
            X = np.zeros((B, self.T), np.int64); Y = np.zeros((B, self.T), np.int64)
            for b in range(B):
                i = self.r.randint(0, len(self.dog_duz) - self.T - 1)
                s = self.dog_duz[i:i + self.T + 1]; X[b], Y[b] = s[:-1], s[1:]
            sonuc["duz"] = geri(m, ileri_ara(m, X), Y)[0]
        self.r = eski
        return sonuc


# ============================ eğitim ============================
def kaydet(m, opt, adim, gunluk, yol):
    np.savez(yol, adim=adim, **{f"p{i}": p for i, p in enumerate(m.parametreler())},
             **{f"mu{i}": x for i, x in enumerate(opt[0])},
             **{f"vu{i}": x for i, x in enumerate(opt[1])})
    json.dump(gunluk, open(yol + ".json", "w"))


def yukle(m, yol):
    z = np.load(yol)
    for i, p in enumerate(m.parametreler()):
        p[...] = z[f"p{i}"]
    mu = [z[f"mu{i}"] for i in range(len(m.parametreler()))]
    vu = [z[f"vu{i}"] for i in range(len(m.parametreler()))]
    gunluk = json.load(open(yol + ".json"))
    return int(z["adim"]), (mu, vu), gunluk


def dev_uretim(m, sozluk, dog_cift, kov):
    """Geliştirme sorularına ÖTÜKEN'deki yolla (Q8 dosya + C tamsayı motoru,
    açgözlü) yanıt üretir; beklenen yanıtla sözcük F1 ortalaması ve ilk cümle
    doğruluğu. Seçim ölçütü budur: kayıp değil, üretilen yanıtın niteliği."""
    import olc
    yol = os.path.join(kov, "_dev.us1"); syol = os.path.join(kov, "_dev.sozluk")
    model_yaz(m, yol)
    open(syol, "wb").write(sozluk)
    ciftler = [olc.ayir(c) for c in dog_cift]
    cik = olc.uret(yol, syol, [q for q, _ in ciftler])
    f = [olc.f1(c, y) for (_, y), c in zip(ciftler, cik)]
    ilk = [olc.ilk_cumle(c) == olc.ilk_cumle(y) for (_, y), c in zip(ciftler, cik)]
    return sum(f) / len(f), sum(ilk) / len(ilk)


def egit(toplam_adim=12000, B=16, devam=False, oran=2e-3, isinma=300, kip="secim",
         sabir=6, en_az_iyilesme=0.005, plan=None):
    """kip='secim'  : sınama + geliştirme + metin doğrulaması AYRI; erken durdurma
                     (geliştirme yanıt kaybı 'sabir' değerlendirme boyunca en az
                     'en_az_iyilesme' iyileşmezse durur); en iyi nokta ayrıca saklanır.
       kip='dagitim': yalnız sınama ayrı; verilen adım sayısı kadar (seçim koşusunun
                     en iyi noktası) eğitilir. plan: öğrenme hızı çizelgesinin boyu
                     (seçim koşusununki; verilmezse toplam_adim) — seçimdeki yörünge
                     aynen izlenir, en iyi adımda durulur.
       Seçim ölçütü: geliştirme sorularında ÜRETİLEN yanıtın F1'i (dev_uretim);
       yanıt kaybı yalnız kayda geçer.
       Eski 'egit' kipi = 'dagitim' ama eski (sızıntılı) derlemle DEĞİL."""
    kov = os.path.join(KOV, kip)
    os.makedirs(kov, exist_ok=True)
    d = derlem_kur(secim=(kip == "secim"))
    sozluk = sozluk_kur(d)
    open(os.path.join(kov, "sozluk.bin"), "wb").write(sozluk)
    veri = Veri(sozluk, d, AYAR["baglam"])
    veri.sabit_dogrulama_kur(d)
    print(f"derlem: eğitim çifti {len(d['egit_cift'])}, geliştirme çifti {len(d['dog_cift'])}, "
          f"metin doğrulaması {sum(len(b) for b in d['dog_duz'])} bayt, sınama çifti "
          f"{len(d['sinama_cift'])} (eğitime girmez); pencere: cevap {len(veri.pen_cevap)}, "
          f"metin {len(veri.pen_metin)}", flush=True)
    m = Kucuk(sozluk=len(sozluk), tohum=7, **AYAR)
    par = m.parametreler()
    opt = ([np.zeros_like(p) for p in par], [np.zeros_like(p) for p in par])
    bas_adim, gunluk = 0, []
    yol = os.path.join(kov, "kayit.npz")
    en_iyi_yol = os.path.join(kov, "en_iyi.npz")
    en_iyi, en_iyi_adim, bekleme = None, 0, 0
    if devam and os.path.exists(yol):
        bas_adim, opt, gunluk = yukle(m, yol)
        print(f"devam: adim {bas_adim}", flush=True)
    mu, vu = opt
    nparam = sum(p.size for p in par)
    print(f"model: d={m.d} katman={m.katman} bas={m.bas} ffn={m.ffn} baglam={m.baglam} "
          f"sozluk={m.sozluk} parametre={nparam}", flush=True)
    t0 = time.time(); ort = None
    plan = plan or toplam_adim
    for t in range(bas_adim + 1, toplam_adim + 1):
        # isınma + kosinüs (çizelge boyu: plan)
        if t <= isinma: lr = oran * t / isinma
        else:
            f = (t - isinma) / max(1, plan - isinma)
            lr = oran * (0.1 + 0.9 * 0.5 * (1 + np.cos(np.pi * f)))
        X, Y, W = veri.yigin(B)
        kayip, g, gk = geri(m, ileri_ara(m, X), Y, W)
        grad = gradyan_listesi(m, g, gk)
        norm = float(np.sqrt(sum(float((x * x).sum()) for x in grad)))
        olc = min(1.0, 1.0 / (norm + 1e-6))
        for j, (p, gr) in enumerate(zip(par, grad)):
            gr = gr * olc
            mu[j] *= 0.9; mu[j] += 0.1 * gr
            vu[j] *= 0.99; vu[j] += 0.01 * gr * gr
            mh = mu[j] / (1 - 0.9 ** t); vh = vu[j] / (1 - 0.99 ** t)
            p -= lr * mh / (np.sqrt(vh) + 1e-8)
            if p.ndim == 2 and j > 0:      # matrislere hafif ağırlık sönümü
                p -= lr * 0.01 * p
        ort = kayip if ort is None else 0.98 * ort + 0.02 * kayip
        if t % 100 == 0:
            gunluk.append({"adim": t, "kayip": round(ort, 4), "lr": lr, "norm": round(norm, 3)})
        if t % 500 == 0 or t == toplam_adim:
            dg = veri.sabit_dogrulama(m)
            if kip == "secim" and d["dog_cift"]:
                dg["f1"], dg["ilk"] = dev_uretim(m, sozluk, d["dog_cift"], kov)
            gunluk[-1].update({f"dog_{k}": round(v, 4) for k, v in dg.items()})
            hiz = (t - bas_adim) / (time.time() - t0)
            print(f"  adim {t:6d}  kayip {ort:.4f}  dog {json.dumps({k: round(v, 3) for k, v in dg.items()})}"
                  f"  lr {lr:.2e}  {hiz:.2f} adim/sn", flush=True)
            kaydet(m, (mu, vu), t, gunluk, yol)
            if kip == "secim" and "f1" in dg:
                if en_iyi is None or dg["f1"] > en_iyi + en_az_iyilesme:
                    en_iyi, en_iyi_adim, bekleme = dg["f1"], t, 0
                    kaydet(m, (mu, vu), t, gunluk, en_iyi_yol)
                else:
                    bekleme += 1
                gunluk[-1].update({"en_iyi_adim": en_iyi_adim, "en_iyi_f1": round(en_iyi, 4)})
                if bekleme >= sabir:
                    print(f"  ERKEN DURDURMA: {sabir} değerlendirmedir iyileşme yok; en iyi adım "
                          f"{en_iyi_adim} (geliştirme üretim F1 {en_iyi:.4f})", flush=True)
                    json.dump(gunluk, open(yol + ".json", "w"))
                    break
    if kip == "secim":
        json.dump({"en_iyi_adim": en_iyi_adim, "en_iyi_f1": en_iyi, "plan": plan},
                  open(os.path.join(kov, "secim.json"), "w"))
    return m, sozluk


def aktar(kayit, cikti):
    """Kayıt noktasını ÖTÜKEN biçimine (Q8) yazar. umay.us1'in ÜSTÜNE
    yazılmaz: karşılaştırma (olc.py karsilastir) yeni modeli üstün
    bulmadıkça dağıtılmaz."""
    sozluk = open(os.path.join(os.path.dirname(kayit), "sozluk.bin"), "rb").read()
    m = Kucuk(sozluk=len(sozluk), tohum=7, **AYAR)
    yukle(m, kayit)
    boy = model_yaz(m, cikti)
    open(os.path.splitext(cikti)[0] + ".sozluk", "wb").write(sozluk)
    return boy, len(sozluk)


def ornek(soru, azami=160, dosya=None):
    """Q8 dosyadan (C motorunun gördüğü ağırlıklar), açgözlü üretim."""
    m = model_oku(dosya or os.path.join(KOK, "umay.us1"))
    sozluk = open(os.path.join(KOK, "sozluk.txt"), "rb").read()
    im = {b: i for i, b in enumerate(sozluk)}
    bas = f"S: {soru}\nU: ".encode("utf-8")
    imler = [im[b] for b in bas if b in im]
    cikti = bytearray()
    for _ in range(azami):
        pencere = imler[-m.baglam:]
        y = ref_ileri(m, pencere)[-1]
        i = int(np.argmax(y))
        if sozluk[i] == ord("\n"):
            break
        cikti.append(sozluk[i]); imler.append(i)
    return cikti.decode("utf-8", "replace")


if __name__ == "__main__":
    kip = sys.argv[1] if len(sys.argv) > 1 else "egit"
    if kip == "denetim":
        hata, fark = gradyan_denetimi()
        print(f"gradyan denetimi: en kötü göreli hata {hata:.2e}; ileri geçiş referans farkı {fark:.2e}")
        sys.exit(0 if hata < 1e-5 and fark < 1e-9 else 1)
    elif kip in ("secim", "dagitim"):
        adim = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 16000
        plan = None
        if "--plan" in sys.argv:
            plan = int(sys.argv[sys.argv.index("--plan") + 1])
        egit(adim, devam="--devam" in sys.argv, kip=kip, plan=plan)
    elif kip == "aktar":
        boy, n = aktar(sys.argv[2], sys.argv[3])
        print("model:", boy, "bayt, sozluk:", n)
    elif kip == "ornek":
        for s in sys.argv[2:]:
            print(f"S: {s}\nU: {ornek(s)}\n")
