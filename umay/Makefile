# =====================================================================
# UMAY — tek basina calisan surum
#
# ÖTÜKEN'e bagli degildir: ne cekirdek kaynagi, ne baglayici betigi,
# ne de ÖTÜKEN'e ozgu bir kosullu derleme kalmistir. Sira bir Linux
# programidir.
#
#   make          -> kov/umay          (program)
#   make sina     -> kov/umay_sina     (sinama takimi) ve kosar
#   make kur      -> ~/.local'a kurar
#   make temiz
# =====================================================================
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?=

KAYNAK = core/umay_str.c core/umay_ortak.c core/umay_bilgi.c \
         core/umay_ozan.c core/umay_bengi.c core/umay_onay.c \
         core/umay_kanal.c core/umay_main.c \
         alp/otag.c alp/alp_gozcu.c alp/alp_onarici.c alp/alp_bilge.c \
         alp/alp_yazman.c alp/alp_uzman.c \
         us/us_stub.c us/umay_us.c us/umay_us_ipc.c us/umay_us_yerel.c \
         us/us1.c us/umay_us_ollama.c \
         ses/umay_ses.c ses/umay_piper.c \
         gui/umay_gui_konsol.c \
         port/umay_port_host.c \
         kabuk/umay_cli.c

SINA_KAYNAK = $(filter-out kabuk/umay_cli.c,$(KAYNAK)) sina/sina.c

# BASLIK BAGIMLILIKLARI: basliklar degisince de yeniden derlenmeli.
# Yoksa "make" sessizce eski ikiliyi birakir. Nitekim birakti: indeks
# siniri 1200'den 4096'ya cikarildi, make hicbir sey yapmadi, kitaplik
# yine 1200'de doldu. Yanlis olcum, kaybedilen zaman.
BASLIK = $(wildcard include/*.h)

all: kov/umay

kov/umay: $(KAYNAK) $(BASLIK)
	@mkdir -p kov
	$(CC) $(CFLAGS) -DUMAY_SES_ACIK=1 -o $@ $(KAYNAK) $(LDFLAGS)
	@echo "kov/umay hazir — ./kov/umay ile calistirin."

sina: kov/umay_sina
	./kov/umay_sina

kov/umay_sina: $(SINA_KAYNAK) $(BASLIK)
	@mkdir -p kov
	$(CC) $(CFLAGS) -DUMAY_SINAMA -o $@ $(SINA_KAYNAK) $(LDFLAGS)

ONEK ?= $(HOME)/.local
# Kurulumda veri yeri IKILIYE GOMULUR: ozel bir onekle kurulunca
# program verisini bulamiyor ve sessizce stub motora dusuyordu.
kur: $(KAYNAK) $(BASLIK)
	@mkdir -p kov
	$(CC) $(CFLAGS) -DUMAY_SES_ACIK=1 \
	      -DUMAY_KUT_ONEK='"$(ONEK)/share/umay/kut"' \
	      -o kov/umay $(KAYNAK) $(LDFLAGS)
	install -d $(ONEK)/bin $(ONEK)/share/umay
	install -m755 kov/umay $(ONEK)/bin/umay
	cp -r kut $(ONEK)/share/umay/
	@echo "kuruldu: $(ONEK)/bin/umay"
	@echo "veri    : $(ONEK)/share/umay/kut"
	@case ":$$PATH:" in *":$(ONEK)/bin:"*) ;; \
	  *) echo "UYARI: $(ONEK)/bin PATH'te degil." ;; esac

# --- ÖTÜKEN dikisi ---
# UMAY, ÖTÜKEN bittiginde buradan baglanir. Degisen TEK sey port
# katmanidir: ana bilgisayar portu yerine ÖTÜKEN portu derlenir.
# Cekirdekte (core/) tek bir kosullu derleme YOKTUR.
OTUKEN_KAYNAK = $(filter-out port/umay_port_host.c ses/umay_piper.c \
                             us/umay_us_ollama.c \
                             gui/umay_gui_konsol.c kabuk/umay_cli.c,$(KAYNAK)) \
                port/umay_port_otuken.c gui/umay_ulgen.c

# Dikisin SAGLAM oldugunu kanitlar: ÖTÜKEN portu ve cekirdek, ÖTÜKEN
# kipinde derleniyor mu? Baglama yapilmaz (cekirdek ortami gerekir),
# yalniz derleme denetlenir. ÖTÜKEN'e dokunmadan kosulabilir.
otuken-denetle:
	@for d in $(OTUKEN_KAYNAK); do \
	  $(CC) -std=c11 -Wall -Wextra -Iinclude -DUMAY_OTUKEN \
	        -ffreestanding -fno-stack-protector -fsyntax-only $$d \
	    || exit 1; \
	done
	@echo "ÖTÜKEN dikisi saglam: $(words $(OTUKEN_KAYNAK)) dosya derlendi."

# Gercek ÖTÜKEN yapimi: cekirdek agacindaki ulib ve baglayici betigi
# gerekir. OT=... ile ÖTÜKEN kokunu verin.
OT ?= ../ot
otuken:
	@test -d $(OT) || { echo "ÖTÜKEN agaci yok: OT=$(OT)"; exit 1; }
	@mkdir -p kov
	$(CC) -std=c11 -Wall -Wextra -O2 -Iinclude -DUMAY_OTUKEN \
	      -ffreestanding -fno-stack-protector -fno-pie -mcmodel=large \
	      -nostdlib -T umay.ld -o kov/umay.elf $(OTUKEN_KAYNAK)
	@echo "kov/umay.elf hazir — ÖTÜKEN diskine UMAY.ELF olarak koyun."

ses:
	./ses/piper_kur.sh

kaldir:
	rm -f $(ONEK)/bin/umay
	rm -rf $(ONEK)/share/umay
	@echo "kaldirildi: $(ONEK)/bin/umay"

temiz:
	rm -f kov/umay kov/umay_sina kov/umay.elf

.PHONY: all sina kur kaldir ses otuken otuken-denetle temiz
