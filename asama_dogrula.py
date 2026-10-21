#!/usr/bin/env python3
"""ÖTÜKEN — yol haritasi asama dogrulayici.
Tek bir tani-modu calistirmasinin ciktisinda HER asamanin kanitini arar."""
import re, sys

ASAMALAR = [
 (20,"Process Manager",        r"process: olusturuldu|surec kimligi"),
 (21,"Syscall ABI",            r"SYSCALL/SYSRET mekanizmasi kuruldu"),
 (22,"Scheduler (preemptive)", r"sched: baslatildi"),
 (23,"Surec adres alanlari",   r"AS-TEST: SUREC IZOLASYONU -> BASARILI"),
 (24,"Userland runtime",       r"MERHABA! Bu yazi GERCEK bir ELF64"),
 (25,"Kabuk 2.0",              r"Kabugu 2\.0"),
 (26,"Surucu mimarisi",        r"driver: toplam AKTIF"),
 (27,"Depolama 2.0 (FAT yaz)", r"YAZMA-TEST: geri okuma.*BASARILI"),
 (28,"USB denetleyici",        r"USB: (XHCI|EHCI|UHCI)"),
 (29,"Ag: DHCP",               r"ULAK-DHCP: ACK alindi"),
 (29,"Ag: DNS",                r"COZULDU"),
 (29,"Ag: soket katmani",      r"soket katmani baslatildi"),

 (30,"Guvenlik (KUT/TORE)",    r"KUT-TEST.*BASARILI"),
 (31,"IPC 2.0 (RPC)",          r"IPC2-TEST.*BASARILI"),
 (32,"Servis yoneticisi",      r"SERVIS-TEST.*BASARILI"),
 (33,"ULGEN compositor",       r"ULGEN"),
 (34,"Uygulama API (pencere)", r"YUZEY|pencere"),
 (37,"KERVAN paket kurulumu",  r"KERVAN: kurulum TAMAM"),
 (37,"KERVAN bozuk paket redi",r"KERVAN.*REDDEDILDI"),
 (38,"Ag hizmetleri",          r"AGHIZMET: hizmetler dinlemede"),
 (39,"UMAY diskten yuklendi",  r"UMAY-HOST.*diskten okundu"),
 (39,"UMAY Ring3'te",          r"\[UMAY\] kullanici alaninda"),
 (40,"UMAY cekirdek API",      r"UMAY-TEST.*BASARILI"),
 (41,"Baski sinamasi",         r"BASKI-TEST.*BASARILI"),
 (41,"Sizinti yok",            r"sizinti suphesi -> YOK"),
 (42,"Surum 1.0 hazir",        r"OTUKEN 1\.0 HAZIR"),
]

def main(yol, ag_yolu=None):
    d = open(yol, 'rb').read().decode('latin1')

    # TCP el sikismasi KANITI, tani modu logunda ARANMAZ: orada dinleyen
    # bir sunucu yoktur ve baglanti dogru sekilde kurulmaz. Gercek kanit,
    # dinleyen sunucuya karsi calistirilan AYRI ag testinin logundadir.
    # (Denetim maddesi 7: sentetik durum makinesi PASS sayilmaz.)
    ek = []
    if ag_yolu:
        try:
            ad = open(ag_yolu, 'rb').read().decode('latin1')
            ek.append((29, "Ag: TCP GERCEK el sikismasi",
                       bool(re.search(r"GERCEK AG handshake tamam", ad))))
        except Exception:
            ek.append((29, "Ag: TCP GERCEK el sikismasi (log yok)", False))
    gec = kal = 0
    for no, ad, desen in ASAMALAR:
        if re.search(desen, d):
            print(f"  [GECTI] {no:2d} {ad}"); gec += 1
        else:
            print(f"  [KALDI] {no:2d} {ad}"); kal += 1
    for no, ad, ok in ek:
        print(f"  [{'GECTI' if ok else 'KALDI'}] {no:2d} {ad}")
        gec += ok; kal += (not ok)
    print(f"\n  asama kaniti: {gec} gecti, {kal} kaldi")
    return 0 if kal == 0 else 1

if __name__ == "__main__":
    tani = sys.argv[1] if len(sys.argv) > 1 else "/tmp/all.log"
    ag   = sys.argv[2] if len(sys.argv) > 2 else None
    sys.exit(main(tani, ag))
