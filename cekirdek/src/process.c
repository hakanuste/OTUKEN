#include "process.h"
#include "serial.h"
#include "vmm.h"

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

process_t *process_create(const char *name, void (*entry)(void)) {
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!process_table[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        serial_write("process: HATA - surec tablosu dolu\n");
        return 0;
    }

    task_t *t = task_create(name, entry);
    if (!t) return 0;

    /* --- Denetim duzeltmesi (CRITICAL) ---
     * Her surec KENDI adres alaninda calisir. Cekirdek bolgeleri
     * (PML4[0] kimlik haritasi ve PML4[511] higher-half) paylasilir;
     * kullanici bolgesi (PML4[1..255]) surece OZELDIR. Boylece iki
     * surec ayni sanal adresi kullanip farkli fiziksel bellege bakar.
     * Adres alani olusturulamazsa surec cekirdek alaninda calisir
     * (islevsellik korunur, yalitim olmaz - durum loglanir). */
    uint64_t as = vmm_create_address_space();
    if (as != 0) {
        task_adres_alani_ata(t, as);
        process_table[slot].address_space = as;
    } else {
        process_table[slot].address_space = 0;
        serial_write("process: UYARI - adres alani olusturulamadi, "
                     "surec cekirdek alaninda calisacak\n");
    }

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
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].active && process_table[i].task == cur) {
            serial_write("process: kendi kendine sonlandi pid=");
            serial_hex_p(process_table[i].pid);
            serial_write("\n");
            process_table[i].active = 0;
            break;
        }
    }
    task_exit(); /* ASLA GERI DONMEZ */
}

int process_destroy(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].active && process_table[i].pid == pid) {
            if (scheduler_terminate_task(process_table[i].task) != 0) {
                serial_write("process: destroy basarisiz (kendisi mi?) pid=");
                serial_hex_p(pid);
                serial_write("\n");
                return -1;
            }
            if (process_table[i].address_space != 0) {
                vmm_destroy_address_space(process_table[i].address_space);
                process_table[i].address_space = 0;
            }
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

process_t *process_find(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].active && process_table[i].pid == pid) return &process_table[i];
    }
    return 0;
}

int process_count_active(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) if (process_table[i].active) n++;
    return n;
}
