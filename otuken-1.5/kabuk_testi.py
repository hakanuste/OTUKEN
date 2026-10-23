#!/usr/bin/env python3
"""ÖTÜKEN kabuk oturum testi: gercek klavye girdisiyle komut calistirir.
KABUK_SMP=4 python3 kabuk_testi.py  -> 4 cekirdekte (is dagitimi acik)."""
import subprocess, socket, time, os, sys, re
os.chdir(os.path.dirname(os.path.abspath(__file__)))
DISK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/kt.img"
LOG, SOCK = "/tmp/kt.log", "/tmp/kt.sock"
subprocess.run(["pkill","-9","-f","qemu-system"], capture_output=True); time.sleep(1)
for f in [LOG, SOCK]:
    if os.path.exists(f): os.remove(f)
subprocess.run(["cp","diskfiles.img",DISK])
p = subprocess.Popen(["qemu-system-x86_64","-cpu","max","-cdrom","otukenos-mini.iso",
    "-drive",f"file={DISK},format=raw,if=ide,index=0",
    "-netdev","user,id=n0","-device","e1000,netdev=n0","-m","512","-smp",os.environ.get("KABUK_SMP","1"),
    "-vga","std","-display","none","-serial",f"file:{LOG}",
    "-monitor",f"unix:{SOCK},server,nowait","-no-reboot","-boot","d"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(60):
    if os.path.exists(SOCK): break
    time.sleep(0.2)
time.sleep(22)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(12)
s.connect(SOCK); time.sleep(0.5); s.recv(65536)
TRQ = {'i':'apostrophe','ı':'i',' ':'spc','.':'slash'}
def k(x, w=0.20):
    s.sendall(("sendkey "+x+"\n").encode()); time.sleep(w)
    try: s.recv(65536)
    except Exception: pass
def komut(t, b=7.0):
    for ch in t: k(TRQ.get(ch, ch))
    k("ret", b)
# Isinma: ilk tuslar kabuk okuma dongusune girmeden gelip kaybolabilir
# (test duzeneginin kusuru, sistemin degil).
k("ret", 3.0)
komut("kimlik"); komut("listele", 8.0); komut("umay durum", 8.0)
# KLAVYE ODAGI: Tab -> UMAY penceresi; yazilan UMAY'a gider, kabuga DEGIL.
# Sonra Tab -> kabuk; kabuk yeniden calisir.
k("tab", 1.0); komut("merhaba", 8.0)
k("tab", 1.0); komut("kimlik", 6.0)
p.kill(); time.sleep(0.5)
d = open(LOG,'rb').read().decode('latin1')
kal = 0
# UMAY paketiyle yeni UMAY geldi: yaniti artik "UMAY 1.0 HAZIR" degil
# (orn. "Ocagim tuter, gozum aciktir."). Sinanan sey IPC gidis-donusu:
# kabuk soruyu yollar, UMAY SORANA (sender_port) yanitlar.
for ad, desen in [("kimlik (SYS_GETPID)", r"Surec kimligi \(PID\): \d+"),
                  ("listele (SYS_LISTDIR, gorev-basina tampon)", r"Dizin:"),
                  ("dosya ogesi", r"OTUKEN\.TXT"),
                  ("umay (SYS_IPC_AL, gorev-basina tampon)", r"UMAY yaniti: \S")]:
    ok = bool(re.search(desen, d)); kal += (not ok)
    print(f"  [{'GECTI' if ok else 'KALDI'}] {ad}")
# Odak: pencereye gecis sonrasi 'merhaba' UMAY'a gitti (selam yaniti),
# kabuga gitmedi; kabuga donunce 'kimlik' ikinci kez calisti.
i_p = d.find("KLAVYE: odak -> pencere"); i_k = d.find("KLAVYE: odak -> kabuk", max(i_p, 0))
ara = d[i_p:i_k] if (i_p >= 0 and i_k > i_p) else ""
for ad, ok in [("odak Tab ile pencereye ve geri gecti", i_p >= 0 and i_k > i_p),
               ("pencerede yazilan UMAY'a gitti (selam yaniti)", "Esenlik olsun" in ara),
               ("pencerede yazilan kabuga GITMEDI", "Bilinmeyen komut: merhaba" not in d),
               ("kabuk odak donunce yine calisiyor", len(re.findall(r"Surec kimligi \(PID\): \d+", d)) >= 2)]:
    kal += (not ok)
    print(f"  [{'GECTI' if ok else 'KALDI'}] {ad}")
pn = d.count("PANIGI") + d.count("PAGE FAULT"); kal += (pn > 0)
print(f"  [{'GECTI' if pn == 0 else 'KALDI'}] panik yok ({pn})")
sys.exit(1 if kal else 0)
