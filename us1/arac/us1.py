#!/usr/bin/env python3
"""US1 — UMAY çıkarım motorunun model biçimi ve referans uygulaması.

Neden kendi biçimimiz: GGUF okuyucu yazmak mümkün ama indirilebilir bir
model yok. US1, ÖTÜKEN'de TAMSAYI aritmetiğiyle okunacak kadar yalındır.

BİÇİM (küçük endian):
  sihir    4 bayt  "US1\\0"
  surum    u32     1
  d_model  u32
  katman   u32
  bas      u32     dikkat başlığı sayısı
  ffn      u32     ileri besleme ara genişliği
  sozluk   u32
  baglam   u32     azami dizi uzunluğu
  ayrilmis u32 x 4
  ağırlıklar: her tensör  u32 satir, u32 sutun,
              satır başına  f32 olcek + int8[sutun]
  sıra: gomme, katman başına (n1,wq,wk,wv,wo,n2,w1,w2,w3), son_norm, cikis

NİCELEME: satır başına simetrik Q8, olcek = max|w|/127. Tamsayı
çarpımıyla toplanıp tek ölçekle geri çevrilebilir.

BİÇİM v2 (1.4.7, RoPE): başlık aynı 48 bayt; surum=2, [32] bayrak (bit0=RoPE),
[36] rope_taban, [40..47] sıfır. bit0=1 ise çıkış katmanından SONRA:
  u32 satir=baglam, u32 sutun=bs/2,
  int32 cos_q30[baglam][bs/2], int32 sin_q30[baglam][bs/2]
Çizelge float64'te hesaplanıp Q30'a YUVARLANIR ve dosyaya yazılır; C motoru
da referans da eğitici de AYNI tamsayıları kullanır (C'de trigonometri yok).
RoPE yalnız q ve k'ye, izdüşümden hemen sonra uygulanır; v'ye uygulanmaz.
Çift kuralı: her başta (v[2i], v[2i+1]), açı t * taban^(-2i/bs).
"""
import numpy as np
import struct, os

SIHIR = b"US1\0"
Q30 = 1 << 30
ROPE_TABAN_VARSAYILAN = 10000


def rope_cizelge(baglam, bs, taban):
    """Q30 cos/sin çizelgesi (int64 dizi, baglam x bs/2). float64'te hesaplanır
    ve Q30'a yuvarlanır. C motoru bu tamsayıları DOSYADAN okur."""
    assert bs % 2 == 0, "RoPE için baş boyu çift olmalı"
    yarim = bs // 2
    i = np.arange(yarim, dtype=np.float64)
    theta = taban ** (-2.0 * i / bs)                       # yarim
    aci = np.arange(baglam, dtype=np.float64)[:, None] * theta[None, :]   # baglam x yarim
    cos_q = np.rint(np.cos(aci) * Q30).astype(np.int64)
    sin_q = np.rint(np.sin(aci) * Q30).astype(np.int64)
    return cos_q, sin_q


def rope_f(x, cos_q, sin_q, T):
    """float RoPE: x (..., T, bs) ya da (T, d)'nin başlara ayrılmış biçimi değil —
    bu yardımcı SON eksen bs olan (..., T, bs) dizisini döndürür; cos/sin Q30
    tamsayı çizelgesinden (float'a çevrilerek) alınır: eğitim ve referans C'nin
    gördüğü AYNI (yuvarlanmış) değerlerle çalışır."""
    c = cos_q[:T].astype(np.float64) / Q30                 # T, yarim
    s = sin_q[:T].astype(np.float64) / Q30
    x0, x1 = x[..., 0::2], x[..., 1::2]                    # (..., T, yarim)
    y = np.empty_like(x)
    y[..., 0::2] = x0 * c - x1 * s
    y[..., 1::2] = x0 * s + x1 * c
    return y


class Kucuk:
    """Eğitim için kayan noktalı model; dışa aktarımda Q8'e iner."""

    def __init__(self, sozluk, d=64, katman=2, bas=4, ffn=176, baglam=64, tohum=7,
                 rope_taban=None):
        r = np.random.RandomState(tohum)
        o = lambda *s: (r.randn(*s) * (1.0 / np.sqrt(s[-1]))).astype(np.float32)
        self.sozluk, self.d, self.katman, self.bas = sozluk, d, katman, bas
        self.ffn, self.baglam = ffn, baglam
        # v2 RoPE: None -> v1 (konum kodlaması yok). Çizelge Q30 tamsayıdır.
        self.rope_taban = rope_taban
        self.rope_cos = self.rope_sin = None
        if rope_taban:
            self.rope_cos, self.rope_sin = rope_cizelge(baglam, d // bas, rope_taban)
        self.gomme = o(sozluk, d)
        self.kat = []
        for _ in range(katman):
            self.kat.append({
                "n1": np.ones(d, np.float32),
                "wq": o(d, d), "wk": o(d, d), "wv": o(d, d), "wo": o(d, d),
                "n2": np.ones(d, np.float32),
                "w1": o(ffn, d), "w2": o(d, ffn), "w3": o(ffn, d),
            })
        self.son_norm = np.ones(d, np.float32)
        self.cikis = o(sozluk, d)

    def parametreler(self):
        p = [self.gomme]
        for k in self.kat:
            p += [k["n1"], k["wq"], k["wk"], k["wv"], k["wo"],
                  k["n2"], k["w1"], k["w2"], k["w3"]]
        return p + [self.son_norm, self.cikis]


def rmsnorm(x, g, eps=1e-5):
    return x * g / np.sqrt((x * x).mean(-1, keepdims=True) + eps)


def softmax(x, eksen=-1):
    x = x - x.max(eksen, keepdims=True)
    e = np.exp(x)
    return e / e.sum(eksen, keepdims=True)


def silu(x):
    return x / (1.0 + np.exp(-x))


def ileri(m, imler):
    """Referans ileri geçiş. C motoru bunun sayısal karşılığını üretmeli."""
    T = len(imler)
    x = m.gomme[imler]
    bs = m.d // m.bas
    maske = np.tril(np.ones((T, T), np.float32))
    for k in m.kat:
        h = rmsnorm(x, k["n1"])
        q, kk, v = h @ k["wq"].T, h @ k["wk"].T, h @ k["wv"].T
        cik = np.zeros_like(q)
        for b in range(m.bas):
            d0, d1 = b * bs, (b + 1) * bs
            qb, kb = q[:, d0:d1], kk[:, d0:d1]
            if getattr(m, "rope_cos", None) is not None:   # v2: q ve k döndürülür (v değil)
                qb = rope_f(qb, m.rope_cos, m.rope_sin, T)
                kb = rope_f(kb, m.rope_cos, m.rope_sin, T)
            s = (qb @ kb.T) / np.sqrt(bs)
            s = np.where(maske > 0, s, -1e9)
            cik[:, d0:d1] = softmax(s) @ v[:, d0:d1]
        x = x + cik @ k["wo"].T
        h = rmsnorm(x, k["n2"])
        x = x + (silu(h @ k["w1"].T) * (h @ k["w3"].T)) @ k["w2"].T
    return rmsnorm(x, m.son_norm) @ m.cikis.T


def q8(w):
    """Satır başına simetrik 8 bit niceleme."""
    w = np.atleast_2d(w).astype(np.float32)
    olcek = np.abs(w).max(1) / 127.0
    olcek[olcek == 0] = 1e-8
    q = np.clip(np.rint(w / olcek[:, None]), -127, 127).astype(np.int8)
    return q, olcek.astype(np.float32)


def tensor_yaz(f, w):
    q, olcek = q8(w)
    satir, sutun = q.shape
    f.write(struct.pack("<II", satir, sutun))
    for i in range(satir):
        f.write(struct.pack("<f", float(olcek[i])))
        f.write(q[i].tobytes())


def model_yaz(m, yol):
    """RoPE'suz model v1 olarak (1.4.6 ile BAYT BAYT aynı) yazılır; RoPE'lu
    model v2 + bayrak + Q30 çizelge olarak."""
    rope = bool(getattr(m, "rope_taban", None))
    with open(yol, "wb") as f:
        f.write(SIHIR)
        f.write(struct.pack("<IIIIIII", 2 if rope else 1, m.d, m.katman, m.bas,
                            m.ffn, m.sozluk, m.baglam))
        if rope:
            f.write(struct.pack("<IIII", 1, int(m.rope_taban), 0, 0))   # bayrak bit0, taban
        else:
            f.write(struct.pack("<IIII", 0, 0, 0, 0))
        tensor_yaz(f, m.gomme)
        for k in m.kat:
            for ad in ("n1", "wq", "wk", "wv", "wo", "n2", "w1", "w2", "w3"):
                tensor_yaz(f, k[ad])
        tensor_yaz(f, m.son_norm)
        tensor_yaz(f, m.cikis)
        if rope:
            cq, sq = m.rope_cos, m.rope_sin
            f.write(struct.pack("<II", cq.shape[0], cq.shape[1]))
            f.write(cq.astype("<i4").tobytes())
            f.write(sq.astype("<i4").tobytes())
    return os.path.getsize(yol)


def model_oku(yol):
    """Dosyadan GERİ AÇ: C motorunun gördüğü ağırlıkların aynısı.
    Karşılaştırma böylece niceleme kaybını iki tarafta da içerir."""
    d = open(yol, "rb").read()
    assert d[:4] == SIHIR
    (surum, dm, katman, bas, ffn, sozluk, baglam) = struct.unpack_from("<IIIIIII", d, 4)
    bayrak, taban = struct.unpack_from("<II", d, 32) if surum == 2 else (0, 0)
    ofs = [48]

    def tensor():
        satir, sutun = struct.unpack_from("<II", d, ofs[0]); ofs[0] += 8
        out = np.zeros((satir, sutun), np.float32)
        for i in range(satir):
            olc = struct.unpack_from("<f", d, ofs[0])[0]; ofs[0] += 4
            q = np.frombuffer(d, np.int8, sutun, ofs[0]); ofs[0] += sutun
            out[i] = q.astype(np.float32) * olc
        return out

    m = Kucuk(sozluk=sozluk, d=dm, katman=katman, bas=bas, ffn=ffn, baglam=baglam)
    m.gomme = tensor()
    for k in m.kat:
        for ad in ("n1", "wq", "wk", "wv", "wo", "n2", "w1", "w2", "w3"):
            t = tensor()
            k[ad] = t[0] if ad in ("n1", "n2") else t
    m.son_norm = tensor()[0]
    m.cikis = tensor()
    if surum == 2 and (bayrak & 1):
        satir, sutun = struct.unpack_from("<II", d, ofs[0]); ofs[0] += 8
        n = satir * sutun
        # DOSYADAKİ tamsayılar (C'nin okuduğu), yeniden hesap DEĞİL
        m.rope_cos = np.frombuffer(d, "<i4", n, ofs[0]).astype(np.int64).reshape(satir, sutun); ofs[0] += 4 * n
        m.rope_sin = np.frombuffer(d, "<i4", n, ofs[0]).astype(np.int64).reshape(satir, sutun); ofs[0] += 4 * n
        m.rope_taban = taban
    return m


if __name__ == "__main__":
    m = Kucuk(sozluk=96)
    print("model yazildi:", model_yaz(m, "/tmp/sinama.us1"), "bayt")
    print("cikti bicimi:", ileri(m, [3, 14, 15, 9, 26, 5]).shape)
