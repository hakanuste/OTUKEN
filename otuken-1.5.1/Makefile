CC      = gcc
LD      = ld
ASM     = nasm

CFLAGS  = -mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -nostdlib -Wall -Wextra -O2 -Iinclude
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib
ASMFLAGS = -f elf64

BOOT_ASM = boot/boot.asm boot/idt_flush.asm boot/interrupt.asm boot/switch.asm boot/syscall_entry.asm \
           boot/ring3_test.asm boot/ring3_entry.asm boot/ap_trampoline.asm
SRC_C    = src/kernel.c src/idt.c src/pic.c src/fb.c \
           src/keyboard.c src/mouse.c src/gui.c src/serial.c \
           src/disk.c src/fat16.c src/fat16_dizin.c src/pmm.c src/vmm.c src/heap.c \
           src/timer.c src/sched.c src/process.c src/thread.c \
           src/spinlock.c src/mutex.c src/msgqueue.c src/pipe.c src/shm.c \
           src/syscall.c src/tss.c src/ring3.c src/vfs.c src/vfs_fat16.c src/disk_cache.c src/fat32.c src/vfs_fat32.c src/driver.c src/pci.c src/ahci.c src/nvme.c src/ulak.c src/timur.c src/tore.c src/log.c src/elf.c src/tcp.c src/socket.c src/dhcp.c src/dns.c src/kut.c src/ipc2.c src/usb.c src/uhci.c src/xhci.c src/iskence.c src/ses.c src/multiboot2.c src/gpt.c src/cpu.c src/smp.c src/gecikme.c src/umay.c src/kararlilik.c src/servis.c src/ulgen.c src/tema.c src/tamga.c src/duvar.c src/simge.c src/tamga_gorsel.c src/orbitron.c src/ezgi.c src/ozan_ses.c src/dede.c src/ulgen_theme.c src/kervan.c src/kervan_ornek.c src/aghizmet.c src/user_elf_blob.c src/shell_elf_blob.c

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
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ)

iso: $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/otukenos-mini.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso 2>/dev/null

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(KERNEL) $(ISO) userprog/*.uo
	rm -rf iso/boot/otukenos-mini.elf iso/boot/grub/grub.cfg

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

.PHONY: all iso clean kullanici userland umay-disk umay/kov/umay
