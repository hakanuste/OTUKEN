#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KITAPLIK KUR — ham arsivi UMAY'in okuyabilecegi kitapliga cevirir.

NE YAPAR
  1. Klasor adindan ALAN cikarir (12 uzman alanindan birine esler).
  2. PDF cikarim gurultusunu temizler: "--- SAYFA 1 ---" isaretcileri,
     satir sonu tirelemeleri, kirik bosluklar.
  3. Belgeyi PARCALARA boler. UMAY'da bir kayit en cok BILGI_ICERIK_MAX
     (3072 bayt) tutar; 348 KB'lik bir belge ilk sayfasina kirpilirdi.
     Bolme, paragraf ve cumle sinirlarinda yapilir.
  4. Her parcanin basina KUNYE yazar:
         #kategori: otaci
         #baslik:   Temel Ilk Yardim (3/altmis dort)
         #etiket:   ilkyardim,travma
         #kaynak:   01_Saglik-Bakanligi-Ilk-Yardim.txt
     Kunye yoksa UMAY belgeyi "ortak" havuza atar ve 12 uzmanin
     hicbiri goremez.
  5. Dosya adlarini 8.3 uyumlu tutar (ÖTÜKEN'in FAT16 birimi icin)
     ve 32 bayttan kisa birakir (dizin girdisi ABI siniri).

NE YAPMAZ
  - Onay bekleyen arsive DOKUNMAZ. Oradaki ham dokum insan onayindan
    gecmeden kitapliga girmez; arsivin kendi MANIFEST'i de boyle diyor.
  - Kisisel saglik klasorunu hic okumaz.
"""
import os, re, sys, unicodedata

# Klasor adindaki anahtar sozcuk -> UMAY alan kategorisi.
# UMAY'in kategorileri: akca yer-su kun demir tulga otaci yargu
#                       balbal alpagu kopuz oyun yazit ortak
ESLEME = [
    ('mimari',      'balbal'), ('kerpic',   'balbal'),
    ('sarnic',      'balbal'), ('yangin',   'balbal'),
    ('biyodizel',   'tulga'),  ('mercedes', 'tulga'),
    ('musso',       'tulga'),  ('mekanik',  'tulga'),
    ('telsiz',      'demir'),  ('haberlesme','demir'),
    ('carklar',     'demir'),  ('tork',     'demir'),
    ('sera',        'yer-su'), ('bitki',    'yer-su'),
    ('su_aritma',   'yer-su'), ('kompost',  'yer-su'),
    ('kumes',       'yer-su'), ('kanatli',  'yer-su'),
    ('radar',       'alpagu'), ('kamera',   'alpagu'),
    ('guvenlik',    'alpagu'), ('siber',    'alpagu'),
    ('tip',         'otaci'),  ('ilk_yardim','otaci'), ('otacilik', 'otaci'),
    ('sahne',       'oyun'),   ('folklor',  'oyun'),
    ('kultur',      'yazit'),  ('tarih',    'yazit'),
    ('cografya',    'yazit'),  ('dil',      'yazit'),
    ('haritacilik', 'yazit'),  ('gis',      'yazit'),
    ('enerji',      'kun'),    ('solar',    'kun'), ('gunes', 'kun'),
    ('hukuk',       'yargu'),  ('mevzuat',  'yargu'),
    ('muzik',       'kopuz'),  ('beste',    'kopuz'),
    ('akce',        'akca'),   ('borsa',    'akca'), ('ekonomi', 'akca'),
]
ATLA = ('00_onay_bekleyenler', '99_kisisel', 'saglik_korumali')
PARCA_AZAMI = 2600          # bayt; UMAY'da sinir 3072, kunyeye pay birakilir


def alan_bul(klasor):
    d = klasor.lower()
    for anahtar, kat in ESLEME:
        if anahtar in d:
            return kat
    return 'ortak'


# Site/kaynak damgalari. DAR tutulur: "Installation Notice" gibi 356 kez
# tekrar eden satirlar gurultu DEGIL, el kitabinin gercek bolum
# basliklaridir. Yalnizca sayfa basina basilan damga silinir.
DAMGA = [
    re.compile(r'^\s*Downloaded from www\.Manualslib\.com.*$', re.M | re.I),
    re.compile(r'^\s*Manualslib\.com - The Global Manuals Library.*$', re.M | re.I),
    re.compile(r'^\s*Manuals / Brands / .*$', re.M),
]


def temizle(metin):
    """PDF cikarim gurultusunu siler."""
    m = metin
    for d in DAMGA:
        m = d.sub('', m)
    m = re.sub(r'-{2,}\s*SAYFA\s*\d+\s*-{2,}', '\n', m)   # sayfa isaretcisi
    m = re.sub(r'(\w)-\n(\w)', r'\1\2', m)                     # satir sonu tiresi
    m = re.sub(r'[ \t]+', ' ', m)
    m = re.sub(r'\n{3,}', '\n\n', m)
    m = re.sub(r'^\s*\d{1,4}\s*$', '', m, flags=re.M)          # yalniz sayfa no
    return m.strip()


def parcala(metin, azami=PARCA_AZAMI):
    """Paragraf, olmazsa cumle sinirinda boler."""
    parcalar, simdi = [], ''
    for p in re.split(r'\n\s*\n', metin):
        p = p.strip()
        if not p:
            continue
        if len(simdi) + len(p) + 2 <= azami:
            simdi = (simdi + '\n\n' + p) if simdi else p
            continue
        if simdi:
            parcalar.append(simdi); simdi = ''
        while len(p) > azami:
            kes = p.rfind('. ', 0, azami)
            if kes < azami // 3:
                kes = p.rfind(' ', 0, azami)
            if kes <= 0:
                kes = azami
            parcalar.append(p[:kes + 1].strip())
            p = p[kes + 1:].lstrip()
        simdi = p
    if simdi:
        parcalar.append(simdi)
    return parcalar


def sadelestir(s):
    """Baslik icin: Turkce harfleri koru, gurultuyu at."""
    s = re.sub(r'^\d+[_\-]', '', s)
    s = re.sub(r'\.(txt|md)$', '', s, flags=re.I)
    s = s.replace('_', ' ').replace('-', ' ')
    return re.sub(r'\s+', ' ', s).strip()[:70]


def etiket_uret(klasor, ad):
    kelimeler = re.findall(r'[a-zçğıöşü]{4,}',
                           (klasor + ' ' + ad).lower()
                           .replace('_', ' ').replace('-', ' '))
    atil = {'sayfa', 'index', 've', 'ile', 'icin', 'web', 'kitabi', 'rehber'}
    out = []
    for k in kelimeler:
        k = unicodedata.normalize('NFC', k)
        if k in atil or k.isdigit() or k in out:
            continue
        out.append(k)
        if len(out) == 4:
            break
    return ','.join(out)


def kos(kaynak, hedef, yaz=True):
    sayac = {}
    toplam_parca = 0
    toplam_belge = 0
    rapor = []

    klasorler = sorted(d for d in os.listdir(kaynak)
                       if os.path.isdir(os.path.join(kaynak, d)))
    for bno, klasor in enumerate(klasorler, 1):
        if any(a in klasor.lower() for a in ATLA):
            rapor.append(('ATLANDI', klasor, 0, 0, '-'))
            continue
        kat = alan_bul(klasor)
        dosyalar = sorted(f for f in os.listdir(os.path.join(kaynak, klasor))
                          if f.lower().endswith(('.txt', '.md')))
        if not dosyalar:
            rapor.append(('BOS', klasor, 0, 0, kat))
            continue

        kp = 0
        for dno, dosya in enumerate(dosyalar, 1):
            yol = os.path.join(kaynak, klasor, dosya)
            try:
                ham = open(yol, encoding='utf-8').read()
            except Exception:
                continue
            metin = temizle(ham)
            if not metin:
                continue
            parcalar = parcala(metin)
            baslik = sadelestir(dosya)
            etiket = etiket_uret(klasor, dosya)
            toplam_belge += 1

            for i, parca in enumerate(parcalar, 1):
                kp += 1
                toplam_parca += 1
                # 8.3 uyumlu, 32 bayttan kisa: b<klasor><belge><parca>
                ad = 'b%02d%02d%03d.txt' % (bno, dno, i)
                kunye = ('#kategori: %s\n#baslik: %s (%d/%d)\n'
                         '#etiket: %s\n#kaynak: %s\n\n'
                         % (kat, baslik, i, len(parcalar), etiket, dosya))
                if yaz:
                    dz = os.path.join(hedef, kat)
                    os.makedirs(dz, exist_ok=True)
                    with open(os.path.join(dz, ad), 'w', encoding='utf-8') as f:
                        f.write(kunye + parca + '\n')
        sayac[kat] = sayac.get(kat, 0) + kp
        rapor.append(('TAMAM', klasor, len(dosyalar), kp, kat))

    return rapor, sayac, toplam_belge, toplam_parca


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('kullanim: kitaplik_kur.py <ham_arsiv_dizini> <kut_dizini>')
        sys.exit(2)
    kaynak, hedef = sys.argv[1], sys.argv[2]
    rapor, sayac, belge, parca = kos(kaynak, hedef)

    print('%-10s %-46s %5s %6s  %s' % ('durum', 'klasor', 'belge', 'parca', 'alan'))
    for d, k, b, p, kat in rapor:
        print('%-10s %-46s %5d %6d  %s' % (d, k[:46], b, p, kat))
    print()
    print('%d belge -> %d parca' % (belge, parca))
    print('alan dagilimi:', ', '.join('%s=%d' % kv for kv in sorted(sayac.items())))
