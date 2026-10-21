#include "ipc2.h"
#include "sched.h"
#include "timer.h"
#include "log.h"
#include "serial.h"
#include "spinlock.h"

#define IPC_QUEUE_DEPTH 8

typedef struct {
    int      used;
    char     name[IPC_PORT_NAME_LEN];
    ipc_message_t queue[IPC_QUEUE_DEPTH];
    int      head, tail, count;
    uint32_t owner_task;
} ipc_port_t;

static ipc_port_t ports[IPC_MAX_PORTS];

/* --- SMP: port tablosu ve kuyruklar PAYLASILAN veridir ---
 * Port acma yuvasi tahsis eder; gonderme/alma kuyruk sayaclarini
 * degistirir. Iki cekirdek ayni anda girerse ayni yuvayi kapabilir
 * ya da sayaclar bozulabilir. Islemler kisa oldugu icin modul
 * duzeyinde TEK kilit yeterli ve basit. */
static spinlock_t ipc_kilit;
static int ipc_kilit_hazir = 0;

static inline uint64_t ipc_kilitle(void) {
    if (!ipc_kilit_hazir) { spinlock_init(&ipc_kilit); ipc_kilit_hazir = 1; }
    return spin_lock_irqsave(&ipc_kilit);
}
static inline void ipc_kilit_ac(uint64_t f) {
    spin_unlock_irqrestore(&ipc_kilit, f);
}
static uint32_t stat_sent = 0, stat_recv = 0;
static uint32_t seq_counter = 1;

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }

static int name_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

void ipc2_init(void) {
    for (int i = 0; i < IPC_MAX_PORTS; i++) ports[i].used = 0;
    stat_sent = stat_recv = 0;
    seq_counter = 1;
    log_write(LOG_BILGI, "IPC2", "servis iletisim katmani baslatildi");
}

int ipc_port_create(const char *name) {
    uint64_t _ik = ipc_kilitle();
    for (int i = 0; i < IPC_MAX_PORTS; i++) {
        if (ports[i].used && name_eq(ports[i].name, name)) {
            ipc_kilit_ac(_ik);
            serial_write("IPC2: port ismi ZATEN kullanimda: ");
            serial_write(name);
            serial_write("\n");
            return -1;
        }
    }
    for (int i = 0; i < IPC_MAX_PORTS; i++) {
        if (ports[i].used) continue;
        ports[i].used = 1;
        ports[i].head = ports[i].tail = ports[i].count = 0;
        task_t *t = scheduler_current_task();
        ports[i].owner_task = t ? t->id : 0;
        int j = 0;
        while (name[j] && j < IPC_PORT_NAME_LEN - 1) { ports[i].name[j] = name[j]; j++; }
        ports[i].name[j] = 0;
        ipc_kilit_ac(_ik);
        serial_write("IPC2: port acildi '");
        serial_write(ports[i].name);
        serial_write("' kimlik=");
        serial_write_hex((uint32_t)i);
        serial_write("\n");
        return i;
    }
    ipc_kilit_ac(_ik);
    log_write(LOG_HATA, "IPC2", "bos port yuvasi yok");
    return -1;
}

int ipc_port_find(const char *name) {
    /* Denetim (1.4.6): kilitsiz okuma, ayni anda acilan kapinin (used=1,
     * ad henuz yazilmamis) eski adiyla eslesebiliyordu. */
    int bulunan = -1;
    uint64_t _ik = ipc_kilitle();
    for (int i = 0; i < IPC_MAX_PORTS; i++)
        if (ports[i].used && name_eq(ports[i].name, name)) { bulunan = i; break; }
    ipc_kilit_ac(_ik);
    return bulunan;
}

int ipc_gorevin_portu(uint32_t gorev_id) {
    int bulunan = -1;
    uint64_t _ik = ipc_kilitle();
    for (int i = 0; i < IPC_MAX_PORTS; i++)
        if (ports[i].used && ports[i].owner_task == gorev_id) { bulunan = i; break; }
    ipc_kilit_ac(_ik);
    return bulunan;
}

int ipc_port_sahibi(int port, uint32_t *sahip) {
    if (port < 0 || port >= IPC_MAX_PORTS) return -1;
    uint64_t _ik = ipc_kilitle();
    int var = ports[port].used;
    if (var && sahip) *sahip = ports[port].owner_task;
    ipc_kilit_ac(_ik);
    return var ? 0 : -1;
}

/* UYUM duzeltmesi: olen gorevin portu acik kaliyordu. Tablo 8 yuva;
 * yeniden baslayan bir servis ayni adi alamiyor ('ZATEN kullanimda')
 * ve olu porta giden iletiler kimsenin okumadigi kuyrukta birikiyordu. */
int ipc_gorev_portlarini_kapat(uint32_t gorev_id) {
    int n = 0;
    uint64_t _ik = ipc_kilitle();
    for (int i = 0; i < IPC_MAX_PORTS; i++)
        if (ports[i].used && ports[i].owner_task == gorev_id) {
            ports[i].used = 0; ports[i].count = 0; n++;
        }
    ipc_kilit_ac(_ik);
    return n;
}

int ipc_port_close(int port) {
    if (port < 0 || port >= IPC_MAX_PORTS || !ports[port].used) return -1;
    uint64_t _ik = ipc_kilitle();
    ports[port].used = 0;
    ipc_kilit_ac(_ik);
    return 0;
}

int ipc_send(int dest_port, const ipc_message_t *msg) {
    if (dest_port < 0 || dest_port >= IPC_MAX_PORTS) return -1;
    uint64_t _ik = ipc_kilitle();
    ipc_port_t *p = &ports[dest_port];
    if (!p->used) { ipc_kilit_ac(_ik); return -1; }
    if (p->count >= IPC_QUEUE_DEPTH) { ipc_kilit_ac(_ik); return IPC_DOLU; }

    p->queue[p->tail] = *msg;
    p->tail = (p->tail + 1) % IPC_QUEUE_DEPTH;
    p->count++;
    stat_sent++;
    ipc_kilit_ac(_ik);
    return 0;
}

int ipc_try_receive(int port, ipc_message_t *out) {
    if (port < 0 || port >= IPC_MAX_PORTS) return -1;
    uint64_t _ik = ipc_kilitle();
    ipc_port_t *p = &ports[port];
    if (!p->used || p->count == 0) { ipc_kilit_ac(_ik); return 0; }

    *out = p->queue[p->head];
    p->head = (p->head + 1) % IPC_QUEUE_DEPTH;
    p->count--;
    stat_recv++;
    ipc_kilit_ac(_ik);
    return 1;
}

int ipc_call(const char *service_name, int reply_port,
              ipc_message_t *msg, ipc_message_t *reply, uint64_t timeout_ticks) {
    int dest = ipc_port_find(service_name);
    if (dest < 0) {
        serial_write("IPC2: servis bulunamadi: ");
        serial_write(service_name);
        serial_write("\n");
        return -1;
    }

    msg->sender_port = (uint32_t)reply_port;
    msg->sequence = seq_counter++;
    uint32_t want_seq = msg->sequence;

    if (ipc_send(dest, msg) != 0) return -1;

    /* Yaniti bekle - CPU'yu bosa harcamamak icin her turda gorevi
     * uyutuyoruz; boylece hizmet gorevi calisip yaniti uretebilir. */
    uint64_t deadline = timer_get_ticks() + timeout_ticks;
    while (timer_get_ticks() < deadline) {
        if (ipc_try_receive(reply_port, reply) == 1) {
            if (reply->sequence == want_seq) return 0;
            /* baska bir yanit - yok say ve beklemeye devam et */
        }
        task_sleep(1);
    }
    serial_write("IPC2: ZAMAN ASIMI - '");
    serial_write(service_name);
    serial_write("' yanit vermedi\n");
    return -1;
}

const char *ipc_port_name(int port) {
    if (port < 0 || port >= IPC_MAX_PORTS || !ports[port].used) return "(yok)";
    return ports[port].name;
}

uint32_t ipc_stats_sent(void)     { return stat_sent; }
uint32_t ipc_stats_received(void) { return stat_recv; }
