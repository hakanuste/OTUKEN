#!/usr/bin/env python3
"""UMAY eğitim derlemi.

Kaynaklar (hepsi bayt düzeyinde, UTF-8):
  - umay/core/umay_bilgi.c içindeki gömülü DOKTRIN kayıtları
  - us1/derlem/*.txt  (V15 proje veri tabanı, kişilik, el yazımı soru-yanıt)
  - diskteki BELGE*.TXT (mtools ile, varsa)

Çıktı iki türlüdür:
  duzyazi : sürekli metin (belgeler); rastgele pencereden öğrenilir
  cift    : "S: soru\\nU: yanıt\\n" soru-yanıt çiftleri; pencere çoğunlukla
            çiftin başından açılır ki soru bağlamda dursun

ÖTÜKEN'de üretim aynı kalıpla istenir: "S: <soru>\\nU: " -> satır sonuna dek.

Doğrulama: çiftlerin ~%10'u ve düzyazının her belgesinin son %10'u
eğitime GİRMEZ. Böylece ezber (eğitim kaybı) ile genelleme (doğrulama
kaybı) ayrı ölçülür.
"""
import os, re, sys, random, subprocess

KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DERLEM = os.path.join(KOK, "us1", "derlem")


def c_dize_coz(s):
    """C dize sabitini (bitişik parçalar dahil) Python dizesine çevirir."""
    parcalar = re.findall(r'"((?:[^"\\]|\\.)*)"', s)
    t = "".join(parcalar)
    return t.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


def doktrin_oku():
    kaynak = open(os.path.join(KOK, "umay/core/umay_bilgi.c"), encoding="utf-8").read()
    bas = kaynak.index("DOKTRIN[] = {")
    son = kaynak.index("{0,0,0,0}", bas)
    govde = kaynak[bas:son]
    kayitlar = []
    for m in re.finditer(r'\{("(?:[^"\\]|\\.)*"),\s*("(?:[^"\\]|\\.)*"),\s*("(?:[^"\\]|\\.)*"),\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}', govde):
        kat, bas_, etiket, ic = (c_dize_coz(m.group(i)) for i in range(1, 5))
        kayitlar.append({"kat": kat, "bas": bas_, "etiket": etiket, "ic": ic})
    return kayitlar


def belgeler_oku():
    """Diskteki BELGE*.TXT (ASCII Türkçe). mtools yoksa sessizce boş."""
    disk = os.path.join(KOK, "diskfiles.img")
    cikti = []
    for ad in ("BELGE1.TXT", "BELGE2.TXT", "BELGE3.TXT"):
        try:
            r = subprocess.run(["mtype", "-i", disk, "::" + ad], capture_output=True, timeout=10)
            if r.returncode == 0:
                t = r.stdout.decode("utf-8", "replace").replace("\r", "")
                cikti.append(t.strip())
        except Exception:
            pass
    return cikti


def cumleler(metin):
    metin = re.sub(r"\s+", " ", metin).strip()
    parca = re.split(r"(?<=[.;])\s+(?=[A-ZÇĞİÖŞÜ0-9])", metin)
    return [p.strip() for p in parca if len(p.strip()) > 8]


def kisalt(s, azami=150):
    """Yanıt satırı UTF-8 baytta 'azami'yi aşmasın; sözcük sınırından kes."""
    b = s.encode("utf-8")
    if len(b) <= azami:
        return s
    k = b[:azami].decode("utf-8", "ignore")
    k = k[: k.rfind(" ")] if " " in k else k
    return k.rstrip(",;:") + "."


def kucuk(s):
    return s.replace("I", "ı").replace("İ", "i").lower()


def soru_bicimleri(soru, r):
    """Aynı soru farklı yazımlarla: küçük/büyük baş harf, soru işareti."""
    ç = [soru, soru + "?"]
    if r.random() < 0.5:
        ç.append(soru[:1].upper() + soru[1:] + "?")
    return ç


DURAK = set("""ve ile ya da veya için gibi olarak olan bir bu şu o de da ki mi mı mu mü
çok daha en her hiç ise yani ancak ama fakat değil kadar sonra önce üzeri üstü
yerine sayede edilir edilecek edilecektir kullanılır kullanılacaktır olur olacak
yapılır yapılacak yapılacaktır sağlanır sağlayacak şekilde tüm tamamen yalnızca
alınır konur kurulur kurulacaktır""".split())

SORU_KALIP = ["{} nedir", "{} nasıl", "{} ne olur", "{} nasıl yapılır", "{} nerede",
              "{} neyle yapılır", "{} hakkında bilgi ver", "{} anlat", "{} ne işe yarar",
              "{} kaç", "{} nasıl olmalı", "{} ne durumda"]

ALAN_DISI = ["borsa", "kripto para", "futbol maçı", "uzay istasyonu", "hava durumu",
             "yarınki haber", "dolar kuru", "seçim sonucu", "film önerisi", "yemek tarifi",
             "piyango", "sinema", "moda", "oyun konsolu", "tatil otelleri"]
BILMIYORUM = "Bu konuda kütüphanemde yeterli bilgi yok; uydurmam."


def anahtar_sozcukler(cumle):
    s = [w.strip(".,;:()'\"") for w in cumle.split()]
    return [kucuk(w) for w in s if len(w) >= 4 and kucuk(w) not in DURAK and not w.isdigit()]


def turetilmis_sorular(cumle, r, adet):
    """Yanıt cümlesinin kendi anahtar sözcüklerinden sorular: model,
    soruda gördüğü sözcüğü o sözcüğü taşıyan yanıta bağlamayı öğrenir."""
    a = anahtar_sozcukler(cumle)
    if not a:
        return []
    out = set()
    for _ in range(adet * 3):
        if len(out) >= adet:
            break
        i = r.randrange(len(a))
        k = [a[i]] if r.random() < 0.55 or i + 1 >= len(a) else [a[i], a[i + 1]]
        out.add(r.choice(SORU_KALIP).format(" ".join(k)))
    return sorted(out)


def ciftler_kur(kayitlar, r):
    ciftler = []
    # 0) V15 cümleleri de soru-yanıt olur (türetilmiş sorularla)
    v15 = open(os.path.join(DERLEM, "v15.txt"), encoding="utf-8").read()
    for satir in v15.split("\n"):
        if ":" not in satir or len(satir) < 30:
            continue
        for c in cumleler(satir.split(":", 1)[1]):
            y = kisalt(c)
            for sb in turetilmis_sorular(c, r, 6):
                ciftler.append((sb, y))
    # 3) Alan dışı: dürüst ret
    for konu in ALAN_DISI:
        for kalip in r.sample(SORU_KALIP, 4):
            ciftler.append((kalip.format(konu), BILMIYORUM))
    # 1) Doktrin: başlık ve etiketlerden sorular, içerik cümlelerinden yanıtlar
    for k in kayitlar:
        cm = cumleler(k["ic"])
        if not cm:
            continue
        b = kucuk(k["bas"])
        sorular = [f"{b} nedir", f"{b} hakkında ne bilirsin", f"{b} anlat"]
        for e in k["etiket"].split(","):
            e = e.strip()
            if e:
                sorular += [f"{e} nasıl", f"{e} hakkında bilgi ver"]
        for i, c in enumerate(cm):
            y = kisalt(c)
            # ilk cümle en çok tekrarlanır: sorunun "özü" odur
            adet = 3 if i == 0 else 1
            for _ in range(adet):
                s = r.choice(sorular)
                for sb in soru_bicimleri(s, r)[:2]:
                    ciftler.append((sb, y))
            for sb in turetilmis_sorular(c, r, 6):
                ciftler.append((sb, y))
    # 2) El yazımı soru-yanıt
    t = open(os.path.join(DERLEM, "soru_yanit.txt"), encoding="utf-8").read()
    for m in re.finditer(r"^S: (.+)\nU: (.+)$", t, re.M):
        s, y = m.group(1).strip(), m.group(2).strip()
        for sb in soru_bicimleri(s, r):
            for _ in range(3):
                ciftler.append((sb, y))
        for sb in turetilmis_sorular(y, r, 3):
            ciftler.append((sb, y))
    return ciftler


def duzyazi_kur(kayitlar):
    belgeler = []
    for ad in ("v15.txt", "kisilik.txt"):
        belgeler.append(open(os.path.join(DERLEM, ad), encoding="utf-8").read().strip())
    for k in kayitlar:
        belgeler.append(f"{k['bas']}. {re.sub(r'[ ]+', ' ', k['ic']).strip()}")
    belgeler += belgeler_oku()
    return belgeler


def derlem_kur_eski(tohum=11):
    """1.4.4/1.4.5 ayrımı AYNEN (eski modelin eğitildiği derlem). Yalnız
    sızıntı denetimi ve sabit sınama kümesinin seçimi için tutulur.
    SIZINTILIDIR: doğrulama çiftlerinin yanıtları eğitimde de var; "?"lı /
    büyük harfli yazım ayrı soru sayılıyor; düzyazı doğrulaması (belge
    sonları) aynı cümleleri yanıt olarak eğitimde bulunduruyor."""
    r = random.Random(tohum)
    kayitlar = doktrin_oku()
    ciftler = ciftler_kur(kayitlar, r)
    duz = duzyazi_kur(kayitlar)

    # doğrulama ayrımı: benzersiz (soru,yanıt) çiftlerinin %10'u
    benzersiz = sorted(set(ciftler))
    r.shuffle(benzersiz)
    n_dog = max(4, len(benzersiz) // 10)
    dog_cift = set(benzersiz[:n_dog])
    egit_cift = [c for c in ciftler if c not in dog_cift]

    egit_duz, dog_duz = [], []
    for b in duz:
        bb = b.encode("utf-8")
        kes = int(len(bb) * 0.9)
        # sözcük sınırına çek
        while kes < len(bb) and bb[kes:kes + 1] != b" ":
            kes += 1
        egit_duz.append(bb[:kes])
        if len(bb) - kes > 40:
            dog_duz.append(bb[kes:].strip())

    def cift_bayt(c):
        return f"S: {c[0]}\nU: {c[1]}\n".encode("utf-8")

    return {
        "egit_duz": egit_duz, "dog_duz": dog_duz,
        "egit_cift": [cift_bayt(c) for c in egit_cift],
        "dog_cift": [cift_bayt(c) for c in sorted(dog_cift)],
        "doktrin_sayi": len(kayitlar),
    }


def norm(s):
    """Soru/yanıt karşılaştırması için: küçük harf, noktalama yok, tek boşluk."""
    s = kucuk(s)
    s = re.sub(r"[^\w\s]", " ", s)
    return re.sub(r"\s+", " ", s).strip()


def sekizliler(metin, n=8):
    w = norm(metin).split()
    return {" ".join(w[i:i + n]) for i in range(len(w) - n + 1)}


SINAMA_DOSYA = os.path.join(DERLEM, "sinama_kumesi.tsv")


def sinama_kumesi():
    """SABİT sınama kümesi (soru, yanıt). Dosyadan okunur; yoksa bir kez
    kurulup dondurulur. Seçim: ESKİ modelin doğrulama çiftlerinden, soru
    biçimi (normalleştirilmiş) eski eğitimde HİÇ geçmeyenler, soru başına bir.
    Böylece eski model de yeni model de bu soruları hiç görmemiştir ve
    ikisi aynı kümede adil karşılaştırılır. Yeni eğitim bu soruları (bütün
    yazım biçimleriyle) hiçbir zaman görmez."""
    if os.path.exists(SINAMA_DOSYA):
        out = []
        for l in open(SINAMA_DOSYA, encoding="utf-8"):
            if l.startswith("#") or "\t" not in l:
                continue
            q, a = l.rstrip("\n").split("\t", 1)
            out.append((q, a))
        return out
    e = derlem_kur_eski()
    ayir = lambda c: (c.decode().split("\nU: ", 1)[0][3:], c.decode().split("\nU: ", 1)[1].rstrip("\n"))
    egit_n = {norm(ayir(c)[0]) for c in e["egit_cift"]}
    goruldu, out = set(), []
    for c in e["dog_cift"]:
        q, a = ayir(c)
        if norm(q) in egit_n or norm(q) in goruldu or a == BILMIYORUM:
            continue
        goruldu.add(norm(q))
        out.append((q, a))
    with open(SINAMA_DOSYA, "w", encoding="utf-8") as f:
        f.write("# UMAY US1 SABİT sınama kümesi: soru<TAB>beklenen yanıt. derlem.sinama_kumesi() kurdu;\n")
        f.write("# eski (1.4.4/1.4.5) modelin eğitiminde bu soruların hiçbir yazım biçimi yok. DEĞİŞTİRME.\n")
        for q, a in out:
            f.write(f"{q}\t{a}\n")
    return out


def derlem_kur(tohum=11, secim=False):
    """Sızıntısız ayrım (Ö7).
      sinama_cift : SABİT sınama kümesi (sinama_kumesi); bu soruların HİÇBİR
                    yazım biçimi eğitime girmez (secim ne olursa olsun).
      secim=True  : model/adım SEÇİMİ koşusu. Ayrıca:
        dog_cift  : geliştirme (erken durdurma) — NORMALLEŞTİRİLMİŞ SORU
                    düzeyinde %10; aynı sorunun "?"lı/büyük harfli biçimi
                    ayrı yana düşemez. Yanıtları eğitimde başka sorularla
                    geçer (yeniden ifade genellemesini ölçer).
        dog_duz   : dil modeli doğrulaması — CÜMLE düzeyinde %10; bu cümleler
                    ne düzyazıda ne herhangi bir yanıtta eğitime girer
                    (ezber / genelleme farkını ölçer).
      secim=False : dağıtım koşusu: sınama dışındaki her şeyle eğitilir;
                    adım sayısı seçim koşusunun en iyi noktasıdır."""
    r = random.Random(tohum)
    kayitlar = doktrin_oku()
    ciftler = ciftler_kur(kayitlar, r)
    duz = duzyazi_kur(kayitlar)
    test = sinama_kumesi()
    test_n = {norm(q) for q, _ in test}
    ciftler = [c for c in ciftler if norm(c[0]) not in test_n]
    dog_cift, dog_duz = [], []
    if secim:
        r2 = random.Random(tohum + 1)
        qs = sorted({norm(q) for q, _ in ciftler})
        r2.shuffle(qs)
        dev_n = set(qs[: max(4, len(qs) // 10)])
        dog_cift = sorted({c for c in ciftler if norm(c[0]) in dev_n})
        ciftler = [c for c in ciftler if norm(c[0]) not in dev_n]
        # dil modeli doğrulaması: cümle düzeyinde
        tum = sorted({c for b in duz for c in cumleler(b)})
        r2.shuffle(tum)
        ayri = set(tum[: max(10, int(len(tum) * 0.10))])
        # YAKIN KOPYALAR: doktrin kayıtları V15'in yeniden yazımıdır; ayrılan
        # cümleyle bir sözcük 8'lisini paylaşan HER cümle (düzyazıda ya da
        # yanıtta) eğitimden çıkar — yoksa doğrulama metni eğitimde kalır.
        a8 = set()
        for c in ayri:
            a8 |= sekizliler(c)
        yakin = lambda t: bool(sekizliler(t) & a8)
        yeni_duz = []
        for b in duz:
            kalan = [c for c in cumleler(b) if c not in ayri and not yakin(c)]
            if kalan:
                yeni_duz.append(" ".join(kalan))
        duz = yeni_duz
        ayri_n = [norm(c) for c in ayri]
        # yanıtı (kısaltılmış da olsa) ayrılan cümleden ya da yakın kopyasından gelen çift eğitime girmez
        def ayrilmis(y):
            yn = norm(y)
            return yakin(y) or any(yn and (yn == a or a.startswith(yn) or yn.startswith(a)) for a in ayri_n)
        ciftler = [c for c in ciftler if not ayrilmis(c[1])]
        dog_cift = [c for c in dog_cift if not ayrilmis(c[1])]
        dog_duz = [c.encode("utf-8") for c in sorted(ayri)]

    def cift_bayt(c):
        return f"S: {c[0]}\nU: {c[1]}\n".encode("utf-8")

    return {
        "egit_duz": [b.encode("utf-8") for b in duz], "dog_duz": dog_duz,
        "egit_cift": [cift_bayt(c) for c in ciftler],
        "dog_cift": [cift_bayt(c) for c in dog_cift],
        "sinama_cift": [cift_bayt(c) for c in test],
        "doktrin_sayi": len(kayitlar),
    }


def sozluk_kur(d):
    """Derlemde geçen baytlar. C motoru baytı bayt eşler; bu yüzden
    sözlük UNICODE HARF değil BAYT listesidir (eski eğiticinin hatası:
    harf listesi yazıyordu, ÖTÜKEN'de Türkçe harfler kayıyordu)."""
    bs = set()
    for grup in ("egit_duz", "dog_duz", "egit_cift", "dog_cift", "sinama_cift"):
        for b in d.get(grup, []):
            bs.update(b)
    bs.add(ord("\n"))
    bs.discard(0)
    return bytes(sorted(bs))


if __name__ == "__main__":
    d = derlem_kur(secim="--secim" in sys.argv)
    soz = sozluk_kur(d)
    top = lambda g: sum(len(b) for b in d[g])
    print(f"doktrin kaydı: {d['doktrin_sayi']}")
    print(f"düzyazı: eğitim {top('egit_duz')} bayt / doğrulama {top('dog_duz')} bayt")
    print(f"çift: eğitim {len(d['egit_cift'])} ({top('egit_cift')} bayt) / doğrulama {len(d['dog_cift'])}")
    print(f"sözlük: {len(soz)} bayt")
    for c in d["egit_cift"][:3]:
        print(" örnek:", c.decode())
