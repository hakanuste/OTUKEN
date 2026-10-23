#include "process.h"
#include "serial.h"
#include "vmm.h"

/* SMP adim 6: surec tablosu. Kisa islemler -> ozyinelemeli donen kilit. */
#include "spinlock.h"
static ozspin_t surec_kilit = OZSPIN_BASLANGIC;

static process_t process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;

static void serial_hex_p(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    buf[10] = 0;
    serial_write(buf);
}

void process_manager_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) process_table[i].active = 0;
    serial_write("process: yonetici baslatildi\n");
}

static process_t *process_create__ic(const char *name, void (*entry)(void)) {
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!process_table[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        serial_write("process: HATA - surec tablosu dolu\n");
        return 0;
    }

    /* --- Denetim duzeltmesi (CRITICAL) ---
     * Her surec KENDI adres alaninda calisir. Cekirdek bolgeleri
     * (PML4[0] kimlik haritasi ve PML4[511] higher-half) paylasilir;
     * kullanici bolgesi (PML4[1..255]) surece OZELDIR. Boylece iki
     * surec ayni sanal adresi kullanip farkli fiziksel bellege bakar.
     *
     * --- 1.4.7 GUVENLIK: cekirdek adres alani GERI DUSUSU KALDIRILDI ---
     * Eskiden adres alani olusturulamazsa surec CEKIRDEK adres alaninda
     * (cr3=0) calismaya devam ediyordu. Bu, yalitimi sessizce kaldiran
     * bir GUVENLIK SINIRI ihlaliydi: bir surec, cekirdegin ve baska tum
     * sureclerin haritalarini gorebilir hale geliyordu. Artik adres alani
     * ONCE olusturulur; olusturulamazsa surec HIC OLUSTURULMAZ (hata: 0).
     * "Yalitimsiz calistirmaktansa hic calistirma."
     *
     * Sira onemli: ONCE adres alani, SONRA ona BAGLI gorev. Boylece gorev
     * hazir kuyruguna cr3'u atanmis halde girer; hicbir an cekirdek adres
     * alaninda gorunmez (latent yaris da kapanir). */
    uint64_t as = vmm_create_address_space();
    if (as == 0) {
        serial_write("process: HATA - adres alani olusturulamadi, "
                     "surec OLUSTURULMUYOR (yalitim sarttir)\n");
        return 0;
    }

    task_t *t = task_create_adresli(name, entry, as, 1);
    if (!t) {
        vmm_destroy_address_space(as);   /* gorev kurulamadi: alani geri al */
        return 0;
    }

    process_table[slot].address_space = as;
    process_table[slot].pid = next_pid++;
    process_table[slot].task = t;
    process_table[slot].active = 1;
    int i = 0;
    while (name[i] && i < 15) { process_table[slot].name[i] = name[i]; i++; }
    process_table[slot].name[i] = 0;

    serial_write("process: olusturuldu pid=");
    serial_hex_p(process_table[slot].pid);
    serial_write(" isim=");
    serial_write(process_table[slot].name);
    serial_write("\n");

    return &process_table[slot];
}

void process_exit_current(void) {
    task_t *cur = scheduler_current_task();
    /* task_exit GERI DONMEZ: kilit yalniz tablo guncellemesini sarar. */
    ozspin_al(&surec_kilit);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].active && process_table[i].task == cur) {
            serial_write("process: kendi kendine sonlandi pid=");
            serial_hex_p(process_table[i].pid);
            serial_write("\n");
            process_table[i].active = 0;
            break;
        }
    }
    ozspin_birak(&surec_kilit);
    task_exit(); /* ASLA GERI DONMEZ */
}

static int process_destroy__ic(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].active && process_table[i].pid == pid) {
            if (scheduler_terminate_task(process_table[i].task) != 0) {
                serial_write("process: destroy basarisiz (kendisi mi?) pid=");
                serial_hex_p(pid);
                serial_write("\n");
                return -1;
            }
            /* Adres alani goreve aittir ve reap'te (gorev hicbir cekirdekte
             * calismiyorken) yok edilir; burada dogrudan yok etmek gorev
             * baska cekirdekte calisirken sayfa tablolarini serbest
             * birakabiliyordu (Denetim 1.4.6). */
            process_table[i].address_space = 0;
            process_table[i].active = 0;
            serial_write("process: disaridan yok edildi pid=");
            serial_hex_p(pid);
            serial_write("\n");
            return 0;
        }
    }
    serial_write("process: destroy basarisiz (pid bulunamadi)\n");
    return -1;
}

/* Reap bir gorevi serbest birakmadan hemen once cagirir: gorev kendi
 * kendine (task_exit / girisinden donerek) bittiyse surec kaydi etkin ve
 * isaretcisi sarkik kaliyordu; sonraki process_destroy serbest birakilmis
 * bellege yazardi (Denetim 1.4.6). */
void process_gorev_bitti(task_t *t) {
    ozspin_al(&surec_kilit);
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].active && process_table[i].task == t) {
            process_table[i].active = 0;
            process_table[i].address_space = 0;
            process_table[i].task = 0;
        }
    ozspin_birak(&surec_kilit);
}

static process_t *process_find__ic(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].active && process_table[i].pid == pid) return &process_table[i];
    }
    return 0;
}

static int process_count_active__ic(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) if (process_table[i].active) n++;
    return n;
}

/* --- SMP adim 6: kilit sarmalayicisi (islev isaretcisi parametreli; elle) --- */
process_t *process_create(const char *name, void (*entry)(void)) {
    ozspin_al(&surec_kilit);
    process_t *r = process_create__ic(name, entry);
    ozspin_birak(&surec_kilit);
    return r;
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int process_destroy(uint32_t pid) {
    ozspin_al(&surec_kilit);
    int r = process_destroy__ic(pid);      /* yalniz sonlandirma ISTEGI */
    ozspin_birak(&surec_kilit);
    /* 1.4.7: kaynak geri alimi (adres alani, yigin, FD, TORE, IPC) surec
     * kilidi BIRAKILDIKTAN sonra: reap kesmeler kapaliyken ve baska bir
     * donen kilidin icinde KOSMAZ. Gorev TERMINATED oldugundan secilmez ve
     * calisiyor=0 iken reap onu guvenle serbest birakir. */
    if (r == 0) scheduler_reap_terminated();
    return r;
}

process_t *process_find(uint32_t pid) {
    ozspin_al(&surec_kilit);
    process_t *r = process_find__ic(pid);
    ozspin_birak(&surec_kilit);
    return r;
}

int process_count_active(void) {
    ozspin_al(&surec_kilit);
    int r = process_count_active__ic();
    ozspin_birak(&surec_kilit);
    return r;
}
