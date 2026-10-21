#!/usr/bin/env python3
"""US1 model ölçümü ve karşılaştırması (Ö7) — ÖTÜKEN'in gördüğü Q8 dosya,
ÖTÜKEN'deki C tamsayı motoruyla (us1_uret: KV önbellek, açgözlü, aynı durma
kuralı).

Kümeler:
  sinama    : SABİT sınama kümesi (us1/derlem/sinama_kumesi.tsv). Ne eski ne
              yeni model bu soruların HİÇBİR yazım biçimini eğitimde görmedi.
  egitim    : dağıtım derleminden 80 eğitim çifti (ezber düzeyi).
  alan_disi : derlem dışı sorular (sabit 10 + tests/umay_kanit/son_olcum.json
              varsa oradaki UD kaynaklı alan dışı sınama soruları).

Ölçütler:
  tam       : üretilen == beklenen
  ilk_cumle : ilk cümleler aynı
  f1_ort    : beklenenle sözcük F1 ortalaması (0..1)
  f1_06     : F1 >= 0.6 olan yanıt oranı
  bozuk     : üretilen sözcüklerden derlemde HİÇ geçmeyenlerin oranı
              (anlamsız/bozuk üretim ölçüsü)
  ret       : alan dışında "yeterli bilgi yok" diyen yanıt oranı
  ezber     : alan dışında bir eğitim yanıtını (F1 >= 0.6) söyleyen oran —
              "rastgele eğitim cümlesi çekme" ölçüsü (düşük iyi)

Kullanım:
  python3 olc.py <model.us1> <sozluk>
  python3 olc.py karsilastir <eski.us1> <eski_sozluk> <yeni.us1> <yeni_sozluk>
Karşılaştırma kuralı (yeni model YALNIZ bu durumda dağıtılır):
  sinama f1_ort artmalı, sinama ilk_cumle azalmamalı, bozuk en çok +0,01.
"""
import os, re, sys, json, random, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from derlem import derlem_kur, sinama_kumesi, norm, BILMIYORUM

KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
UST = os.path.normpath(os.path.join(KOK, ".."))
ALAN_DISI_SABIT = ["kuantum bilgisayar nedir", "basketbol kuralları anlat", "bitcoin fiyatı kaç",
                   "yarın yağmur yağacak mı", "en iyi telefon hangisi",
                   "roma imparatorluğu ne zaman yıkıldı", "pizza nasıl yapılır", "ay'a kim gitti",
                   "şarkı sözü yaz", "euro kaç lira"]


def motor():
    yol = os.path.join(KOK, "us1_uret")
    kaynak = [os.path.join(KOK, "sinama", "us1_uret.c"), os.path.join(KOK, "c", "us1.c")]
    if not os.path.exists(yol) or any(os.path.getmtime(k) > os.path.getmtime(yol) for k in kaynak):
        subprocess.run(["gcc", "-std=c11", "-O2", "-o", yol] + kaynak, check=True)
    return yol


def uret(model, sozluk, sorular):
    p = subprocess.run([motor(), model, sozluk], input=("\n".join(sorular) + "\n").encode("utf-8"),
                       capture_output=True, timeout=3600)
    if p.returncode != 0:
        raise SystemExit(p.stderr.decode())
    satir = p.stdout.split(b"\n")[: len(sorular)]
    return [x.decode("utf-8", "replace") for x in satir]


def ilk_cumle(t):
    return re.split(r"(?<=\.)\s", t.strip(), 1)[0].strip()


def f1(a, b):
    x, y = norm(a).split(), norm(b).split()
    if not x or not y:
        return 0.0
    ortak = 0
    kalan = list(y)
    for w in x:
        if w in kalan:
            kalan.remove(w); ortak += 1
    if ortak == 0:
        return 0.0
    p, r = ortak / len(x), ortak / len(y)
    return 2 * p * r / (p + r)


def ayir(c):
    t = c.decode("utf-8")
    s, u = t.split("\nU: ", 1)
    return s[3:], u.rstrip("\n")


def kumeler():
    r = random.Random(5)
    d = derlem_kur()                       # dağıtım derlemi (sınama hariç)
    egit = [ayir(c) for c in r.sample(d["egit_cift"], min(80, len(d["egit_cift"])))]
    sozcuk = set()
    for c in d["egit_cift"]:
        sozcuk.update(norm(c.decode("utf-8")).split())
    for b in d["egit_duz"]:
        sozcuk.update(norm(b.decode("utf-8")).split())
    yanitlar = sorted({ayir(c)[1] for c in d["egit_cift"]})
    dis = list(ALAN_DISI_SABIT)
    js = os.path.join(UST, "tests", "umay_kanit", "son_olcum.json")
    if os.path.exists(js):
        k = json.load(open(js, encoding="utf-8"))["kumeler"]["sinama"]
        dis += k.get("dis", [])[:150] + k.get("zor", [])[:150]
    return {"sinama": sinama_kumesi(), "egitim": egit, "alan_disi": dis,
            "sozcuk": sozcuk, "yanitlar": yanitlar}


def olc(model, sozluk, K):
    sonuc, ham = {}, {}
    for ad in ("sinama", "egitim"):
        kume = K[ad]
        cik = uret(model, sozluk, [s for s, _ in kume])
        ham[ad] = [{"soru": s, "beklenen": y, "uretilen": c} for (s, y), c in zip(kume, cik)]
        f = [f1(c, y) for (_, y), c in zip(kume, cik)]
        sw = [w for c in cik for w in norm(c).split()]
        sonuc[ad] = {
            "toplam": len(kume),
            "tam": round(sum(c == y for (_, y), c in zip(kume, cik)) / len(kume), 3),
            "ilk_cumle": round(sum(ilk_cumle(c) == ilk_cumle(y) for (_, y), c in zip(kume, cik)) / len(kume), 3),
            "f1_ort": round(sum(f) / len(f), 3),
            "f1_06": round(sum(x >= 0.6 for x in f) / len(f), 3),
            "bozuk": round(sum(w not in K["sozcuk"] for w in sw) / max(1, len(sw)), 3),
        }
        sonuc[ad]["_f1"] = f
    cik = uret(model, sozluk, K["alan_disi"])
    ham["alan_disi"] = [{"soru": s, "uretilen": c} for s, c in zip(K["alan_disi"], cik)]
    ret = sum(("yeterli bilgi yok" in c) for c in cik)
    ezber = 0
    for c in cik:
        if "yeterli bilgi yok" in c:
            continue
        if max((f1(c, y) for y in K["yanitlar"]), default=0) >= 0.6:
            ezber += 1
    sonuc["alan_disi"] = {"toplam": len(cik), "ret": round(ret / len(cik), 3),
                          "ezber": round(ezber / len(cik), 3)}
    return sonuc, ham


def yazdir(ad, s):
    print(f"[{ad}]")
    for k in ("sinama", "egitim", "alan_disi"):
        print(f"  {k:9s} " + ", ".join(f"{a} {b}" for a, b in s[k].items() if not a.startswith("_")))


def main():
    if len(sys.argv) >= 6 and sys.argv[1] == "karsilastir":
        K = kumeler()
        e, eh = olc(sys.argv[2], sys.argv[3], K)
        y, yh = olc(sys.argv[4], sys.argv[5], K)
        yazdir("ESKİ " + sys.argv[2], e)
        yazdir("YENİ " + sys.argv[4], y)
        kaz = sum(b > a for a, b in zip(e["sinama"]["_f1"], y["sinama"]["_f1"]))
        kay = sum(b < a for a, b in zip(e["sinama"]["_f1"], y["sinama"]["_f1"]))
        ustun = (y["sinama"]["f1_ort"] > e["sinama"]["f1_ort"] and
                 y["sinama"]["ilk_cumle"] >= e["sinama"]["ilk_cumle"] and
                 y["sinama"]["bozuk"] <= e["sinama"]["bozuk"] + 0.01)
        print(f"sınama, soru soru F1: yeni daha iyi {kaz}, daha kötü {kay}, eşit "
              f"{len(e['sinama']['_f1']) - kaz - kay}")
        print("KARAR:", "YENİ MODEL ÜSTÜN — dağıtılabilir" if ustun else "yeni model üstün DEĞİL — dağıtılmaz")
        for s in (e, y):
            for k in s.values():
                k.pop("_f1", None) if isinstance(k, dict) else None
        json.dump({"eski": e, "yeni": y, "kazanc": kaz, "kayip": kay, "ustun": ustun,
                   "eski_ornek": eh, "yeni_ornek": yh},
                  open(os.path.join(KOK, "kov", "karsilastirma.json"), "w"), ensure_ascii=False, indent=1)
        sys.exit(0 if ustun else 3)
    model = sys.argv[1] if len(sys.argv) > 1 else os.path.join(KOK, "umay.us1")
    sozluk = sys.argv[2] if len(sys.argv) > 2 else os.path.join(KOK, "sozluk.txt")
    s, h = olc(model, sozluk, kumeler())
    yazdir(model, s)
    for k in s.values():
        k.pop("_f1", None)
    os.makedirs(os.path.join(KOK, "kov"), exist_ok=True)
    json.dump({"sonuc": s, "ornek": h}, open(os.path.join(KOK, "kov", "olcum.json"), "w"),
              ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
