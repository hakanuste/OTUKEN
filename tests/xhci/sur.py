#!/usr/bin/env python3
"""XHCI + USB HID sinamasi: PS/2 denetleyicisi KAPALI (i8042=off), girdi
yalniz USB'den gelir. Klavye (usb-kbd) ve fare/tablet QMP ile surulur.
  sur.py <iso> <disk> <dizin> <tablet|fare>"""
import subprocess, socket, time, os, sys, json
iso, disk, D, kip = sys.argv[1:5]
log, qs = f"{D}/seri.log", f"{D}/qmp.sock"
for f in (log, qs):
    if os.path.exists(f): os.remove(f)
cihaz = "usb-tablet" if kip == "tablet" else "usb-mouse"
p = subprocess.Popen(["qemu-system-x86_64", "-machine", "pc,i8042=off", "-cpu", "max", "-cdrom", iso,
    "-drive", f"file={disk},format=raw,if=ide,index=0", "-device", "qemu-xhci,id=x",
    "-device", "usb-kbd,bus=x.0", "-device", f"{cihaz},bus=x.0", "-m", "1024", "-smp", "2",
    "-vga", "std", "-display", "none", "-serial", f"file:{log}", "-qmp", f"unix:{qs},server,nowait",
    "-no-reboot", "-boot", "d", "-nic", "none"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
def g(): return open(log, 'rb').read().decode('latin-1') if os.path.exists(log) else ""
try:
    for _ in range(240):
        if "Otağ kuruldu".encode().decode('latin-1') in g() or "Ota\xc4\x9f kuruldu" in g(): break
        time.sleep(1)
    time.sleep(4)
    q = socket.socket(socket.AF_UNIX); q.connect(qs); q.settimeout(5); q.recv(65536)
    def cmd(o):
        q.sendall((json.dumps(o) + "\n").encode()); time.sleep(0.15)
        try: q.recv(65536)
        except Exception: pass
    cmd({"execute": "qmp_capabilities"})
    def tus(*adlar, tut=None):
        a = {"keys": [{"type": "qcode", "data": k} for k in adlar]}
        if tut: a["hold-time"] = tut
        cmd({"execute": "send-key", "arguments": a}); time.sleep(0.12)
    # 1) TR-Q: "ağ" + Shift+i (İ) + Shift+ş (Ş) + AltGr+q (@): bilinmeyen komut olarak yankilanir
    for k in ("a", "bracket_left"): tus(k)
    tus("shift", "apostrophe"); tus("shift", "semicolon"); tus("alt_r", "q")
    tus("ret"); time.sleep(1.5)
    # 2) basili tutma: 'z' 1,5 sn -> yineleme
    tus("z", tut=1500); time.sleep(0.5); tus("ret"); time.sleep(1.5)
    # 3) fare: ULGEN tamgasina (rihtim, ~510,650) tikla -> Gorunum uygulamasi acilir
    if kip == "tablet":
        cmd({"execute": "input-send-event", "arguments": {"events": [
            {"type": "abs", "data": {"axis": "x", "value": 510 * 32768 // 1024}},
            {"type": "abs", "data": {"axis": "y", "value": 650 * 32768 // 768}}]}})
    else:   # goreli: ekran ortasindan (512,384) -> (510,650)
        cmd({"execute": "input-send-event", "arguments": {"events": [
            {"type": "rel", "data": {"axis": "x", "value": -2}},
            {"type": "rel", "data": {"axis": "y", "value": 266}}]}})
    time.sleep(1)
    for basili in (True, False):
        cmd({"execute": "input-send-event", "arguments": {"events": [
            {"type": "btn", "data": {"down": basili, "button": "left"}}]}})
        time.sleep(0.3)
    time.sleep(6)
finally:
    p.kill(); p.wait()
