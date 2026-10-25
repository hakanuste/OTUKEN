#!/usr/bin/env python3
"""README'deki UMAY ölçüleri — KODDAN ve SINAMALARDAN üretilir, elle yazılmaz.

  python3 tests/belge/umay_olculer.py yaz      # README'deki işaretli bölümü yeniler
  python3 tests/belge/umay_olculer.py denetle  # README bölümü güncel değilse çıkış 1

Kaynaklar (hepsi depodan okunur ya da koşturulur):
  - us1/umay.us1 başlığı          -> boyutlar, parametre sayısı, dosya boyu
  - us1/sozluk.txt                -> sözlük (bayt)
  - us1/c/us1.h                   -> motor sınırları
  - umay/core/umay_bilgi.c        -> doktrin kayıt sayısı
  - us1/arac/derlem.py            -> eğitim derlemi (dağıtım ayrımı) boyu
  - umay/include/umay_kanit.h     -> kanıt eşikleri
  - make -C umay sina             -> UMAY mantık sınaması sayısı (koşturulur)
  - us1_sina                      -> US1 motor sınaması sayısı (koşturulur)
  - kanitlar/us1/olcum.json       -> dağıtılan modelin sınama kümesi ölçümü (us1/arac/olc.py)
  - tests/umay_kanit/son_olcum.json -> kanıt katmanı ölçümü (varsa)
"""
import os, re, sys, json, struct, subprocess
KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
BAS, SON = "<!-- UMAY-OLCULER-BASLA (tests/belge/umay_olculer.py yazar; elle düzenleme) -->", \
           "<!-- UMAY-OLCULER-BITIS -->"


def oku(y, kip="r"):
    return open(os.path.join(KOK, y), kip, **({} if "b" in kip else {"encoding": "utf-8"})).read()


def model():
    b = oku("us1/umay.us1", "rb")
    assert b[:4] == b"US1\0"
    surum, d, k, h, f, V, T = struct.unpack("<7I", b[4:32])
    p = V * d + k * (2 * d + 4 * d * d + 2 * f * d + d * f) + d + V * d
    return dict(surum=surum, d=d, katman=k, bas=h, ffn=f, sozluk=V, baglam=T, param=p, boy=len(b))


def sinirlar():
    h = oku("us1/c/us1.h")
    return {a: int(re.search(rf"#define US1_AZAMI_{a}\s+(\d+)", h).group(1))
            for a in ("D", "KATMAN", "FFN", "SOZLUK", "BAGLAM")}


def kanit_esikleri():
    h = oku("umay/include/umay_kanit.h")
    return {a: int(re.search(rf"#define KANIT_{a}\s+(\d+)", h).group(1))
            for a in ("KAPSAMA_ESIK", "BAGLILIK_ESIK", "YUKLEM_AGIRLIK")}


def doktrin_sayi():
    sys.path.insert(0, os.path.join(KOK, "us1", "arac"))
    import derlem
    d = derlem.derlem_kur()
    bayt = sum(len(x) for g in ("egit_duz", "egit_cift") for x in d[g])
    return len(derlem.doktrin_oku()), bayt, len(d["egit_cift"]), len(d["sinama_cift"])


def sina_sayilari():
    u = subprocess.run(["make", "-s", "-C", os.path.join(KOK, "umay"), "sina"], capture_output=True, text=True)
    m = re.search(r"SONUC: (\d+) gecti, (\d+) kaldi", u.stdout)
    us1 = os.path.join(KOK, "us1")
    subprocess.run(["gcc", "-std=c11", "-O2", "-o", "us1_sina", "sinama/us1_sina.c", "c/us1.c"],
                   cwd=us1, check=True, capture_output=True)
    v = subprocess.run(["./us1_sina"], cwd=us1, capture_output=True, text=True)
    n = re.search(r"US1 SINAMA: (\d+) gecti, (\d+) kaldi", v.stdout)
    return (int(m.group(1)), int(m.group(2))), (int(n.group(1)), int(n.group(2)))


def binlik(n):
    return f"{n:,}".replace(",", ".")


def ondalik(x, basamak=2):
    return f"{x:.{basamak}f}".replace(".", ",")


def bolum():
    m, s, ke = model(), sinirlar(), kanit_esikleri()
    dk, bayt, cift, sin = doktrin_sayi()
    (ug, uk), (vg, vk) = sina_sayilari()
    subprocess.run(["make", "-s", "-C", os.path.join(KOK, "umay")], capture_output=True)   # ikili boyu için
    elf = os.path.join(KOK, "umay", "kov", "umay")
    elf_kb = os.path.getsize(elf) // 1024 if os.path.exists(elf) else None
    sat = [
        "**UMAY nedir:** ÖTÜKEN içinde Ring3'te çalışan **deneysel** bir sistem: küçük bir dil modeli "
        "(US1) + kalıcı hafıza (BENGİ) + yerel kütüphane araştırması + ajan meclisi (OTAĞ). "
        "Genel amaçlı bir dil modeli DEĞİLDİR; bildikleri yalnız kütüphanesindeki kayıtlardır.",
        "",
        "| Ölçü | Değer | Kaynak |",
        "|---|---|---|",
        f"| US1 modeli | d={m['d']}, {m['katman']} katman, {m['bas']} baş, ffn={m['ffn']}, bağlam {m['baglam']}, "
        f"**{binlik(m['param'])} parametre** ({m['boy'] // 1024} KB, biçim v{m['surum']}) | `us1/umay.us1` başlığı |",
        f"| Sözlük | {m['sozluk']} **bayt** (bayt düzeyi; harf/alt sözcük değil) | `us1/sozluk.txt` |",
        f"| Konum kodlaması | yok (tasarım: `docs/US1-V2-TASARIM.md`) | `us1/c/us1.c` |",
        f"| Motor sınırları | d ≤ {s['D']}, katman ≤ {s['KATMAN']}, ffn ≤ {s['FFN']}, sözlük ≤ {s['SOZLUK']}, "
        f"bağlam ≤ {s['BAGLAM']} | `us1/c/us1.h` |",
        f"| Eğitim derlemi | {bayt // 1024} KB ({cift} soru-yanıt çifti + düzyazı), {dk} doktrin kaydından | "
        f"`us1/arac/derlem.py` |",
        f"| Sabit sınama kümesi | {sin} soru; hiçbir yazım biçimi eğitimde yok | `us1/derlem/sinama_kumesi.tsv` |",
    ]
    oj = os.path.join(KOK, "kanitlar", "us1", "olcum.json")
    if os.path.exists(oj):
        o = json.load(open(oj, encoding="utf-8"))["sonuc"]
        sat.append(f"| Model, sınama kümesi | ilk cümle doğru %{o['sinama']['ilk_cumle'] * 100:.0f}, "
                   f"sözcük F1 {ondalik(o['sinama']['f1_ort'])}; alan dışında kendi başına ret "
                   f"%{o['alan_disi']['ret'] * 100:.0f} | `kanitlar/us1/olcum.json` (`us1/arac/olc.py`) |")
    kj = os.path.join(KOK, "tests", "umay_kanit", "son_olcum.json")
    if os.path.exists(kj):
        k = json.load(open(kj, encoding="utf-8"))
        t = k["sinama"]
        dis = min(v for a, v in t.items() if a.endswith("_ret"))
        sat.append(f"| Kanıt katmanı, sınama kümesi | alan içi doğru dayanakla yanıt %{t['ic_dogru'] * 100:.0f} "
                   f"(yüklemli soru %{t['ic_ey_dogru'] * 100:.0f}); alan dışı ret ≥ %{ondalik(dis * 100, 1)}; "
                   f"yönerge örnekleri {sum(1 for _, y in k['ornek'] if not y)}/{len(k['ornek'])} ret | "
                   f"`tests/umay_kanit` |")
    sat += [
        f"| Kanıt eşikleri | kapsama ≥ %{ke['KAPSAMA_ESIK']}, söz bağlılığı ≥ %{ke['BAGLILIK_ESIK']}, "
        f"yüklem ağırlığı {ke['YUKLEM_AGIRLIK']} | `umay/include/umay_kanit.h` |",
        f"| UMAY ikilisi | {elf_kb} KB | `umay/kov/umay` |" if elf_kb else "| UMAY ikilisi | (derlenmedi) | |",
        f"| UMAY mantık sınaması | {ug} geçti, {uk} kaldı | `make -C umay sina` |",
        f"| US1 motor sınaması | {vg} geçti, {vk} kaldı | `us1/us1_sina` |",
        "",
        "Model küçüktür ve genellemesi zayıftır: yanıtın doğruluğunu model değil **kanıt katmanı** korur. "
        "Soru kütüphanede yeterli kanıtla karşılanmıyorsa motor hiç çağrılmaz ve UMAY "
        "\"Bunu doğrulayacak yeterli bilgim yok.\" der; modelin sözü dayanağa bağlı değilse atılır ve "
        "kaynaktaki cümle aynen verilir (`docs/UMAY-KANIT.md`).",
    ]
    return BAS + "\n" + "\n".join(sat) + "\n" + SON


def main():
    kip = sys.argv[1] if len(sys.argv) > 1 else "denetle"
    r = oku("README.md")
    if BAS not in r or SON not in r:
        print("README'de işaretli bölüm yok"); sys.exit(2)
    i, j = r.index(BAS), r.index(SON) + len(SON)
    yeni = bolum()
    if kip == "yaz":
        open(os.path.join(KOK, "README.md"), "w", encoding="utf-8").write(r[:i] + yeni + r[j:])
        print(yeni)
    else:
        if r[i:j] != yeni:
            print("README UMAY ölçüleri GÜNCEL DEĞİL:\n--- README\n" + r[i:j] + "\n--- koddan\n" + yeni)
            sys.exit(1)
        print("README UMAY ölçüleri koddan/sınamalardan üretilenle aynı.")


if __name__ == "__main__":
    main()
