#!/usr/bin/env python3
"""Eğitim/doğrulama SIZINTI denetimi (Ö7).

Bir derlem ayrımı için ölçer:
  1) soru sızıntısı   : doğrulama/sınama sorusunun NORMALLEŞTİRİLMİŞ biçimi
                        (küçük harf, noktalama yok, tek boşluk) eğitimde var mı
                        ("zeytin nedir" ile "Zeytin nedir?" AYNI sorudur)
  2) yanıt örtüşmesi  : doğrulama yanıtı eğitimde başka soruyla geçiyor mu
                        (yeniden ifade doğrulamasında TASARIM GEREĞİ vardır;
                        dil modeli doğrulamasında olmamalıdır)
  3) metin sızıntısı  : dil modeli doğrulama metninin sözcük 8'lilerinin kaçı
                        eğitim metninde (düzyazı + bütün çift satırları) geçiyor

Kullanım:
  python3 sizinti.py eski   # 1.4.4/1.4.5 ayrımı (derlem_kur_eski)
  python3 sizinti.py yeni   # derlem_kur(secim=True) ayrımı
"""
import os, re, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derlem


def norm(s):
    s = derlem.kucuk(s)
    s = re.sub(r"[^\w\s]", " ", s)
    return re.sub(r"\s+", " ", s).strip()


def ayir(c):
    t = c.decode("utf-8") if isinstance(c, bytes) else c
    s, u = t.split("\nU: ", 1)
    return s[3:], u.rstrip("\n")


def sekizli(metin, n=8):
    w = norm(metin).split()
    return {" ".join(w[i:i + n]) for i in range(len(w) - n + 1)}


def denetle(d):
    egit = [ayir(c) for c in d["egit_cift"]]
    egit_s = {norm(s) for s, _ in egit}
    egit_y = {norm(y) for _, y in egit}
    rapor = {}
    for ad in ("dog_cift", "sinama_cift"):
        if ad not in d or not d[ad]:
            continue
        kume = [ayir(c) for c in d[ad]]
        rapor[ad] = {
            "toplam": len(kume),
            "soru_egitimde_birebir": sum(1 for s, _ in kume if s in {x for x, _ in egit}),
            "soru_egitimde_normal": sum(1 for s, _ in kume if norm(s) in egit_s),
            "yanit_egitimde": sum(1 for _, y in kume if norm(y) in egit_y),
        }
    if d.get("dog_duz"):
        egit_metin = b"\n".join(d["egit_duz"]).decode("utf-8", "replace") + "\n" + \
                     "\n".join(f"{s}\n{y}" for s, y in egit)
        e8 = sekizli(egit_metin)
        d8 = set()
        for b in d["dog_duz"]:
            d8 |= sekizli(b.decode("utf-8", "replace"))
        rapor["dog_duz"] = {"sekizli": len(d8), "egitimde": len(d8 & e8),
                            "oran": round(len(d8 & e8) / max(1, len(d8)), 3)}
    return rapor


if __name__ == "__main__":
    kip = sys.argv[1] if len(sys.argv) > 1 else "yeni"
    d = derlem.derlem_kur_eski() if kip == "eski" else derlem.derlem_kur(secim=True)
    r = denetle(d)
    print(json.dumps(r, ensure_ascii=False, indent=1))
