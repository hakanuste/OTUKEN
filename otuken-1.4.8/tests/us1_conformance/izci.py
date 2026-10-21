#!/usr/bin/env python3
"""US1 uyum sınaması — ortak parçalar.

  CMotor   : us1/c/us1.c'nin -DUS1_IZ ile derlenmiş paylaşımlı kitaplığı
             (ctypes). ÖTÜKEN'deki UMAY'ın koştuğu kodun AYNISI; yalnız iz
             kancası açık. Her işlemin 16.16 çıktısını yakalar.
  Referans : aynı Q8 dosyadan (niceleme kaybı İKİ tarafta da aynı) float64
             ileri geçiş; C ile AYNI işlem noktalarında iz üretir.
  uret()   : UMAY us1-yerel üretecinin kuralları (istem kalıbı, açgözlü seçim,
             satır sonu / cümle sonu durma, bağlam dolunca son yarıyla
             yeniden doldurma) — iki taraf için ortak.
"""
import ctypes, os, sys
import numpy as np

KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(KOK, "us1", "arac"))
from us1 import model_oku  # noqa: E402

BIR = 65536.0
IZ_FN = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint32,
                         ctypes.POINTER(ctypes.c_int32), ctypes.c_uint32)


class CMotor:
    """ÖTÜKEN C motoru (iz açık)."""

    def __init__(self, kitaplik, model_yolu, sozluk_yolu):
        self.lib = ctypes.CDLL(kitaplik)
        self.veri = open(model_yolu, "rb").read()
        self._tampon = ctypes.create_string_buffer(self.veri, len(self.veri))
        # us1_model_t'yi opak tampon olarak tut (boyu cömert)
        self.model = ctypes.create_string_buffer(8192)
        r = self.lib.us1_yukle(self.model, self._tampon, ctypes.c_uint32(len(self.veri)))
        if r != 0:
            raise RuntimeError(f"us1_yukle başarısız: {r}")
        hdr = np.frombuffer(self.veri, np.uint32, 7, 4)
        self.d, self.katman, self.bas, self.ffn, self.V, self.baglam = (int(x) for x in hdr[1:7])
        self.sozluk = open(sozluk_yolu, "rb").read()
        self._soz_c = ctypes.create_string_buffer(self.sozluk, len(self.sozluk))
        self.iz = None
        self._cb = IZ_FN(self._yakala)
        self.lib.us1_iz_kur(self._cb)
        self._satir = (ctypes.c_int32 * self.V)()

    def _yakala(self, islem, L, b, t, deger, n):
        if self.iz is not None:
            self.iz[(islem.decode(), L, b)] = np.ctypeslib.as_array(deger, (n,)).astype(np.float64) / BIR

    def kodla(self, metin_bayt):
        imler = (ctypes.c_uint32 * 4096)()
        n = self.lib.us1_kodla(self._soz_c, len(self.sozluk), self.V,
                               ctypes.c_char_p(metin_bayt), imler, 4096)
        return [imler[i] for i in range(n)]

    # --- artımlı arayüz (uret() kullanır) ---
    def sifirla(self):
        self.lib.us1_sifirla(self.model)

    def adim(self, im, iz=False):
        self.iz = {} if iz else None
        r = self.lib.us1_adim(self.model, ctypes.c_uint32(im), self._satir)
        if r != 0:
            raise RuntimeError(f"us1_adim: {r}")
        izler, self.iz = self.iz, None
        return np.array(self._satir[:], np.float64) / BIR, izler

    def sec(self, mantik):                 # C'nin kendi argmax'ı (us1_sec, tohum yok)
        arr = (ctypes.c_int32 * self.V)(*[int(round(x * BIR)) for x in mantik])
        return self.lib.us1_sec(arr, self.V, None)


def _rms(x, g, eps=1e-5):
    return x / np.sqrt((x * x).mean() + eps) * g


def _silu(x):
    return x / (1.0 + np.exp(-x))


class Referans:
    """Aynı Q8 dosyadan float64 artımlı ileri geçiş (KV önbellekli)."""

    def __init__(self, model_yolu, sozluk_yolu):
        m = model_oku(model_yolu)
        f = lambda a: np.asarray(a, np.float64)
        self.gomme, self.son_norm, self.cikis = f(m.gomme), f(m.son_norm), f(m.cikis)
        self.kat = [{k: f(v) for k, v in kk.items()} for kk in m.kat]
        self.d, self.bas, self.baglam, self.V, self.ffn = m.d, m.bas, m.baglam, m.sozluk, m.ffn
        self.katman = m.katman
        self.sozluk = open(sozluk_yolu, "rb").read()
        self.im = {}
        for i, b in enumerate(self.sozluk[: self.V]):
            self.im.setdefault(b, i)       # C de İLK eşleşmeyi alır
        self.sifirla()

    def kodla(self, metin_bayt):
        return [self.im[b] for b in metin_bayt if b in self.im]

    def sifirla(self):
        self.K = [[] for _ in self.kat]
        self.Vv = [[] for _ in self.kat]

    def adim(self, im, iz=False):
        Z = {} if iz else None
        d, bs = self.d, self.d // self.bas
        t = len(self.K[0])
        x = self.gomme[im].copy()
        if iz: Z[("gomme", -1, -1)] = x.copy()
        for L, k in enumerate(self.kat):
            h = _rms(x, k["n1"])
            q, kk, v = k["wq"] @ h, k["wk"] @ h, k["wv"] @ h
            self.K[L].append(kk); self.Vv[L].append(v)
            if iz:
                Z[("n1", L, -1)] = h; Z[("q", L, -1)] = q; Z[("k", L, -1)] = kk; Z[("v", L, -1)] = v
            Km, Vm = np.array(self.K[L]), np.array(self.Vv[L])
            cik = np.zeros(d)
            for b in range(self.bas):
                d0, d1 = b * bs, (b + 1) * bs
                s = Km[:, d0:d1] @ q[d0:d1] / np.sqrt(bs)
                e = np.exp(s - s.max()); p = e / e.sum()
                if iz: Z[("skor", L, b)] = s; Z[("p", L, b)] = p
                cik[d0:d1] = p @ Vm[:, d0:d1]
            wo = k["wo"] @ cik
            x = x + wo
            h2 = _rms(x, k["n2"])
            g1, g3 = k["w1"] @ h2, k["w3"] @ h2
            sw = _silu(g1) * g3
            w2 = k["w2"] @ sw
            if iz:
                Z[("dikkat", L, -1)] = cik; Z[("wo", L, -1)] = wo; Z[("x_orta", L, -1)] = x.copy()
                Z[("n2", L, -1)] = h2; Z[("w1", L, -1)] = g1; Z[("w3", L, -1)] = g3
                Z[("swiglu", L, -1)] = sw; Z[("w2", L, -1)] = w2
            x = x + w2
            if iz: Z[("x_son", L, -1)] = x.copy()
        hs = _rms(x, self.son_norm)
        y = self.cikis @ hs
        if iz: Z[("son_norm", -1, -1)] = hs; Z[("mantik", -1, -1)] = y
        return y, Z

    @staticmethod
    def sec(mantik):                       # ilk en büyük (C ile aynı eşitlik kuralı)
        return int(np.argmax(mantik))


def uret(motor, soru, azami=160, iz=False):
    """UMAY us1-yerel kuralları. Döner: (baytlar, adımlar[(im, mantik, iz)])."""
    istem = f"S: {soru}\nU: ".encode("utf-8")
    ids = motor.kodla(istem)
    B = motor.baglam
    motor.sifirla()
    gecmis = list(ids)
    y, Z = None, None
    tum = []                                   # (konum, iz) — ön doldurma dahil
    for i in ids[:B]:
        y, Z = motor.adim(i, iz)
        if iz: tum.append(Z)
    cikti = bytearray()
    adimlar = []
    for _ in range(azami):
        im = motor.sec(y)
        adimlar.append((im, y, Z))
        c = motor.sozluk[im]
        if c == 10:
            break
        if c == 32 and len(cikti) >= 8 and cikti[-1:] == b".":
            break
        cikti.append(c)
        gecmis.append(im)
        konum = len(motor.K[0]) if isinstance(motor, Referans) else motor.lib.us1_konum()
        if konum >= B:
            k = B // 2
            motor.sifirla()
            for j in gecmis[-k:]:
                y, Z = motor.adim(j, iz)
                if iz: tum.append(Z)
        else:
            y, Z = motor.adim(im, iz)
            if iz: tum.append(Z)
    if iz:
        uret.son_tum = tum
    return bytes(cikti), adimlar



def yerel_hatalar(tum, P):
    """Her işlemi C'nin KENDİ girdileriyle float64'te yeniden hesaplar;
    yalnız o işlemin kattığı hatayı döndürür: {(op, katman): (azami_mutlak, azami|ref|)}.
    tum: C'nin art arda adım izleri (konum 0'da sıfırlanan diziler)."""
    out = {}
    def kaydet(op, L, c, ref):
        e = float(np.abs(c - ref).max()); m = float(np.abs(ref).max())
        k = (op, L)
        a = out.get(k, (0.0, 0.0, 0.0))
        # ölçüt oranı: e / (4 birim + 1e-4 m); en kötü oranı sakla
        oran = e / (4.0 / BIR + 1e-4 * m)
        if oran > a[2]: out[k] = (e, m, oran)
    bs = P.d // P.bas
    Kc = [[] for _ in P.kat]; Vc = [[] for _ in P.kat]
    for Z in tum:
        x_in = Z[("gomme", -1, -1)]
        for L, k in enumerate(P.kat):
            if ("n1", L, -1) not in Z:
                break
            n1 = Z[("n1", L, -1)]
            kaydet("n1", L, n1, _rms(x_in, k["n1"]))
            q, kk, v = Z[("q", L, -1)], Z[("k", L, -1)], Z[("v", L, -1)]
            kaydet("q", L, q, k["wq"] @ n1); kaydet("k", L, kk, k["wk"] @ n1); kaydet("v", L, v, k["wv"] @ n1)
            if Z[("skor", L, 0)].shape[0] == 1:          # konum 0: yeni dizi
                Kc[L] = []; Vc[L] = []
            Kc[L].append(kk); Vc[L].append(v)
            Km, Vm = np.array(Kc[L]), np.array(Vc[L])
            dk = np.zeros(P.d)
            for b in range(P.bas):
                d0, d1 = b * bs, (b + 1) * bs
                sk = Z[("skor", L, b)]; pp = Z[("p", L, b)]
                kaydet("skor", L, sk, Km[:, d0:d1] @ q[d0:d1] / np.sqrt(bs))
                e = np.exp(sk - sk.max()); pf = e / e.sum()
                kaydet("p", L, pp, pf)
                # C ağırlıklı toplamı izdeki 16.16 p ile DEĞİL, 2^-30'luk iç
                # olasılıkla yapar; doğru yerel girdi softmax'a giren skordur.
                dk[d0:d1] = pf @ Vm[:, d0:d1]
            dikkat = Z[("dikkat", L, -1)]
            kaydet("dikkat", L, dikkat, dk)
            wo = Z[("wo", L, -1)]; kaydet("wo", L, wo, k["wo"] @ dikkat)
            xo = Z[("x_orta", L, -1)]; kaydet("x_orta", L, xo, x_in + wo)
            n2 = Z[("n2", L, -1)]; kaydet("n2", L, n2, _rms(xo, k["n2"]))
            w1, w3 = Z[("w1", L, -1)], Z[("w3", L, -1)]
            kaydet("w1", L, w1, k["w1"] @ n2); kaydet("w3", L, w3, k["w3"] @ n2)
            sw = Z[("swiglu", L, -1)]; kaydet("swiglu", L, sw, _silu(w1) * w3)
            w2 = Z[("w2", L, -1)]; kaydet("w2", L, w2, k["w2"] @ sw)
            xs = Z[("x_son", L, -1)]; kaydet("x_son", L, xs, xo + w2)
            x_in = xs
        sn = Z[("son_norm", -1, -1)]; kaydet("son_norm", -1, sn, _rms(x_in, P.son_norm))
        kaydet("mantik", -1, Z[("mantik", -1, -1)], P.cikis @ sn)
    return out
