#!/usr/bin/env python3
"""İki modelin sınama sonuçları arasındaki farkın İSTATİSTİKSEL anlamlılığı.

  python3 anlamlilik.py <karsilastirma.json> [cikti.json]

Girdi: olc.py karsilastir'ın yazdığı json (soru soru üretimler 'eski_ornek',
'yeni_ornek'). Aynı sorular iki modelde de ölçüldüğü için sınamalar EŞLİdir.

  - F1 farkı (yeni - eski), soru başına:
      eşli permütasyon sınaması (işaret çevirme, iki yönlü, 20 000 örnek,
      tohum 147) ve %95 bootstrap güven aralığı (10 000 örnek, tohum 147);
  - ilk cümle doğru mu (0/1), soru başına:
      kesin McNemar (yalnız uyuşmayan çiftler; iki yönlü binom);
  - alan dışı ret (0/1): kesin McNemar.

Karar (önceden yazılmış kural, gevşetilmez): "anlamlı iyileşme" ANCAK
  F1 için p < 0,05 ve güven aralığının alt ucu > 0, ilk cümle kötüleşmemiş
  (McNemar anlamlı kötüleşme yok) ve alan dışı ret anlamlı kötüleşmemişse.
Aksi hâlde: "anlamlı fark yok" ya da "anlamlı kötüleşme".
"""
import sys, json, random, re, os
from math import comb
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from olc import f1, ilk_cumle

TOHUM, N_PERM, N_BOOT = 147, 20000, 10000


def eslik_perm(fark, rng):
    g = abs(sum(fark))
    n = 0
    for _ in range(N_PERM):
        s = sum(x if rng.random() < 0.5 else -x for x in fark)
        n += abs(s) >= g - 1e-12
    return (n + 1) / (N_PERM + 1)


def bootstrap(fark, rng):
    m = len(fark)
    ort = sorted(sum(fark[rng.randrange(m)] for _ in range(m)) / m for _ in range(N_BOOT))
    return ort[int(0.025 * N_BOOT)], ort[int(0.975 * N_BOOT) - 1]


def mcnemar(a, b):
    """a, b: 0/1 listeleri (eski, yeni). Dönüş: (yeni+ eski-, eski+ yeni-, p)."""
    y = sum(1 for x, z in zip(a, b) if z and not x)
    e = sum(1 for x, z in zip(a, b) if x and not z)
    n = y + e
    if n == 0:
        return y, e, 1.0
    k = min(y, e)
    p = min(1.0, 2 * sum(comb(n, i) for i in range(k + 1)) / 2 ** n)
    return y, e, p


def main():
    k = json.load(open(sys.argv[1], encoding="utf-8"))
    E, Y = k["eski_ornek"], k["yeni_ornek"]
    es = E["sinama"]; ys = Y["sinama"]
    assert [x["soru"] for x in es] == [x["soru"] for x in ys], "sınama soruları eşleşmiyor"
    fe = [f1(x["uretilen"], x["beklenen"]) for x in es]
    fy = [f1(x["uretilen"], x["beklenen"]) for x in ys]
    fark = [b - a for a, b in zip(fe, fy)]
    rng = random.Random(TOHUM)
    p_f1 = eslik_perm(fark, rng)
    alt, ust = bootstrap(fark, random.Random(TOHUM))
    ie = [int(ilk_cumle(x["uretilen"]) == ilk_cumle(x["beklenen"])) for x in es]
    iy = [int(ilk_cumle(x["uretilen"]) == ilk_cumle(x["beklenen"])) for x in ys]
    ic = mcnemar(ie, iy)
    ad_e, ad_y = E["alan_disi"], Y["alan_disi"]
    assert [x["soru"] for x in ad_e] == [x["soru"] for x in ad_y]
    re_ = [int("yeterli bilgi yok" in x["uretilen"]) for x in ad_e]
    ry = [int("yeterli bilgi yok" in x["uretilen"]) for x in ad_y]
    rc = mcnemar(re_, ry)
    n = len(fark)
    ort = sum(fark) / n
    ilk_kotu = ic[1] > ic[0] and ic[2] < 0.05
    ret_kotu = rc[1] > rc[0] and rc[2] < 0.05
    if p_f1 < 0.05 and alt > 0 and not ilk_kotu and not ret_kotu:
        karar = "ANLAMLI İYİLEŞME"
    elif (p_f1 < 0.05 and ust < 0) or ilk_kotu or ret_kotu:
        karar = "ANLAMLI KÖTÜLEŞME"
    else:
        karar = "ANLAMLI FARK YOK"
    s = {
        "sinama_soru": n,
        "f1_eski": round(sum(fe) / n, 4), "f1_yeni": round(sum(fy) / n, 4),
        "f1_fark_ort": round(ort, 4), "f1_fark_gA95": [round(alt, 4), round(ust, 4)],
        "f1_p_permutasyon": round(p_f1, 4),
        "f1_soru_yeni_iyi": sum(x > 0 for x in fark), "f1_soru_yeni_kotu": sum(x < 0 for x in fark),
        "ilk_cumle_eski": sum(ie), "ilk_cumle_yeni": sum(iy),
        "ilk_cumle_mcnemar": {"yalniz_yeni_dogru": ic[0], "yalniz_eski_dogru": ic[1], "p": round(ic[2], 4)},
        "alan_disi_soru": len(re_), "ret_eski": sum(re_), "ret_yeni": sum(ry),
        "ret_mcnemar": {"yalniz_yeni_ret": rc[0], "yalniz_eski_ret": rc[1], "p": round(rc[2], 4)},
        "karar": karar,
        "yontem": f"eşli permütasyon {N_PERM}, bootstrap {N_BOOT}, tohum {TOHUM}, kesin McNemar",
    }
    print(json.dumps(s, ensure_ascii=False, indent=1))
    if len(sys.argv) > 2:
        json.dump(s, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
