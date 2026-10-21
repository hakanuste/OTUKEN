CC  = aarch64-linux-gnu-gcc
LD  = aarch64-linux-gnu-ld
OC  = aarch64-linux-gnu-objcopy

CFLAGS = -ffreestanding -nostdlib -mgeneral-regs-only -O2 -Wall -Wextra
LDFLAGS = -T linker.ld -nostdlib

OBJ = boot/boot.o src/uart.o src/cekirdek.o

all: otuken-arm64.img

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

otuken-arm64.elf: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

otuken-arm64.img: otuken-arm64.elf
	$(OC) -O binary $< $@

clean:
	rm -f $(OBJ) otuken-arm64.elf otuken-arm64.img

.PHONY: all clean
