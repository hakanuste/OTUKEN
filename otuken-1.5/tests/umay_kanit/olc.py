#!/usr/bin/env python3
"""UMAY kanıt karar katmanı: veri kümeleri + ölçüm (tek komut: ./calistir.sh).

Kümeler ÜRETİLİR; kodda konu/soru listesi yoktur. "ornek" (yönergedeki
alan dışı örnekler) YALNIZ raporlanır, parametre seçiminde kullanılmaz.

  ic      kütüphane kayıtlarının cümlelerinden, o cümlenin sözcükleriyle kurulan
          sorular. Beklenen: YETERLİ ve sorunun geldiği kayıt DAYANAKTA (_dogru);
          ayrıca ilk dayanak olma oranı (_ilk) raporlanır.
          Geliştirme/sınama ayrımı KAYIT düzeyinde.
  ic_ey   aynı, sona UD'den gerçek bir çekimli yüklem eklenmiş ("... durur").
  dis     UD Türkçe ağaç bankalarındaki ad/sıfat/özel adlardan, kütüphanede HİÇ
          geçmeyenler (C'nin kendi eşlemesiyle sıklık 0). Beklenen: YETERSİZ.
          Ayrım SÖZCÜK düzeyinde.
  dis_ey  aynı + sona yüklem.
  zor     bir kütüphane sözcüğü + bir yabancı ad ("zeytin <yabancı>"). YETERSİZ.
  zor_ey  aynı + sona yüklem.
  zor_yalin / dis_yalin  soru sözcüğü OLMADAN ("zeytin <yabancı>", "<yabancı>
          <yabancı>"): yabancı ad sorunun son sözcüğüdür (yüklem kuralının en
          zor durumu). YETERSİZ.

Parametreler (kapsama eşiği E, yüklem ağırlığı Y) GELİŞTİRME kümesinde
seçilir; SINAMA kümesi ayrı raporlanır. Karar, C'nin döktüğü terim
ayrıntısından (terim:df:kapsandı:yüklem) C'deki formülle yeniden hesaplanır;
C'deki parametrelerle C'nin kendi kararıyla BİREBİR aynı olduğu denetlenir.
"""
import json, os, random, re, subprocess, sys
KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(KOK, "us1", "arac"))
from derlem import doktrin_oku  # noqa: E402

ARAC = sys.argv[1] if len(sys.argv) > 1 else "/tmp/kanit_olc"
BELGE = sys.argv[2] if len(sys.argv) > 2 else "/tmp/kanit_belge"
UD = sys.argv[3] if len(sys.argv) > 3 else "/tmp/ud"
# C'deki değerler (include/umay_kanit.h) — yeniden hesap denetimi için
C_ESIK = C_YUKLEM = None
for _l in open(os.path.join(KOK, "umay", "include", "umay_kanit.h"), encoding="utf-8"):
    _m = re.match(r"#define KANIT_KAPSAMA_ESIK\s+(\d+)", _l)
    if _m: C_ESIK = int(_m.group(1))
    _m = re.match(r"#define KANIT_YUKLEM_AGIRLIK\s+(\d+)", _l)
    if _m: C_YUKLEM = int(_m.group(1))

KALIP_IC = ["{} nedir", "{} nasıl", "{} hakkında bilgi ver", "{} anlat", "{} ne olur",
            "{} nerede", "{} neyle yapılır", "{} ne işe yarar", "{} kaç"]
KALIP_DIS = ["{} nedir", "{} nasıl yapılır", "{} hakkında bilgi ver", "{} anlat",
             "{} ne zaman", "{} kaç", "{} hangisi"]
ORNEK = ["kuantum bilgisayar nedir", "basketbol kuralları", "bitcoin fiyatı",
         "yarın yağmur yağacak mı", "en iyi telefon hangisi"]


def kucuk(s):
    return s.replace("I", "ı").replace("İ", "i").lower()


def sozcukler(metin):
    return [kucuk(w) for w in re.findall(r"[A-Za-zÇĞİÖŞÜçğıöşüÂâÎîÛû]+", metin)]


def arac(kip, satirlar):
    p = subprocess.run([ARAC, kip, BELGE], input="\n".join(satirlar) + "\n",
                       capture_output=True, text=True, timeout=900)
    # host kapısı kütüphane iletilerini de stdout'a yazar; sonuç satırları sekmelidir
    out = [l.split("\t") for l in p.stdout.splitlines() if "\t" in l]
    if len(out) != len([x for x in satirlar if x.strip()]):
        raise SystemExit(f"arac: {len(satirlar)} girdi, {len(out)} sonuç satırı")
    return out


def siklik(ws):
    return {w: int(n) for w, n in arac("siklik", sorted(set(ws)))}


def ud_oku():
    """UD'den (IMST hariç: ticari olmayan lisans) ad/sıfat/özel ad sözcükleri ve
    tümce kökü çekimli eylemler (Mood=... ve VerbForm yok)."""
    adlar, eylemler, cumle = {}, {}, 0
    for d in sorted(os.listdir(UD)):
        yol = os.path.join(UD, d)
        if "IMST" in d or not os.path.isdir(yol):
            continue
        for f in sorted(os.listdir(yol)):
            if not f.endswith(".conllu"):
                continue
            aralik = {}
            for l in open(os.path.join(yol, f), encoding="utf-8"):
                if l.startswith("# text"):
                    cumle += 1; aralik = {}; continue
                c = l.rstrip("\n").split("\t")
                if len(c) != 10:
                    continue
                if "-" in c[0]:
                    a, b = c[0].split("-")
                    for i in range(int(a), int(b) + 1): aralik[str(i)] = c[1]
                    continue
                if "." in c[0]:
                    continue
                w = kucuk(aralik.get(c[0], c[1]))
                if not re.fullmatch(r"[a-zçğıöşüâîû]{5,14}", w):
                    continue
                if c[3] in ("NOUN", "PROPN", "ADJ"):
                    adlar[w] = adlar.get(w, 0) + 1
                elif (c[3] == "VERB" and c[7] == "root" and "VerbForm" not in c[5]
                      and re.search(r"Mood=(Ind|Nec|Pot|Opt)", c[5])):
                    # soru yüklemi olabilecek kipler (buyruk kipi soru yüklemi değildir)
                    eylemler[w] = eylemler.get(w, 0) + 1
    return adlar, eylemler, cumle


def kararlar(sorular):
    for s, l in zip(sorular, arac("karar", [s["soru"] for s in sorular])):
        s["c_yeterli"], s["c_kapsama"] = int(l[0]), int(l[1])
        s["ilk_kayit"], s["n"] = l[5], int(l[4])
        s["terim"] = [] if l[7] == "-" else [
            (t.rsplit(":", 3)[0],) + tuple(int(x) for x in t.rsplit(":", 3)[1:]) for t in l[7].split(",")]
        s["N"] = int(l[8])
        s["dayanak"] = [] if l[9] == "-" else l[9].split("|")
    return sorular


def agirlik(df, N):
    if df == 0: return 400
    if df == 1: return 300
    if df <= 3: return 200
    if df * 4 <= N: return 120
    return 60


def karar(s, E, Y):
    """C'deki kanit_degerlendir ile aynı formül (umay/core/umay_kanit.c)."""
    if s["n"] == 0 or not s["terim"]:
        return 0, 0
    top = kap = 0; odak = -1; enb = -1
    for i, (_, df, kapsandi, yuklem) in enumerate(s["terim"]):
        a = Y if yuklem else agirlik(df, s["N"])
        top += a
        if kapsandi: kap += a
        if not yuklem and a > enb: enb, odak = a, i
    kapsama = (100 * kap) // top if top else 0
    return int(kapsama >= E and odak >= 0 and s["terim"][odak][2] == 1), kapsama


def oranlar(k, E, Y):
    o = {}
    for g, v in k.items():
        if not v: continue
        yet = [karar(s, E, Y)[0] for s in v]
        if g.startswith("ic"):
            o[g + "_yanit"] = sum(yet) / len(v)
            o[g + "_ilk"] = sum(y and s["ilk_kayit"] == s["beklenen_kayit"] for y, s in zip(yet, v)) / len(v)
            o[g + "_dogru"] = sum(y and s["beklenen_kayit"] in s["dayanak"] for y, s in zip(yet, v)) / len(v)
        else:
            o[g + "_ret"] = 1 - sum(yet) / len(v)
    return o


# Seçim ölçütü (güvenlik önce): her alan dışı kümede yanlış kabul <= %1
# olmalı; bu koşulu sağlayanlar arasında alan içi doğru yanıt oranı en yüksek
# olan seçilir; eşitlikte daha tutucu (yüksek E, yüksek Y).
AZAMI_YANLIS_KABUL = 0.01


def uygun(o):
    return all(o[g] >= 1 - AZAMI_YANLIS_KABUL for g in o if g.endswith("_ret"))


def puan(o):
    return (o["ic_dogru"] + o["ic_ey_dogru"]) / 2


def main():
    r = random.Random(2026)
    kayit = [{"baslik": k["bas"], "metin": k["ic"]} for k in doktrin_oku()]

    adlar, eylemler, n_ud = ud_oku()
    ad_aday = sorted(w for w, c in adlar.items() if c >= 2)
    ey_aday = sorted(w for w, c in eylemler.items() if c >= 2)
    lib_s = sorted({w for kk in kayit for w in sozcukler(kk["metin"]) if len(w) >= 5})
    df = siklik(ad_aday + ey_aday + lib_s)
    yabanci = [w for w in ad_aday if df[w] == 0]
    eylem = [w for w in ey_aday if df[w] == 0]
    lib_ortak = [w for w in lib_s if df.get(w, 0) >= 2]
    r.shuffle(yabanci); r.shuffle(eylem)
    yabanci = yabanci[:1600]
    ey_yari = len(eylem) // 2

    # yüklem kuralının kapsamı: UD kök çekimli eylemlerinin kaçı eylem ekli sayılıyor
    ek = arac("karar", [f"zeytin {w}" for w in eylem[:600]])
    ekli = sum(1 for l in ek if l[7] != "-" and l[7].split(",")[-1].endswith(":1"))
    # ve kütüphanede geçmeyen ADLARIN kaçı yanlışlıkla eylem ekli sayılıyor
    ek2 = arac("karar", [f"zeytin {w}" for w in yabanci[:600]])
    ad_ekli = sum(1 for l in ek2 if l[7] != "-" and l[7].split(",")[-1].endswith(":1"))

    r2 = random.Random(7)
    kayitlar = kayit[:]; r2.shuffle(kayitlar)
    yari_k = len(kayitlar) // 2
    grup = ("ic", "ic_ey", "dis", "dis_ey", "dis_yalin", "zor", "zor_ey", "zor_yalin")
    k = {t: {g: [] for g in grup} for t in ("gelistirme", "sinama")}
    for i, kk in enumerate(kayitlar):
        t = "gelistirme" if i < yari_k else "sinama"
        eyl = eylem[:ey_yari] if t == "gelistirme" else eylem[ey_yari:]
        for c in [c for c in re.split(r"(?<=[.;])\s+", kk["metin"]) if len(c) > 20]:
            ws = [w for w in sozcukler(c) if len(w) >= 5]
            if not ws: continue
            for _ in range(3):
                sec = r.sample(ws, min(len(ws), r.choice((1, 2, 2, 3))))
                k[t]["ic"].append({"soru": r.choice(KALIP_IC).format(" ".join(sec)), "beklenen_kayit": kk["baslik"]})
                k[t]["ic_ey"].append({"soru": " ".join(sec) + " " + r.choice(eyl), "beklenen_kayit": kk["baslik"]})
    yy = len(yabanci) // 2
    for t, ws in (("gelistirme", yabanci[:yy]), ("sinama", yabanci[yy:])):
        eyl = eylem[:ey_yari] if t == "gelistirme" else eylem[ey_yari:]
        for i in range(0, len(ws) - 1, 2):
            iki = ws[i:i + 1 + (i // 2) % 2]
            k[t]["dis"].append({"soru": r.choice(KALIP_DIS).format(" ".join(iki))})
            k[t]["dis_ey"].append({"soru": " ".join(iki) + " " + r.choice(eyl)})
            k[t]["zor"].append({"soru": r.choice(KALIP_DIS).format(f"{r.choice(lib_ortak)} {ws[i]}")})
            k[t]["zor_ey"].append({"soru": f"{r.choice(lib_ortak)} {ws[i]} {r.choice(eyl)}"})
            k[t]["dis_yalin"].append({"soru": " ".join(iki)})
            k[t]["zor_yalin"].append({"soru": f"{r.choice(lib_ortak)} {ws[i + 1]}"})
    for t in k:
        for g in k[t]:
            kararlar(k[t][g])
    ornek = kararlar([{"soru": s} for s in ORNEK])

    # anlamsız alan içi soru: hiç içerik terimi yok (yalnız işlev sözcükleri) -> ayrı sayılır
    anlamsiz = {t: sum(1 for g in ("ic", "ic_ey") for s in k[t][g] if not s["terim"]) for t in k}
    for t in k:
        for g in ("ic", "ic_ey"):
            k[t][g] = [s for s in k[t][g] if s["terim"]]

    # C kararı ile Python yeniden hesabı birebir mi
    uyumsuz = [(s["soru"], s["c_yeterli"], s["c_kapsama"], karar(s, C_ESIK, C_YUKLEM))
               for t in k for g in k[t] for s in k[t][g]
               if (s["c_yeterli"], s["c_kapsama"]) != karar(s, C_ESIK, C_YUKLEM)]

    print(f"UD: {n_ud} tümce; kütüphanede geçmeyen ad {len(yabanci)}, kök eylem {len(eylem)}")
    print(f"  yüklem kuralı: kök eylemlerin {ekli}/{min(600, len(eylem))} eylem ekli; "
          f"adların {ad_ekli}/{min(600, len(yabanci))} yanlışlıkla eylem ekli")
    for t in k:
        print(f"{t}: " + ", ".join(f"{g} {len(v)}" for g, v in k[t].items()) + f"; anlamsız ic {anlamsiz[t]}")
    for g in grup:
        a = {s["soru"] for s in k["gelistirme"][g]}; b = {s["soru"] for s in k["sinama"][g]}
        print(f"  sızıntı {g}: ortak soru {len(a & b)}")
    ka = {s["beklenen_kayit"] for s in k["gelistirme"]["ic"]}; kb = {s["beklenen_kayit"] for s in k["sinama"]["ic"]}
    print(f"  sızıntı ic kayıt: {len(ka & kb)}")
    print(f"C kararı = yeniden hesap (E={C_ESIK}, Y={C_YUKLEM}): uyumsuz {len(uyumsuz)}")
    for u in uyumsuz[:5]: print("   ", u)

    print("\nTarama (GELİŞTİRME): E=kapsama eşiği, Y=yüklem ağırlığı")
    print("   E    Y  ic_doğru ic_ey_doğru dış_ret dış_ey dış_yalın zor_ret zor_ey zor_yalın  puan")
    en = None
    for E in (40, 50, 60, 70, 75, 80, 85, 90):
        for Y in (20, 40, 60, 120, 200, 300, 400):
            o = oranlar(k["gelistirme"], E, Y); p = puan(o) if uygun(o) else -1
            print(f"  {E:3d} {Y:4d}   {o['ic_dogru']:.3f}    {o['ic_ey_dogru']:.3f}     {o['dis_ret']:.3f}  "
                  f"{o['dis_ey_ret']:.3f}  {o['dis_yalin_ret']:.3f}    {o['zor_ret']:.3f}  {o['zor_ey_ret']:.3f}  "
                  f"{o['zor_yalin_ret']:.3f}    {p:.4f}" + ("" if p >= 0 else "  (yanlış kabul > %1)"))
            # eşitlikte daha tutucu (yüksek E, yüksek Y) seçilir
            if en is None or p > en[2] + 1e-12 or (abs(p - en[2]) <= 1e-12 and (E, Y) > (en[0], en[1])):
                en = (E, Y, p)
    E, Y, _ = en
    o = oranlar(k["sinama"], E, Y)
    print(f"\nSeçilen (geliştirme): E={E}, Y={Y}   [C'deki: E={C_ESIK}, Y={C_YUKLEM}]")
    print("SINAMA: " + ", ".join(f"{a} {b:.3f}" for a, b in o.items()))
    orn = [(s["soru"], karar(s, E, Y)[0]) for s in ornek]
    print(f"Yönerge örnekleri: ret {sum(1 for _, y in orn if not y)}/{len(orn)}")
    for q, y in orn:
        if y: print(f"  YANLIŞ KABUL: {q}")
    json.dump({"secilen": {"E": E, "Y": Y}, "c": {"E": C_ESIK, "Y": C_YUKLEM}, "sinama": o,
               "uyumsuz": len(uyumsuz), "ornek": orn,
               "kumeler": {t: {g: [s["soru"] for s in v] for g, v in k[t].items()} for t in k}},
              open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "son_olcum.json"), "w"),
              ensure_ascii=False, indent=1)
    kotu = [s for s in k["sinama"]["ic"] + k["sinama"]["ic_ey"]
            if not (karar(s, E, Y)[0] and s["beklenen_kayit"] in s["dayanak"])]
    print(f"\nSınama alan içi hatalı ({len(kotu)}), ilk 30:")
    for s in kotu[:30]:
        print(f"  {s['soru']} | beklenen {s['beklenen_kayit']} | dayanak {'/'.join(s['dayanak'])} | "
              + " ".join(f"{t}:{d}:{c}:{y}" for t, d, c, y in s["terim"]))
    kotu = [s for g in grup[2:] for s in k["sinama"][g] if karar(s, E, Y)[0]]
    print(f"\nSınama alan dışı YANLIŞ KABUL ({len(kotu)}), ilk 30:")
    for s in kotu[:30]:
        print(f"  {s['soru']} | ilk {s['ilk_kayit']} | " + " ".join(f"{t}:{d}:{c}:{y}" for t, d, c, y in s["terim"]))
    return 0 if not uyumsuz else 1


if __name__ == "__main__":
    sys.exit(main())
