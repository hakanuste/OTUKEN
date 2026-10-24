# =====================================================================
# UMAY — ÖTÜKEN userland yapay us uygulaması
#   make            -> ÖTÜKEN Ring3 ikilisi (kov/umay)
#   make sina       -> ana makinede mantık sınaması
#   make temiz
# ÖTÜKEN ağacına yerleştirirken ULIB/CRT0 yollarını düzelt.
# =====================================================================
CC       ?= gcc
# ÖTÜKEN ağacındaki gerçek yollar (denetimde yer tutucuydu).
OTUKEN   ?= ..
CRT0     ?= $(OTUKEN)/userprog/crt0.o
LDS      ?= umay.ld

# US1 motoru TEK kaynaktan: ../us1/c (eskiden us/ altinda birebir kopya vardi)
US1_DIZIN = ../us1/c
CFLAGS_ORTAK = -std=c11 -Wall -Wextra -Wshadow -Wmissing-prototypes -Iinclude -I$(US1_DIZIN)
# Ring3 kullanıcı kodu: çekirdek yığını yok, kırmızı bölge yok, SSE yok
# mcmodel=large şart: program 0x9000000000'a yüklenir (umay.ld).
CFLAGS_OTUKEN = $(CFLAGS_ORTAK) -DUMAY_OTUKEN -ffreestanding -fno-builtin \
                -fno-stack-protector -fno-pic -mno-red-zone -mno-sse -mno-mmx \
                -mcmodel=large -O2
LDFLAGS_OTUKEN = -nostdlib -static

# 1.5: Dede Korkut sesi (OSES). Kayan noktalı: bu dosyalar SSE ile derlenir
# (çekirdek görev başına FXSAVE saklar). Diğer dosyalar -mno-sse kalır;
# arayüzde float değer geçmez.
OSES_KAYNAK = us/umay_egitim.c $(US1_DIZIN)/us1_egit.c ses/umay_dede_ses.c ses/oses/oses.c ses/oses/oses_mat.c ses/oses/oses_yigin.c ses/oses/oses_ornekle.c \
              ses/oses/turkce.c ses/oses/turkce_tablo.c ses/oses/turkce_kimlik.c ses/oses/turkce_sayi.c
OSES_NESNE  = $(OSES_KAYNAK:.c=.o)
$(OSES_NESNE): CFLAGS_OTUKEN := $(filter-out -mno-sse,$(CFLAGS_OTUKEN)) -msse -msse2 -Ises

KAYNAK = core/umay_str.c core/umay_ortak.c core/umay_bilgi.c core/umay_ozan.c \
         core/umay_bengi.c core/umay_onay.c core/umay_kanal.c core/umay_main.c core/umay_kanit.c \
         alp/otag.c alp/alp_gozcu.c alp/alp_onarici.c alp/alp_bilge.c alp/alp_yazman.c \
         alp/alp_uzman.c alp/arastirma.c us/us_stub.c us/umay_us.c us/umay_us_ipc.c us/umay_us_yerel.c $(US1_DIZIN)/us1.c ses/umay_ses.c gui/umay_ulgen.c \
         $(OSES_KAYNAK)

NESNE_OTUKEN = $(KAYNAK:.c=.o) port/umay_port_otuken.o

all: kov/umay

%.o: %.c
	$(CC) $(CFLAGS_OTUKEN) -c $< -o $@

kov/umay: $(NESNE_OTUKEN)
	@mkdir -p kov
	ld -nostdlib -static -z noexecstack -T $(LDS) $^ -o $@
	@echo "kov/umay hazır — UMAY.ELF olarak imaja kopyalanabilir."


# --- ana makine sınaması (ÖTÜKEN'e gitmez) ---
SINA_KAYNAK = sina/sina.c core/umay_str.c core/umay_ortak.c core/umay_bilgi.c \
              core/umay_ozan.c core/umay_bengi.c core/umay_onay.c core/umay_kanit.c \
              alp/otag.c alp/alp_gozcu.c alp/alp_onarici.c alp/alp_bilge.c \
              alp/alp_yazman.c alp/alp_uzman.c alp/arastirma.c us/us_stub.c us/umay_us.c us/umay_us_ipc.c us/umay_us_yerel.c $(US1_DIZIN)/us1.c ses/umay_ses.c $(OSES_KAYNAK) port/umay_port_host.c

sina:
	@mkdir -p kov
	$(CC) $(CFLAGS_ORTAK) -Ises -O1 -g -o kov/umay_sina $(SINA_KAYNAK)
	./kov/umay_sina

temiz:
	rm -f $(NESNE_OTUKEN) kov/umay kov/umay_sina

.PHONY: all sina temiz
