#!/usr/bin/env python3
"""US1 Python <-> C uyum sınaması (tek komut: ./calistir.sh).

Bölümler (her biri AYRI raporlanır):
  A  sözlük/kodlayıcı      C us1_kodla == Python eşlemesi (istemler + rastgele bayt dizileri)
  B  Q8 çözümü             C'nin satır ölçeği iç gösterimi vs dosyadaki f32 ölçek
  C  sabit nokta işlemleri carp/bol/kokk/ustel/silu/softmax vs float (ızgara)
  D  işlem düzeyi iz       her istemde, C ile Python AYNI geçmişi görürken, her
                           işlemin (gömme, RMSNorm, Q/K/V, skor, softmax, dikkat,
                           wo, SwiGLU, w2, artık, son norm, mantık) normlu göreli hatası
  E  açgözlü üretim        70 istem: aynı im dizisi mi; ayrışırsa İLK ayrışan im,
                           iki tarafın imi ve mantıkları, azami mutlak/göreli fark,
                           ilk eşiği aşan katman/işlem ve SINIFLAMA:
                             yakın-eşitlik : Python'da ilk iki aday arasındaki pay
                                             C'nin mantık hatasından küçük (sayısal)
                             HATA          : pay büyük ama C başka im seçti (gerçek hata)
  F  argmax / örnekleme    eşitlikte ilk en büyük; tohumlu örneklemenin dağılımı
  G  KV önbelleği          artımlı (us1_adim) == tam geçiş (us1_ileri), bit bit

Eşikler hata ÇÖZÜNÜRLÜĞÜNDEN türetilir (16.16'nın 1 birimi = 1.5e-5), sınamayı
geçirmek için gevşetilmez; değerleri ve gerekçeleri aşağıda yazılıdır.
"""
import ctypes, json, os, random, sys, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from izci import CMotor, Referans, uret, yerel_hatalar, KOK, BIR  # noqa: E402

KITAPLIK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/libus1iz.so"
MODEL = os.path.join(KOK, "us1", "umay.us1")
SOZLUK = os.path.join(KOK, "us1", "sozluk.txt")
ISTEM = os.path.join(os.path.dirname(os.path.abspath(__file__)), "istemler.txt")

# İşlem hatası eşiği (normlu göreli: max|c-p| / max|p|). 16.16 çözünürlüğü
# 1.5e-5; 4 katman boyunca birikim ve Q8 ölçeğinin iç gösterimi dahil,
# DOĞRU bir sabit nokta motorunda işlem başına 2e-3'ü aşmamalı.
ESIK_ISLEM = 2e-3

sonuc = {"bolum": {}, "gecti": 0, "kaldi": 0}


def dene(bolum, ad, kosul, ayrinti=""):
    sonuc["bolum"].setdefault(bolum, []).append({"ad": ad, "gecti": bool(kosul), "ayrinti": ayrinti})
    sonuc["gecti" if kosul else "kaldi"] += 1
    print(f"  [{'GECTI' if kosul else 'KALDI'}] {ad}" + (f"  ({ayrinti})" if ayrinti else ""))


def normlu(c, p):
    return float(np.abs(c - p).max() / max(np.abs(p).max(), 1e-9))


def main():
    t0 = time.time()
    C = CMotor(KITAPLIK, MODEL, SOZLUK)
    P = Referans(MODEL, SOZLUK)
    istemler = [x for x in open(ISTEM, encoding="utf-8").read().split("\n") if x]
    print(f"model: d={C.d} katman={C.katman} bas={C.bas} ffn={C.ffn} sozluk={C.V} baglam={C.baglam}; "
          f"istem={len(istemler)}")

    # ---------------- A ----------------
    print("\nA) sözlük / kodlayıcı")
    r = random.Random(1)
    diziler = [f"S: {s}\nU: ".encode() for s in istemler]
    diziler += [bytes(r.randrange(1, 256) for _ in range(r.randrange(1, 60))) for _ in range(500)]
    uymayan = sum(1 for b in diziler if C.kodla(b) != P.kodla(b))
    dene("A", "C us1_kodla == Python eşlemesi", uymayan == 0, f"{len(diziler)} dizi, uymayan {uymayan}")

    # ---------------- B ----------------
    print("\nB) Q8 çözümü (satır ölçeği)")
    import struct
    veri = C.veri
    ofs = 48; en_kotu = 0.0; en_kotu_ad = ""
    adlar = ["gomme"] + [f"k{L}.{a}" for L in range(C.katman)
                         for a in ("n1", "wq", "wk", "wv", "wo", "n2", "w1", "w2", "w3")] + ["son_norm", "cikis"]
    # C iç ölçeği: us1_iz_olcek32 us1_tensor_t ister; tensörü C tarafında
    # kurmak için satir/sutun/veri işaretçisini doğrudan veriyoruz.
    class Tensor(ctypes.Structure):
        _fields_ = [("satir", ctypes.c_uint32), ("sutun", ctypes.c_uint32), ("veri", ctypes.c_void_p)]
    C.lib.us1_iz_olcek32.restype = ctypes.c_int64
    taban = ctypes.addressof(C._tampon)
    for ad in adlar:
        satir, sutun = struct.unpack_from("<II", veri, ofs); ofs += 8
        T = Tensor(satir, sutun, taban + ofs)
        for i in range(satir):
            f = struct.unpack_from("<f", veri, ofs + i * (4 + sutun))[0]
            ic = C.lib.us1_iz_olcek32(ctypes.byref(T), i) / 2.0 ** 32
            g = abs(ic - f) / max(abs(f), 1e-30)
            if g > en_kotu: en_kotu, en_kotu_ad = g, f"{ad}[{i}]"
        ofs += satir * (4 + sutun)
    dene("B", "satır ölçeği iç gösterimi dosyadaki f32'ye yakın (göreli < 1e-4)", en_kotu < 1e-4,
         f"en kötü göreli {en_kotu:.2e} @ {en_kotu_ad}")
    sonuc["olcek_en_kotu"] = en_kotu

    # ---------------- C ----------------
    print("\nC) sabit nokta işlemleri")
    L = C.lib
    for fn in ("us1_iz_carp", "us1_iz_bol", "us1_iz_kokk", "us1_iz_ustel", "us1_iz_silu"):
        getattr(L, fn).restype = ctypes.c_int32
    # Girdi 16.16'ya nicelenir; KARŞILAŞTIRMA nicelenmiş girdiyle yapılır
    # (yoksa ölçülen, işlevin değil girdinin niceleme hatası olur).
    xq = [int(x * BIR) for x in np.linspace(-20, 20, 8001)]
    # ustel: 16.16'ya sığan aralıkta (e^x < 2^15) hata <= 2 birim + 5e-5 göreli
    e_ust = 0.0; doyma_ok = True
    for q in xq:
        x = q / BIR; c = L.us1_iz_ustel(q) / BIR
        if np.exp(x) < 32767.0:
            e_ust = max(e_ust, (abs(c - np.exp(x)) - 2.0 / BIR) / np.exp(x))
        elif L.us1_iz_ustel(q) != 0x7FFFFFFF:
            doyma_ok = False
    e_silu = max(abs(L.us1_iz_silu(q) / BIR - (q / BIR) / (1 + np.exp(-q / BIR))) for q in xq)
    kq = [int(k * BIR) for k in np.linspace(0.001, 30000, 6001)]
    e_kok = max(abs(L.us1_iz_kokk(q) / BIR - np.sqrt(q / BIR)) for q in kq)   # mutlak, birim cinsinden
    rr = np.random.RandomState(2)
    e_carp = max(abs(L.us1_iz_carp(int(a * BIR), int(b * BIR)) / BIR - (int(a * BIR) / BIR) * (int(b * BIR) / BIR))
                 for a, b in rr.uniform(-100, 100, (3000, 2)))
    e_bol = max(abs(L.us1_iz_bol(int(a * BIR), int(b * BIR)) / BIR - (int(a * BIR) / BIR) / (int(b * BIR) / BIR))
                for a, b in zip(rr.uniform(-100, 100, 3000), rr.uniform(0.5, 100, 3000)))
    e_sm = 0.0
    for _ in range(300):
        v = rr.uniform(-10, 10, rr.randint(1, 96))
        a = (ctypes.c_int32 * len(v))(*[int(x * BIR) for x in v])
        L.us1_iz_softmax(a, len(v))
        pv = np.exp(v - v.max()); pv /= pv.sum()
        e_sm = max(e_sm, float(np.abs(np.array(a[:]) / BIR - pv).max()))
    print(f"     ustel (birim-düzeltilmiş göreli) {e_ust:.2e} | silu mutlak {e_silu:.2e} | kokk mutlak {e_kok:.2e} | "
          f"carp mutlak {e_carp:.2e} | bol mutlak {e_bol:.2e} | softmax mutlak {e_sm:.2e}")
    sonuc["sabit_nokta"] = dict(ustel=e_ust, silu=e_silu, kokk=e_kok, carp=e_carp, bol=e_bol, softmax=e_sm)
    dene("C", "ustel: hata <= 2 birim + 5e-5 göreli (e^x < 2^15)", e_ust <= 5e-5, f"{e_ust:.2e}")
    dene("C", "ustel: 16.16 dışında DOYAR (taşma/UB yok)", doyma_ok)
    dene("C", "silu: mutlak hata < 3 birim (4.6e-5), [-20, 20]", e_silu < 4.6e-5, f"{e_silu:.2e}")
    dene("C", "kokk: mutlak hata <= 1 birim (1.5e-5)", e_kok <= 1.0 / BIR + 1e-12, f"{e_kok:.2e}")
    dene("C", "carp/bol mutlak < 2 birim (3e-5)", e_carp < 3e-5 and e_bol < 3e-5, f"{e_carp:.2e} / {e_bol:.2e}")
    dene("C", "softmax mutlak < 5e-4", e_sm < 5e-4, f"{e_sm:.2e}")

    # ---------------- D + E ----------------
    print("\nD+E) işlem düzeyi iz ve açgözlü üretim")
    ayni = 0; ayrismalar = []; op_hata = {}; yerel = {}
    for no, soru in enumerate(istemler):
        ct, ca = uret(C, soru, iz=True)
        for k, v in yerel_hatalar(uret.son_tum, P).items():
            if v[2] > yerel.get(k, (0, 0, 0))[2]: yerel[k] = v
        pt, pa = uret(P, soru, iz=True)
        n = min(len(ca), len(pa))
        ilk = next((i for i in range(n) if ca[i][0] != pa[i][0]), None)
        # işlem hatası: ortak geçmişli adımlarda (ilk ayrışmaya kadar)
        son_ortak = ilk if ilk is not None else n
        for i in range(son_ortak):
            cz, pz = ca[i][2], pa[i][2]
            for anahtar, cv in cz.items():
                if anahtar in pz:
                    op = anahtar[0]
                    op_hata[op] = max(op_hata.get(op, 0.0), normlu(cv, pz[anahtar]))
        if ct == pt and ilk is None:
            ayni += 1
            continue
        i = ilk if ilk is not None else n
        if i >= n:   # biri erken durdu: aynı imler, farklı uzunluk (olmamalı)
            ayrismalar.append({"no": no, "soru": soru, "sinif": "HATA", "neden": "uzunluk farkı"})
            continue
        cy, py = ca[i][1], pa[i][1]
        s = np.argsort(-py)
        pay = float(py[s[0]] - py[s[1]])
        hata = float(np.abs(cy - py).max())
        # ilk eşiği aşan işlem (yürütme sırasıyla)
        sira = ["gomme"] + [o for o in ("n1", "q", "k", "v", "skor", "p", "dikkat", "wo", "x_orta",
                                         "n2", "w1", "w3", "swiglu", "w2", "x_son")] + ["son_norm", "mantik"]
        ilk_op = None
        cz, pz = ca[i][2], pa[i][2]
        anahtarlar = sorted(cz.keys(), key=lambda k: (k[1] if k[1] >= 0 else (99 if k[0] in ("son_norm", "mantik") else -1),
                                                    sira.index(k[0]), k[2]))
        for k in anahtarlar:
            if k in pz and normlu(cz[k], pz[k]) > ESIK_ISLEM:
                ilk_op = f"katman {k[1]} / {k[0]}" + (f" (baş {k[2]})" if k[2] >= 0 else "") + \
                         f" hata {normlu(cz[k], pz[k]):.2e}"
                break
        c_im, p_im = ca[i][0], pa[i][0]
        sinif = "yakın-eşitlik" if (c_im == s[1] and pay < 2 * hata) else "HATA"
        kay = {"no": no, "soru": soru, "adim": i, "sinif": sinif,
               "python_im": chr(P.sozluk[p_im]) if P.sozluk[p_im] < 128 else f"0x{P.sozluk[p_im]:02x}",
               "c_im": chr(C.sozluk[c_im]) if C.sozluk[c_im] < 128 else f"0x{C.sozluk[c_im]:02x}",
               "python_ilk5": [(int(j), round(float(py[j]), 4)) for j in s[:5]],
               "c_ilk5": [(int(j), round(float(cy[j]), 4)) for j in np.argsort(-cy)[:5]],
               "pay": round(pay, 5), "mantik_azami_mutlak": round(hata, 5),
               "mantik_azami_goreli": round(normlu(cy, py), 6), "ilk_esik_asan": ilk_op or "(yok)"}
        ayrismalar.append(kay)
    print(f"     aynı üretim: {ayni}/{len(istemler)}")
    for a in ayrismalar:
        print(f"     #{a['no']} '{a['soru']}' adım {a.get('adim')}: Python '{a.get('python_im')}' C '{a.get('c_im')}' "
              f"pay {a.get('pay')} mantık hatası {a.get('mantik_azami_mutlak')} -> {a['sinif']}; "
              f"ilk eşik aşan: {a.get('ilk_esik_asan')}")
    print("     BİRİKMİŞ hata (işlem başına en büyük normlu göreli, ortak geçmişli tüm adımlar; bilgi):")
    for op in ["gomme", "n1", "q", "k", "v", "skor", "p", "dikkat", "wo", "x_orta", "n2", "w1", "w3",
               "swiglu", "w2", "x_son", "son_norm", "mantik"]:
        if op in op_hata:
            print(f"       {op:9s} {op_hata[op]:.2e}")
    print("     YEREL hata (işlem C'nin kendi girdileriyle yeniden hesaplandı; yalnız o işlemin kattığı):")
    print("       işlem     katman  azami_mutlak  birim   oran(<=1 geçer: 4 birim + 1e-4*|ref|)")
    sira_op = ["gomme", "n1", "q", "k", "v", "skor", "p", "dikkat", "wo", "x_orta", "n2", "w1", "w3",
               "swiglu", "w2", "x_son", "son_norm", "mantik"]
    ilk_asan = None
    for (op, Lk), (e, m, oran) in sorted(yerel.items(), key=lambda kv: (kv[0][1] if kv[0][1] >= 0 else 99, sira_op.index(kv[0][0]))):
        isaret = "  <-- SINIR ÜSTÜ" if oran > 1 else ""
        if oran > 1 and ilk_asan is None: ilk_asan = f"katman {Lk} / {op}"
        print(f"       {op:9s} {Lk:6d}  {e:.3e}  {e * BIR:6.2f}  {oran:.3f}{isaret}")
    sonuc.update(ayni=ayni, toplam=len(istemler), ayrismalar=ayrismalar, islem_hatasi=op_hata,
                 yerel_hata={f"{k[0]}@{k[1]}": v for k, v in yerel.items()})
    en_kotu_yerel = max(yerel.items(), key=lambda kv: kv[1][2])
    dene("D", "YEREL: her işlemin kendi kattığı hata <= 4 birim + 1e-4*|ref| (ilk aşan: "
         + (ilk_asan or "yok") + ")", ilk_asan is None,
         f"en kötü oran {en_kotu_yerel[1][2]:.3f} @ {en_kotu_yerel[0][0]}/katman {en_kotu_yerel[0][1]}")
    # Birikmiş hata ağın koşullanmasına da bağlıdır (uygulamanın tek başına
    # özelliği değil); gerilemeye karşı YALNIZ son çıktıda sınanır. Eşik
    # değişmedi: 2e-3 (düzeltme öncesi 2.96e-2 idi).
    dene("D", f"BİRİKMİŞ: mantık normlu göreli hatası < {ESIK_ISLEM:g}",
         op_hata.get("mantik", 1) < ESIK_ISLEM, f"{op_hata.get('mantik', 1):.2e}")
    hatali = [a for a in ayrismalar if a["sinif"] == "HATA"]
    dene("E", "açıklanamayan (HATA sınıfı) ayrışma yok", not hatali, f"{len(hatali)} HATA")
    dene("E", f"açgözlü üretim aynı: {ayni}/{len(istemler)}", ayni == len(istemler))

    # ---------------- F ----------------
    print("\nF) argmax / örnekleme")
    V = C.V
    esit = [0.5] * V; esit[7] = esit[3] = 2.0
    dene("F", "eşitlikte ilk en büyük (C == Python)", C.sec(esit) == Referans.sec(np.array(esit)) == 3)
    rs = np.random.RandomState(9)
    mant = rs.uniform(-3, 3, V)
    arr = (ctypes.c_int32 * V)(*[int(x * BIR) for x in mant])
    tohum = ctypes.c_uint32(12345)
    say = np.zeros(V)
    N = 100000
    for _ in range(N):
        say[C.lib.us1_sec(arr, V, ctypes.byref(tohum))] += 1
    beklenen = np.exp(mant - mant.max()); beklenen /= beklenen.sum()
    fark = float(np.abs(say / N - beklenen).max())
    # 100k çekilişte p(1-p)/N'nin 4 sigması: en çok ~0.0063
    sinir = 4 * np.sqrt((beklenen * (1 - beklenen)).max() / N)
    dene("F", "tohumlu örnekleme dağılımı softmax'a uyuyor (4σ)", fark < sinir, f"azami fark {fark:.4f}, sınır {sinir:.4f}")
    t1 = ctypes.c_uint32(7); t2 = ctypes.c_uint32(7)
    a1 = [C.lib.us1_sec(arr, V, ctypes.byref(t1)) for _ in range(50)]
    a2 = [C.lib.us1_sec(arr, V, ctypes.byref(t2)) for _ in range(50)]
    dene("F", "aynı tohum aynı dizi", a1 == a2)

    # ---------------- G ----------------
    print("\nG) KV önbelleği")
    ids = C.kodla(("S: " + istemler[0] + "\nU: " + "x" * 200).encode())[: C.baglam]
    tam = (ctypes.c_int32 * (C.baglam * 128))()
    idarr = (ctypes.c_uint32 * len(ids))(*ids)
    C.lib.us1_ileri(C.model, idarr, len(ids), tam)
    C.sifirla(); esit_kv = True
    for t, im in enumerate(ids):
        y, _ = C.adim(im)
        if not np.array_equal(np.round(y * BIR).astype(np.int64),
                              np.array(tam[t * C.V:(t + 1) * C.V], np.int64)):
            esit_kv = False; break
    dene("G", f"artımlı == tam geçiş, bit bit ({len(ids)} konum)", esit_kv)

    # ---------------- H ----------------
    # Genişletilmiş örneklem: derlemden (eğitim + doğrulama soruları) 70'in
    # DIŞINDA belirlenimci 300 istem, iz yok. Ölçüt: HATA sınıfı ayrışma yok.
    print("\nH) genişletilmiş üretim (300 istem, iz yok)")
    sys.path.insert(0, os.path.join(KOK, "us1", "arac"))
    from derlem import derlem_kur
    d = derlem_kur()
    havuz = sorted({c.decode("utf-8").split("\n")[0][3:] for c in d["egit_cift"] + d["dog_cift"]} - set(istemler))
    ek = random.Random(11).sample(havuz, min(300, len(havuz)))
    ayni_h = 0; hata_h = 0; yakin_h = 0
    for soru in ek:
        ct, ca = uret(C, soru)
        pt, pa = uret(P, soru)
        if ct == pt and len(ca) == len(pa):
            ayni_h += 1; continue
        n = min(len(ca), len(pa))
        i = next((j for j in range(n) if ca[j][0] != pa[j][0]), None)
        if i is None: hata_h += 1; continue
        py, cy = pa[i][1], ca[i][1]; s2 = np.argsort(-py)
        if ca[i][0] == s2[1] and float(py[s2[0]] - py[s2[1]]) < 2 * float(np.abs(cy - py).max()):
            yakin_h += 1
        else:
            hata_h += 1
    print(f"     aynı {ayni_h}/{len(ek)}, yakın-eşitlik {yakin_h}, HATA {hata_h}")
    sonuc.update(genis_ayni=ayni_h, genis_toplam=len(ek), genis_yakin=yakin_h, genis_hata=hata_h)
    dene("H", "genişletilmiş örneklemde HATA sınıfı ayrışma yok", hata_h == 0,
         f"aynı {ayni_h}/{len(ek)}, yakın-eşitlik {yakin_h}")

    sonuc["sure_sn"] = round(time.time() - t0, 1)
    print(f"\n=== US1 UYUM: {sonuc['gecti']} gecti, {sonuc['kaldi']} kaldi — açgözlü üretim "
          f"{sonuc['ayni']}/{sonuc['toplam']} aynı ({sonuc['sure_sn']} sn) ===")
    cikti = os.environ.get("UYUM_JSON")
    if cikti:
        json.dump(sonuc, open(cikti, "w"), ensure_ascii=False, indent=1, default=float)
    return 0 if sonuc["kaldi"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
