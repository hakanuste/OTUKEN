CC      = gcc
LD      = ld
ASM     = nasm

CFLAGS  = -mcmodel=kernel -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -nostdlib -Wall -Wextra -O2 -Iinclude
LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib
ASMFLAGS = -f elf64

BOOT_ASM = boot/boot.asm boot/idt_flush.asm boot/interrupt.asm boot/switch.asm boot/syscall_entry.asm \
           boot/ring3_test.asm boot/ring3_entry.asm
SRC_C    = src/kernel.c src/idt.c src/pic.c src/fb.c \
           src/keyboard.c src/mouse.c src/gui.c src/serial.c \
           src/disk.c src/fat16.c src/fat16_dizin.c src/pmm.c src/vmm.c src/heap.c \
           src/timer.c src/sched.c src/process.c src/thread.c \
           src/spinlock.c src/mutex.c src/msgqueue.c src/pipe.c src/shm.c \
           src/syscall.c src/tss.c src/ring3.c src/vfs.c src/vfs_fat16.c src/disk_cache.c src/fat32.c src/vfs_fat32.c src/driver.c src/pci.c src/ahci.c src/nvme.c src/ulak.c src/timur.c src/tore.c src/log.c src/elf.c src/tcp.c src/socket.c src/dhcp.c src/dns.c src/kut.c src/ipc2.c src/usb.c src/uhci.c src/iskence.c src/ses.c src/multiboot2.c src/gpt.c src/umay.c src/kararlilik.c src/servis.c src/ulgen.c src/tema.c src/tamga.c src/duvar.c src/simge.c src/tamga_gorsel.c src/orbitron.c src/ezgi.c src/ozan_ses.c src/dede.c src/ulgen_theme.c src/kervan.c src/kervan_ornek.c src/aghizmet.c src/user_elf_blob.c src/shell_elf_blob.c

OBJ = $(BOOT_ASM:.asm=.o) $(SRC_C:.c=.o)

KERNEL = otukenos-mini.elf
ISO    = otukenos-mini.iso

all: $(KERNEL)

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ)

iso: $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/otukenos-mini.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso 2>/dev/null

clean:
	rm -f $(OBJ) $(KERNEL) $(ISO)
	rm -rf iso/boot/otukenos-mini.elf iso/boot/grub/grub.cfg

.PHONY: all iso clean

# =====================================================================
# Asama 42: kullanici programlari KAYNAKTAN uretilir.
# Onceden shell.elf ve umay.elf agaca elle konmustu; GNU_STACK
# basliklari eksikti ve kaynaktan yeniden uretilemiyorlardi.
# mcmodel=large sart: programlar 0x8000A00000 gibi YUKSEK adreslere
# yuklenir, small model 32 bitlik yer degistirme uretir.
# =====================================================================
UCFLAGS  = -m64 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
           -mno-sse -mno-mmx -mcmodel=large -O2 -Wall -Wextra -Iuserprog
ULDFLAGS = -nostdlib -static -z noexecstack

userprog/%.elf: userprog/%.c userprog/crt0.c userprog/ulib.c userprog/%.ld
	$(CC) $(UCFLAGS) -c userprog/crt0.c -o userprog/crt0.o
	$(CC) $(UCFLAGS) -c userprog/ulib.c -o userprog/ulib.o
	$(CC) $(UCFLAGS) -c $< -o $(basename $<).o
	ld $(ULDFLAGS) -T userprog/$*.ld userprog/crt0.o userprog/ulib.o \
	   $(basename $<).o -o $@

userland: userprog/shell.elf
	@echo "userland: shell.elf uretildi"

blob: userland
	python3 tools/elf_blob.py userprog/shell.elf embedded_shell_elf > src/shell_elf_blob.c
	@echo "blob: gomulu dizi yenilendi"

.PHONY: userland blob
