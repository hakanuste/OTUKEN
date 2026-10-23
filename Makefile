CC      = gcc
LD      = ld
ASM     = nasm

CFLAGS  = -mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -nostdlib -Wall -Wextra -Wshadow -Wmissing-prototypes -O2 -Iinclude
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib
ASMFLAGS = -f elf64

BOOT_ASM = boot/boot.asm boot/idt_flush.asm boot/interrupt.asm boot/switch.asm boot/syscall_entry.asm \
           boot/ring3_test.asm boot/ring3_entry.asm boot/ap_trampoline.asm
SRC_C    = src/kernel.c src/idt.c src/pic.c src/fb.c \
           src/keyboard.c src/mouse.c src/gui.c src/serial.c \
           src/disk.c src/fat16.c src/fat16_dizin.c src/pmm.c src/vmm.c src/heap.c \
           src/timer.c src/sched.c src/process.c src/thread.c \
           src/spinlock.c src/mutex.c src/msgqueue.c src/pipe.c src/shm.c \
           src/syscall.c src/tss.c src/ring3.c src/vfs.c src/vfs_fat16.c src/disk_cache.c src/fat32.c src/vfs_fat32.c src/driver.c src/pci.c src/ahci.c src/nvme.c src/ulak.c src/timur.c src/tore.c src/log.c src/elf.c src/tcp.c src/ag.c src/rtc.c src/takvim.c src/bellek.c src/socket.c src/dhcp.c src/dns.c src/kut.c src/ipc2.c src/usb.c src/uhci.c src/iskence.c src/ses.c src/multiboot2.c src/gpt.c src/cpu.c src/smp.c src/gecikme.c src/umay.c src/kararlilik.c src/servis.c src/ulgen.c src/ekran.c src/ekran_sinama.c src/tema.c src/tamga.c src/duvar.c src/simge.c src/tamga_gorsel.c src/orbitron.c src/ezgi.c src/ozan_ses.c src/dede.c src/ulgen_theme.c src/kervan.c src/kervan2.c src/kervan_anahtar.c src/kervan_ornek.c src/aghizmet.c src/sinama147.c src/sinama148.c src/sinama149.c src/sinama_kervan.c src/sinama_uyg.c src/modul.c src/smap.c src/asena.c src/ergenekon.c src/sinama_asena.c src/sinama_dizin.c src/uygulama.c src/user_elf_blob.c src/shell_elf_blob.c

OBJ = $(BOOT_ASM:.asm=.o) $(SRC_C:.c=.o)

KERNEL = otukenos-mini.elf
ISO    = otukenos-mini.iso

all: $(KERNEL)

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# Baslik bagimliliklari: -MMD her .o icin .d uretir. Bu olmadan bir
# baslikta yapi yerlesimi degisince (orn. cpu_yerel_t) eski .o dosyalari
# ESKI ofsetlerle kalir ve bellegi sessizce ezer (SMP adim 5'te yasandi).
%.o: %.c
	$(CC) $(CFLAGS) $(EKCFLAGS) -MMD -MP -c $< -o $@

# 1.4.9: BearSSL basliklarini kullanan cekirdek dosyalari (komut satirindan
# verilen CFLAGS'i ezmemek icin ayri degisken)
src/kervan2.o src/sinama_kervan.o src/ergenekon.o src/syscall.o src/sinama_asena.o src/sinama_uyg.o: EKCFLAGS = -Iucuncu/bearssl/inc

-include $(OBJ:.o=.d)

# 1.4.9: BearSSL'in cekirdek kopyasi (KERVAN imza dogrulamasi: SHA-256 +
# ECDSA P-256). Arsivden yalniz gereken nesneler baglanir. Uyari bayraklari
# atilir (ucuncu taraf kod degistirilmedi).
BRK_CFLAGS = $(filter-out -W%,$(CFLAGS)) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Iucuncu/bearssl/inc \
             -Iucuncu/bearssl/src -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_RAND=0 \
             -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_POWER8=0 -DBR_INT128=1
BRK_SRC = $(shell find ucuncu/bearssl/src -name '*.c' | sort)
BRK_OBJ = $(patsubst ucuncu/bearssl/src/%.c,cekirdek_br/%.o,$(BRK_SRC))
cekirdek_br/%.o: ucuncu/bearssl/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(BRK_CFLAGS) -c $< -o $@
cekirdek_br/bearssl.a: $(BRK_OBJ)
	@rm -f $@; ar rcs $@ $(BRK_OBJ)

$(KERNEL): $(OBJ) linker.ld cekirdek_br/bearssl.a
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ) cekirdek_br/bearssl.a

iso: $(KERNEL) ergenekon.krv
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/otukenos-mini.elf
	cp ergenekon.krv iso/boot/ergenekon.krv
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso 2>/dev/null

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(KERNEL) $(ISO) userprog/*.uo userprog/bearssl.a
	rm -rf iso/boot/otukenos-mini.elf iso/boot/grub/grub.cfg iso/boot/ergenekon.krv ergenekon.krv ergenekon_kaynak

# --- Kullanici programlari (Ring3) ---
# Kabuk cekirdege gomulur (src/shell_elf_blob.c); UMAY diskte UMAY.ELF.
# Bayraklar ve baglama sirasi (crt0 -> program -> ulib) mevcut ikilileri
# BAYT BAYT yeniden uretir. hello (src/user_elf_blob.c) kapsam disi:
# eski bir baglayici betigiyle uretilmis, yeniden uretilemiyor.
UCC    = gcc
UCFLAGS = -ffreestanding -fno-pie -nostdlib -fno-stack-protector -fno-builtin \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=large -O2

userprog/%.uo: userprog/%.c userprog/ulib.h
	$(UCC) $(UCFLAGS) -c $< -o $@

userprog/shell.elf: userprog/crt0.uo userprog/shell.uo userprog/ulib.uo userprog/shell.ld
	$(LD) -m elf_x86_64 -T userprog/shell.ld -nostdlib -o $@ userprog/crt0.uo userprog/shell.uo userprog/ulib.uo


# 1.4.8: masaustu uygulamalari (diskte, KENDI adres alanlarinda kosar;
# src/uygulama.c yukler). ulib.c'ye dokunulmaz: ekler ulib_uyg.c'de.
UYG_ELF = userprog/sistem.elf userprog/gorunum.elf userprog/tulpar.elf userprog/kervan.elf \
          userprog/ulak.elf userprog/dede.elf userprog/toreg.elf userprog/timur.elf userprog/kut.elf \
          userprog/asena.elf userprog/ergenekon.elf userprog/tamga.elf
userprog/sistem.uo userprog/gorunum.uo userprog/kervan.uo userprog/ulib_uyg.uo: userprog/uyg.h
# (1.4.9: bu genel kural onceki commit'te yanlislikla silinmisti; ELF'ler
# onceden derli oldugu icin fark edilmedi. Temiz agacta uygulama-disk kirilirdi.)
userprog/%.elf: userprog/crt0.uo userprog/%.uo userprog/ulib.uo userprog/ulib_uyg.uo userprog/user.ld
	$(LD) -m elf_x86_64 -T userprog/user.ld -nostdlib -o $@ userprog/crt0.uo userprog/$*.uo userprog/ulib.uo userprog/ulib_uyg.uo
# 1.4.9: ag uygulamalari (HTTP istemcisi ortak) ve TLS (BearSSL 0.6, ucuncu/bearssl)
BR_KOK   = ucuncu/bearssl
BR_CFLAGS = $(UCFLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -I$(BR_KOK)/inc -I$(BR_KOK)/src -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 \
            -DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 \
            -DBR_POWER8=0 -DBR_INT128=1
BR_SRC   = $(shell find $(BR_KOK)/src -name '*.c' | sort)
BR_OBJ   = $(patsubst $(BR_KOK)/src/%.c,userprog/bearssl/%.uo,$(BR_SRC))
userprog/bearssl/%.uo: $(BR_KOK)/src/%.c
	@mkdir -p $(dir $@)
	$(UCC) $(BR_CFLAGS) -c $< -o $@
userprog/bearssl.a: $(BR_OBJ)
	rm -f $@; ar rcs $@ $(BR_OBJ)
userprog/tls.uo: userprog/tls.c userprog/http.h userprog/uyg.h
	$(UCC) $(UCFLAGS) -I$(BR_KOK)/inc -c $< -o $@
userprog/tls_kokler.uo: userprog/tls_kokler.c
	$(UCC) $(UCFLAGS) -I$(BR_KOK)/inc -c $< -o $@
# 1.4.9: sinama paketi uygulamasi (diske KONMAZ; tests/ag paketler, KERVAN agdan kurar)
userprog/merhaba.elf: userprog/crt0.uo userprog/merhaba.uo userprog/ulib.uo userprog/ulib_uyg.uo userprog/user.ld
	$(LD) -m elf_x86_64 -T userprog/user.ld -nostdlib -o $@ userprog/crt0.uo userprog/merhaba.uo userprog/ulib.uo userprog/ulib_uyg.uo
userprog/tamga.uo: userprog/tamga.c userprog/uyg.h
	$(UCC) $(UCFLAGS) -I$(BR_KOK)/inc -c $< -o $@
userprog/tamga.elf: userprog/crt0.uo userprog/tamga.uo userprog/ulib.uo userprog/ulib_uyg.uo userprog/tls_kokler.uo userprog/bearssl.a userprog/user.ld
	$(LD) -m elf_x86_64 -T userprog/user.ld -nostdlib -o $@ userprog/crt0.uo userprog/tamga.uo userprog/tls_kokler.uo userprog/ulib.uo userprog/ulib_uyg.uo userprog/bearssl.a
userprog/agsina.uo: userprog/agsina.c userprog/http.h userprog/uyg.h
	$(UCC) $(UCFLAGS) -I$(BR_KOK)/inc -c $< -o $@
TLS_UO = userprog/tls.uo userprog/tls_kokler.uo userprog/bearssl.a
AG_UO = userprog/http.uo
# 1.4.9: KERVAN agdan indirir (HTTP/HTTPS istemcisi ve TLS)
userprog/kervan.uo: userprog/kervan.c userprog/uyg.h userprog/http.h
userprog/kervan.elf: userprog/crt0.uo userprog/kervan.uo userprog/ulib.uo userprog/ulib_uyg.uo $(AG_UO) $(TLS_UO) userprog/user.ld
	$(LD) -m elf_x86_64 -T userprog/user.ld -nostdlib -o $@ userprog/crt0.uo userprog/kervan.uo $(AG_UO) userprog/tls.uo userprog/tls_kokler.uo userprog/ulib.uo userprog/ulib_uyg.uo userprog/bearssl.a
userprog/http.uo userprog/tulpar.uo: userprog/uyg.h userprog/http.h userprog/html.h
userprog/html.uo: userprog/html.h userprog/uyg.h
userprog/tulpar.elf: userprog/crt0.uo userprog/tulpar.uo userprog/html.uo userprog/ulib.uo userprog/ulib_uyg.uo $(AG_UO) $(TLS_UO) userprog/user.ld
	$(LD) -m elf_x86_64 -T userprog/user.ld -nostdlib -o $@ userprog/crt0.uo userprog/tulpar.uo userprog/html.uo $(AG_UO) userprog/tls.uo userprog/tls_kokler.uo userprog/ulib.uo userprog/ulib_uyg.uo userprog/bearssl.a
# AGSINA sinama koku her kosuda uretilir: baglama tests/ag/calistir.sh'de.
agsina-nesneleri: userprog/crt0.uo userprog/agsina.uo userprog/http.uo userprog/ulib.uo userprog/ulib_uyg.uo $(TLS_UO)
ucflags-yaz:
	@echo $(UCFLAGS)

uygulama-disk: $(UYG_ELF)
	mcopy -o -i diskfiles.img userprog/sistem.elf ::SISTEM.ELF
	mcopy -o -i diskfiles.img userprog/gorunum.elf ::GORUNUM.ELF
	mcopy -o -i diskfiles.img userprog/tulpar.elf ::TULPAR.ELF
	mcopy -o -i diskfiles.img userprog/kervan.elf ::KERVAN.ELF
	mcopy -o -i diskfiles.img userprog/ulak.elf ::ULAK.ELF
	mcopy -o -i diskfiles.img userprog/dede.elf ::DEDE.ELF
	mcopy -o -i diskfiles.img userprog/toreg.elf ::TORE.ELF
	mcopy -o -i diskfiles.img userprog/timur.elf ::TIMUR.ELF
	mcopy -o -i diskfiles.img userprog/kut.elf ::KUT.ELF
	mcopy -o -i diskfiles.img userprog/asena.elf ::ASENA.ELF
	mcopy -o -i diskfiles.img userprog/ergenekon.elf ::ERGENEKO.ELF
	mcopy -o -i diskfiles.img userprog/tamga.elf ::TAMGA.ELF
	mcopy -o -i diskfiles.img paketler/ORNEK.KRV ::ORNEK.KRV

# 1.4.9: ERGENEKON dizge imaji = fabrika diskinin kok dizini (KERVAN v2 bicimi,
# imzali). Uretim anahtari (anahtarlar/, git disi) yoksa SINAMA anahtariyla
# imzalanir: o imaj yalniz TANI yapisinda kabul edilir.
ERGENEKON_DOSYALAR = OTUKEN.TXT NOTLAR.TXT BELGE1.TXT BELGE2.TXT BELGE3.TXT UMAY.ELF UMAY.US1 \
    SOZLUK.TXT SISTEM.ELF GORUNUM.ELF TULPAR.ELF KERVAN.ELF ORNEK.KRV ULAK.ELF DEDE.ELF TORE.ELF \
    TIMUR.ELF KUT.ELF ASENA.ELF ERGENEKO.ELF TAMGA.ELF
ergenekon.krv: diskfiles.img araclar/kervan_paketle.py
	rm -rf ergenekon_kaynak; mkdir ergenekon_kaynak
	for f in $(ERGENEKON_DOSYALAR); do mcopy -n -i diskfiles.img ::$$f ergenekon_kaynak/$$f || exit 1; done
	if [ -f anahtarlar/kervan_uretim.pem ]; then K=anahtarlar/kervan_uretim.pem; N=0; \
	elif [ -f tests/kervan/sinama_anahtari.pem ]; then K=tests/kervan/sinama_anahtari.pem; N=1; echo "UYARI: uretim anahtari yok, ERGENEKON imaji SINAMA anahtariyla (yalniz TANI)"; \
	else echo "HATA: ERGENEKON imajini imzalamak icin anahtarlar/kervan_uretim.pem gerekli"; exit 1; fi; \
	python3 araclar/kervan_paketle.py paketle --ad otuken-dizge --surum 1.4.9 \
	  --aciklama "OTUKEN dizge imaji (ERGENEKON)" --anahtar $$K --anahtar-no $$N --cikti $@ \
	  $(addprefix ergenekon_kaynak/,$(ERGENEKON_DOSYALAR))

src/shell_elf_blob.c: userprog/shell.elf elf_blob_yap.py
	python3 elf_blob_yap.py $< $@ embedded_shell_elf

# --- UMAY yapay us uygulamasi (umay/) ve US1 modeli (us1/) ---
# Diskteki UMAY.ELF umay/ agacindan uretilir. Model ve sozluk de
# diskten okunur - cekirdek yeniden derlenmeden degistirilebilir.
umay/kov/umay:
	$(MAKE) -C umay

umay-disk: umay/kov/umay
	mcopy -o -i diskfiles.img umay/kov/umay ::UMAY.ELF
	mcopy -o -i diskfiles.img us1/umay.us1 ::UMAY.US1
	mcopy -o -i diskfiles.img us1/sozluk.txt ::SOZLUK.TXT

kullanici: src/shell_elf_blob.c

# UMAY paketi uyumlulugu (kur.sh 'make userland' cagirir)
userland: src/shell_elf_blob.c

.PHONY: all iso clean kullanici userland umay-disk uygulama-disk agsina-nesneleri ucflags-yaz umay/kov/umay
