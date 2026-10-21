#include "ulak.h"
#include "cpu.h"
#include "serial.h"

/* SMP adim 6: ULAK/TCP/soket/DHCP/DNS KILITSIZDIR, cunku tek kullanicisi
 * var: ana dongu (gui_main, BSP'ye bagli) yoklar; kullanici programlarina
 * ag syscall'i yok. Bu varsayim bozulursa (ag syscall'i eklenirse) kilit
 * gerekir - burada bir kez uyarilir ki sessizce bozulmasin. */
static void bsp_denetle(const char *islev) {
    static volatile int uyarildi = 0;
    if (cpu_bu()->indeks != 0 && !__atomic_exchange_n(&uyarildi, 1, __ATOMIC_RELAXED)) {
        serial_write("ULAK: [HATA] ag yigini BSP disindan cagrildi (kilitsiz!): ");
        serial_write(islev);
        serial_write("\n");
    }
}
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "tcp.h"
#include "dhcp.h"
#include "dns.h"
#include "socket.h"

/* ---- Intel e1000 (82540EM) kayit ofsetleri ---- */
#define E1000_CTRL     0x0000
#define E1000_STATUS   0x0008
#define E1000_EERD     0x0014
#define E1000_ICR      0x00C0
#define E1000_IMS      0x00D0
#define E1000_RCTL     0x0100
#define E1000_TCTL     0x0400
#define E1000_RDBAL    0x2800
#define E1000_RDBAH    0x2804
#define E1000_RDLEN    0x2808
#define E1000_RDH      0x2810
#define E1000_RDT      0x2818
#define E1000_TDBAL    0x3800
#define E1000_TDBAH    0x3804
#define E1000_TDLEN    0x3808
#define E1000_TDH      0x3810
#define E1000_TDT      0x3818
#define E1000_RAL      0x5400
#define E1000_RAH      0x5404

#define RCTL_EN        (1 << 1)
#define RCTL_BAM       (1 << 15)
#define RCTL_SECRC     (1 << 26)
#define RCTL_BSIZE_2048 0

#define TCTL_EN        (1 << 1)
#define TCTL_PSP       (1 << 3)
/* e1000 spesifikasyonu: iletimin GERCEKTEN baslamasi icin carpisma
 * esigi (CT, bit 4-11) ve carpisma mesafesi (COLD, bit 12-21) de
 * ayarlanmali - sifir birakilirsa kart paket gondermez (bu, gercekten
 * yasadigimiz "TX zaman asimi" hatasinin sebebiydi). */
#define TCTL_CT_SHIFT   4
#define TCTL_COLD_SHIFT 12
#define TCTL_CT_DEFAULT   (0x0F << TCTL_CT_SHIFT)    /* standart deger 15 */
#define TCTL_COLD_FULLDUP (0x40 << TCTL_COLD_SHIFT)  /* tam cift yonlu: 64 */

#define E1000_CTRL_SLU  (1 << 6)   /* Set Link Up */
#define E1000_CTRL_ASDE (1 << 5)   /* otomatik hiz tespiti */

#define TXD_CMD_EOP    (1 << 0)
#define TXD_CMD_IFCS   (1 << 1)
#define TXD_CMD_RS     (1 << 3)
#define TXD_STAT_DD    (1 << 0)
#define RXD_STAT_DD    (1 << 0)

#define NUM_RX_DESC 16
#define NUM_TX_DESC 16
#define RX_BUF_SIZE 2048

#define ULAK_MMIO_VIRT 0xB00000000ULL
#define ULAK_RING_VIRT 0xB10000000ULL

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} tx_desc_t;

static uint64_t mmio;
static volatile rx_desc_t *rx_ring;
static volatile tx_desc_t *tx_ring;
static uint8_t   *rx_buffers[NUM_RX_DESC];
static uint8_t   *tx_buffers[NUM_TX_DESC];
static uint64_t   rx_ring_phys, tx_ring_phys;
static uint64_t   rx_buf_phys[NUM_RX_DESC], tx_buf_phys[NUM_TX_DESC];
static uint32_t   rx_cur, tx_cur;
static ulak_state_t state;

/* --- Stage 16 tamamlanmasi: ARP onbellegi ---
 * Onceden TUM IP paketlerini yayin (broadcast) MAC'ine gonderiyorduk.
 * Bu, ayni agdaki basit hedefler icin calisiyordu ama yonlendirilmis
 * hedefler (ornegin QEMU guestfwd) yayin cercevelerini islemez -
 * paketler sessizce dusuyordu. Artik hedef IP'nin MAC adresini ARP
 * ile cozup GERCEK hedefe gonderiyoruz. */
#define ARP_CACHE_SIZE 8
typedef struct {
    int used;
    uint8_t ip[4];
    uint8_t mac[6];
} arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static void arp_cache_put(const uint8_t *ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].used) {
            int same = 1;
            for (int k = 0; k < 4; k++) if (arp_cache[i].ip[k] != ip[k]) same = 0;
            if (same) {
                for (int k = 0; k < 6; k++) arp_cache[i].mac[k] = mac[k];
                return;
            }
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].used) {
            arp_cache[i].used = 1;
            for (int k = 0; k < 4; k++) arp_cache[i].ip[k] = ip[k];
            for (int k = 0; k < 6; k++) arp_cache[i].mac[k] = mac[k];
            serial_write("ULAK: ARP onbellegine eklendi ");
            for (int k = 0; k < 4; k++) { serial_write_hex(ip[k]); if (k<3) serial_write("."); }
            serial_write(" -> ");
            for (int k = 0; k < 6; k++) { serial_write_hex(mac[k]); if (k<5) serial_write(":"); }
            serial_write("\n");
            return;
        }
    }
}

/* Bulursa 1 ve MAC'i doldurur, bulamazsa 0 doner. */
static int arp_cache_get(const uint8_t *ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].used) continue;
        int same = 1;
        for (int k = 0; k < 4; k++) if (arp_cache[i].ip[k] != ip[k]) same = 0;
        if (same) {
            for (int k = 0; k < 6; k++) mac_out[k] = arp_cache[i].mac[k];
            return 1;
        }
    }
    return 0;
}

static void mw32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)(mmio + reg) = val;
}
static uint32_t mr32(uint32_t reg) {
    return *(volatile uint32_t *)(uintptr_t)(mmio + reg);
}

/* ---- yardimcilar ---- */

static void mac_copy(uint8_t *dst, const uint8_t *src) {
    for (int i = 0; i < 6; i++) dst[i] = src[i];
}
static int ip_equal(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 4; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void print_mac(const uint8_t *m) {
    for (int i = 0; i < 6; i++) {
        serial_write_hex(m[i]);
        if (i < 5) serial_write(":");
    }
}

/* ---- baslatma ---- */
static int alloc_dma_page(uint64_t virt, uint64_t *phys_out, uint8_t **virt_out) {
    uint64_t p = pmm_alloc_frame();
    if (p == 0) return -1;
    /* DMA tamponlari icin onbellek devre disi (PCD) - kart DMA ile
     * dogrudan RAM'e yazdigi icin, CPU onbellegi bayat veri
     * gosterebilirdi. */
    if (vmm_map_page(virt, p, VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
        pmm_free_frame(p);
        return -1;
    }
    *phys_out = p;
    *virt_out = (uint8_t *)(uintptr_t)virt;
    for (int i = 0; i < 4096; i++) (*virt_out)[i] = 0;
    return 0;
}

int ulak_init(void) {
    state.link_up = 0;
    state.rx_packets = state.tx_packets = 0;
    state.rx_arp = state.rx_ipv4 = state.rx_udp = state.rx_icmp = 0;

    const pci_device_t *dev = pci_find_by_class(0x02, 0x00);
    if (!dev) {
        serial_write("ULAK: PCI'de ag denetleyici bulunamadi\n");
        return -1;
    }

    /* Denetim duzeltmesi: sinif 02:00 her Ethernet denetleyicisini
     * (Realtek, Intel I225, Broadcom...) e1000 sanip e1000 kayitlarina
     * yaziyordu - gercek anakartlarda yanlis cihazin MMIO'suna yazmak
     * demekti. Yalniz klasik e1000 ailesi surulur; digerleri atlanir. */
    {
        uint16_t v = dev->vendor_id, d = dev->device_id;
        int e1000_ailesi = (v == 0x8086) &&
            ((d >= 0x1000 && d <= 0x101F) || (d >= 0x1026 && d <= 0x1028) ||
             (d >= 0x1075 && d <= 0x107C) || d == 0x10B5 || d == 0x10D3);
        if (!e1000_ailesi) {
            serial_write("ULAK: ag denetleyicisi desteklenmiyor (yalniz Intel e1000 ailesi) vendor=");
            serial_write_hex(v);
            serial_write(" device=");
            serial_write_hex(d);
            serial_write("\n");
            return -1;
        }
    }

    uint64_t mmio_phys = (uint64_t)(dev->bar[0] & 0xFFFFFFF0);
    /* 64 bit bellek BAR'i (tur alani bit2:1 = 10b): ust yari BAR1'dedir. */
    if ((dev->bar[0] & 0x6) == 0x4) mmio_phys |= (uint64_t)dev->bar[1] << 32;
    if (mmio_phys == 0) {
        serial_write("ULAK: BAR0 bos\n");
        return -1;
    }

    /* e1000 kayit alani 128KB - 32 sayfa haritala */
    for (int i = 0; i < 32; i++) {
        if (vmm_map_page(ULAK_MMIO_VIRT + (uint64_t)i * 4096,
                          mmio_phys + (uint64_t)i * 4096,
                          VMM_PRESENT | VMM_WRITABLE | (1ULL << 4)) != 0) {
            serial_write("ULAK: MMIO haritalama basarisiz\n");
            return -1;
        }
    }
    mmio = ULAK_MMIO_VIRT;

    /* KRITIK: PCI Command kaydinda Bus Master (bit 2) bitini AC.
     * Bu bit kapaliyken cihaz DMA YAPAMAZ - tanimlayicilari okuyabilir
     * (TDH ilerler) ama sonuc durumunu RAM'e YAZAMAZ. Gercekten
     * yasadigimiz "TDH ilerliyor ama desc_status hep 0" belirtisinin
     * sebebi tam olarak buydu. Ayrica Memory Space (bit 1) de acik
     * olmali (MMIO erisimi icin). */
    uint32_t cmd_reg = pci_config_read32(dev->bus, dev->slot, dev->func, 0x04);
    cmd_reg |= (1 << 2) | (1 << 1);   /* Bus Master | Memory Space */
    pci_config_write32(dev->bus, dev->slot, dev->func, 0x04, cmd_reg);
    serial_write("ULAK: PCI Bus Master + Memory Space etkinlestirildi, CMD=");
    serial_write_hex(pci_config_read32(dev->bus, dev->slot, dev->func, 0x04) & 0xFFFF);
    serial_write("\n");

    serial_write("ULAK: e1000 bulundu vendor=");
    serial_write_hex(dev->vendor_id);
    serial_write(" device=");
    serial_write_hex(dev->device_id);
    serial_write(" MMIO=");
    serial_write_hex((uint32_t)mmio_phys);
    serial_write("\n");

    /* MAC adresini RAL/RAH kayitlarindan oku (QEMU bunlari onceden
     * doldurur - EEPROM okumaya gerek kalmaz) */
    uint32_t ral = mr32(E1000_RAL);
    uint32_t rah = mr32(E1000_RAH);
    state.mac[0] = (uint8_t)(ral & 0xFF);
    state.mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    state.mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    state.mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    state.mac[4] = (uint8_t)(rah & 0xFF);
    state.mac[5] = (uint8_t)((rah >> 8) & 0xFF);

    serial_write("ULAK: MAC adresi=");
    print_mac(state.mac);
    serial_write("\n");

    /* Link'i acikca ayaga kaldir (SLU) + otomatik hiz tespiti */
    mw32(E1000_CTRL, mr32(E1000_CTRL) | E1000_CTRL_SLU | E1000_CTRL_ASDE);

    uint32_t status = mr32(E1000_STATUS);
    state.link_up = (status & (1 << 1)) ? 1 : 0;
    serial_write("ULAK: baglanti (link) durumu=");
    serial_write(state.link_up ? "UP\n" : "DOWN\n");

    /* --- RX halkasi --- */
    uint8_t *ring_virt;
    if (alloc_dma_page(ULAK_RING_VIRT, &rx_ring_phys, &ring_virt) != 0) {
        serial_write("ULAK: RX halkasi ayrilamadi\n");
        return -1;
    }
    rx_ring = (volatile rx_desc_t *)ring_virt;

    for (int i = 0; i < NUM_RX_DESC; i++) {
        if (alloc_dma_page(ULAK_RING_VIRT + 0x10000 + (uint64_t)i * 4096,
                            &rx_buf_phys[i], &rx_buffers[i]) != 0) {
            serial_write("ULAK: RX tamponu ayrilamadi\n");
            return -1;
        }
        rx_ring[i].addr = rx_buf_phys[i];
        rx_ring[i].status = 0;
    }

    mw32(E1000_RDBAL, (uint32_t)(rx_ring_phys & 0xFFFFFFFF));
    mw32(E1000_RDBAH, (uint32_t)(rx_ring_phys >> 32));
    mw32(E1000_RDLEN, NUM_RX_DESC * sizeof(rx_desc_t));
    mw32(E1000_RDH, 0);
    mw32(E1000_RDT, NUM_RX_DESC - 1);
    rx_cur = 0;
    mw32(E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC | RCTL_BSIZE_2048);

    /* --- TX halkasi --- */
    uint8_t *tx_ring_virt;
    if (alloc_dma_page(ULAK_RING_VIRT + 0x8000, &tx_ring_phys, &tx_ring_virt) != 0) {
        serial_write("ULAK: TX halkasi ayrilamadi\n");
        return -1;
    }
    tx_ring = (volatile tx_desc_t *)tx_ring_virt;

    for (int i = 0; i < NUM_TX_DESC; i++) {
        if (alloc_dma_page(ULAK_RING_VIRT + 0x30000 + (uint64_t)i * 4096,
                            &tx_buf_phys[i], &tx_buffers[i]) != 0) {
            serial_write("ULAK: TX tamponu ayrilamadi\n");
            return -1;
        }
        tx_ring[i].addr = tx_buf_phys[i];
        tx_ring[i].status = TXD_STAT_DD; /* bos/hazir isaretle */
        tx_ring[i].cmd = 0;
    }

    mw32(E1000_TDBAL, (uint32_t)(tx_ring_phys & 0xFFFFFFFF));
    mw32(E1000_TDBAH, (uint32_t)(tx_ring_phys >> 32));
    mw32(E1000_TDLEN, NUM_TX_DESC * sizeof(tx_desc_t));
    mw32(E1000_TDH, 0);
    mw32(E1000_TDT, 0);
    tx_cur = 0;
    mw32(E1000_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT_DEFAULT | TCTL_COLD_FULLDUP);

    serial_write("ULAK: RX/TX halkalari kuruldu, kart hazir\n");
    return 0;
}

int ulak_send_frame(const uint8_t *data, uint16_t len) {
    bsp_denetle(__func__);
    if (len > RX_BUF_SIZE) return -1;

    uint32_t i = tx_cur;
    for (uint16_t k = 0; k < len; k++) tx_buffers[i][k] = data[k];

    tx_ring[i].length = len;
    tx_ring[i].cmd = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    tx_ring[i].status = 0;

    tx_cur = (tx_cur + 1) % NUM_TX_DESC;
    mw32(E1000_TDT, tx_cur);

    /* gonderimin tamamlanmasini bekle (DD biti) */
    for (int spin = 0; spin < 1000000; spin++) {
        if (tx_ring[i].status & TXD_STAT_DD) {
            state.tx_packets++;
            return 0;
        }
    }
    serial_write("ULAK: TX zaman asimi\n");
    return -1;
}

/* ---- protokol katmanlari ---- */
static uint16_t checksum16(const uint8_t *data, int len) {
    uint32_t sum = 0;
    for (int i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)data[i] << 8) | data[i + 1];
    if (len & 1) sum += (uint32_t)data[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int ulak_send_arp_request(const uint8_t target_ip[4]) {
    uint8_t frame[42];
    for (int i = 0; i < 6; i++) frame[i] = 0xFF;          /* hedef: yayin */
    mac_copy(&frame[6], state.mac);                        /* kaynak MAC */
    frame[12] = 0x08; frame[13] = 0x06;                    /* EtherType=ARP */

    frame[14] = 0x00; frame[15] = 0x01;                    /* HTYPE=Ethernet */
    frame[16] = 0x08; frame[17] = 0x00;                    /* PTYPE=IPv4 */
    frame[18] = 6; frame[19] = 4;                          /* uzunluklar */
    frame[20] = 0x00; frame[21] = 0x01;                    /* OPER=istek */
    mac_copy(&frame[22], state.mac);
    for (int i = 0; i < 4; i++) frame[28 + i] = state.ip[i];
    for (int i = 0; i < 6; i++) frame[32 + i] = 0x00;
    for (int i = 0; i < 4; i++) frame[38 + i] = target_ip[i];

    return ulak_send_frame(frame, sizeof(frame));
}

/* Stage 16 tamamlanmasi: herhangi bir ust-katman protokolu (TCP=6,
 * UDP=17, ICMP=1) icin genel IPv4 gonderimi. TCP katmani bunu kullanir. */
int ulak_send_ipv4(const uint8_t dest_ip[4], uint8_t protocol,
                    const uint8_t *payload, uint16_t len) {
    if (len > 1400) return -1;
    uint8_t frame[1518];
    int pos = 0;

    /* Hedef MAC'i ARP onbelleginden coz; yoksa ARP istegi gonderip
     * (bu paket icin) yayina dus - bir sonraki gonderimde onbellekte
     * hazir olacak. */
    uint8_t dst_mac[6];
    if (!arp_cache_get(dest_ip, dst_mac)) {
        for (int i = 0; i < 6; i++) dst_mac[i] = 0xFF;
        ulak_send_arp_request(dest_ip);
    }
    for (int i = 0; i < 6; i++) frame[pos++] = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[pos++] = state.mac[i];
    frame[pos++] = 0x08; frame[pos++] = 0x00;                 /* EtherType=IPv4 */

    int ip_start = pos;
    frame[pos++] = 0x45;
    frame[pos++] = 0x00;
    uint16_t total_len = (uint16_t)(20 + len);
    frame[pos++] = (uint8_t)(total_len >> 8);
    frame[pos++] = (uint8_t)(total_len & 0xFF);
    frame[pos++] = 0x00; frame[pos++] = 0x02;
    frame[pos++] = 0x40; frame[pos++] = 0x00;
    frame[pos++] = 64;
    frame[pos++] = protocol;
    int cksum_pos = pos;
    frame[pos++] = 0x00; frame[pos++] = 0x00;
    for (int i = 0; i < 4; i++) frame[pos++] = state.ip[i];
    for (int i = 0; i < 4; i++) frame[pos++] = dest_ip[i];

    uint16_t ip_cksum = checksum16(&frame[ip_start], 20);
    frame[cksum_pos]     = (uint8_t)(ip_cksum >> 8);
    frame[cksum_pos + 1] = (uint8_t)(ip_cksum & 0xFF);

    for (uint16_t i = 0; i < len; i++) frame[pos++] = payload[i];
    return ulak_send_frame(frame, (uint16_t)pos);
}

int ulak_send_udp(const uint8_t dest_ip[4], uint16_t src_port,
                   uint16_t dest_port, const uint8_t *payload, uint16_t len) {
    if (len > 1400) return -1;
    uint8_t frame[1518];
    int pos = 0;

    /* Ethernet: hedef MAC yayin (basit tutuldu - ARP tablosu
     * cozumlemesi sonraki asamada) */
    for (int i = 0; i < 6; i++) frame[pos++] = 0xFF;
    for (int i = 0; i < 6; i++) frame[pos++] = state.mac[i];
    frame[pos++] = 0x08; frame[pos++] = 0x00;   /* EtherType=IPv4 */

    int ip_start = pos;
    frame[pos++] = 0x45;                         /* v4, IHL=5 */
    frame[pos++] = 0x00;
    uint16_t total_len = (uint16_t)(20 + 8 + len);
    frame[pos++] = (uint8_t)(total_len >> 8);
    frame[pos++] = (uint8_t)(total_len & 0xFF);
    frame[pos++] = 0x00; frame[pos++] = 0x01;    /* ID */
    frame[pos++] = 0x40; frame[pos++] = 0x00;    /* bayraklar: DF */
    frame[pos++] = 64;                            /* TTL */
    frame[pos++] = 17;                            /* protokol=UDP */
    int cksum_pos = pos;
    frame[pos++] = 0x00; frame[pos++] = 0x00;    /* checksum (sonra) */
    for (int i = 0; i < 4; i++) frame[pos++] = state.ip[i];
    for (int i = 0; i < 4; i++) frame[pos++] = dest_ip[i];

    uint16_t ip_cksum = checksum16(&frame[ip_start], 20);
    frame[cksum_pos]     = (uint8_t)(ip_cksum >> 8);
    frame[cksum_pos + 1] = (uint8_t)(ip_cksum & 0xFF);

    /* UDP basligi */
    frame[pos++] = (uint8_t)(src_port >> 8);
    frame[pos++] = (uint8_t)(src_port & 0xFF);
    frame[pos++] = (uint8_t)(dest_port >> 8);
    frame[pos++] = (uint8_t)(dest_port & 0xFF);
    uint16_t udp_len = (uint16_t)(8 + len);
    frame[pos++] = (uint8_t)(udp_len >> 8);
    frame[pos++] = (uint8_t)(udp_len & 0xFF);
    frame[pos++] = 0x00; frame[pos++] = 0x00;    /* UDP checksum: 0=kullanilmiyor */

    for (uint16_t i = 0; i < len; i++) frame[pos++] = payload[i];

    return ulak_send_frame(frame, (uint16_t)pos);
}

static void handle_arp(const uint8_t *pkt, uint16_t len) {
    if (len < 42) return;
    uint16_t oper = ((uint16_t)pkt[20] << 8) | pkt[21];
    const uint8_t *sender_mac = &pkt[22];
    const uint8_t *sender_ip  = &pkt[28];
    const uint8_t *target_ip  = &pkt[38];

    state.rx_arp++;
    arp_cache_put(sender_ip, sender_mac);   /* hem istek hem yanit ogretici */
    serial_write("ULAK: ARP ");
    serial_write(oper == 1 ? "istegi" : "yaniti");
    serial_write(" alindi, gonderen IP=");
    for (int i = 0; i < 4; i++) { serial_write_hex(sender_ip[i]); if (i<3) serial_write("."); }
    serial_write("\n");

    /* Bize yonelik ARP istegine YANIT ver */
    if (oper == 1 && ip_equal(target_ip, state.ip)) {
        uint8_t reply[42];
        mac_copy(&reply[0], sender_mac);
        mac_copy(&reply[6], state.mac);
        reply[12] = 0x08; reply[13] = 0x06;
        reply[14] = 0x00; reply[15] = 0x01;
        reply[16] = 0x08; reply[17] = 0x00;
        reply[18] = 6; reply[19] = 4;
        reply[20] = 0x00; reply[21] = 0x02;  /* OPER=yanit */
        mac_copy(&reply[22], state.mac);
        for (int i = 0; i < 4; i++) reply[28 + i] = state.ip[i];
        mac_copy(&reply[32], sender_mac);
        for (int i = 0; i < 4; i++) reply[38 + i] = sender_ip[i];
        ulak_send_frame(reply, sizeof(reply));
        serial_write("ULAK: ARP yaniti gonderildi (bize sorulmustu)\n");
    }
}

static void handle_ipv4(const uint8_t *pkt, uint16_t len) {
    if (len < 34) return;
    const uint8_t *ip = &pkt[14];
    uint8_t ihl = (uint8_t)((ip[0] & 0x0F) * 4);
    uint8_t proto = ip[9];
    const uint8_t *src_ip = &ip[12];
    /* Denetim (1.4.6): IHL, IP toplam boyu ve UDP boyu PAKETIN BASLIGINDAN
     * geliyordu ve cercevenin gercek boyuyla sinanmiyordu: buyuk boy bildiren
     * bir paket ust katmanlari RX tamponunun disini okumaya goturebilirdi.
     * Artik hepsi cerceveye (len) kirpilir. */
    if (ihl < 20 || (uint32_t)14 + ihl > len) return;
    uint16_t ip_cerceve = (uint16_t)(len - 14);
    uint16_t ip_bildirilen = ((uint16_t)ip[2] << 8) | ip[3];
    if (ip_bildirilen < ihl) return;
    uint16_t ip_boy = ip_bildirilen < ip_cerceve ? ip_bildirilen : ip_cerceve;

    state.rx_ipv4++;
    serial_write("ULAK: IPv4 paketi, protokol=");
    serial_write_hex(proto);
    serial_write(" kaynak=");
    for (int i = 0; i < 4; i++) { serial_write_hex(src_ip[i]); if (i<3) serial_write("."); }
    serial_write("\n");

    if (proto == 17) { /* UDP */
        if ((uint32_t)ihl + 8 > ip_boy) return;      /* UDP basligi cercevede yok */
        const uint8_t *udp = ip + ihl;
        uint16_t sport = ((uint16_t)udp[0] << 8) | udp[1];
        uint16_t dport = ((uint16_t)udp[2] << 8) | udp[3];
        state.rx_udp++;
        uint16_t udp_total = ((uint16_t)udp[4] << 8) | udp[5];
        uint16_t udp_azami = (uint16_t)(ip_boy - ihl);
        if (udp_total > udp_azami) udp_total = udp_azami;
        uint16_t payload_len = (udp_total > 8) ? (uint16_t)(udp_total - 8) : 0;
        const uint8_t *udp_payload = udp + 8;

        serial_write("ULAK: UDP paketi, kaynak_port=");
        serial_write_hex(sport);
        serial_write(" hedef_port=");
        serial_write_hex(dport);
        serial_write("\n");

        /* Asama 29: datagrami ust katmanlara yonlendir */
        if (dport == 68) {
            dhcp_handle_packet(udp_payload, payload_len);
        } else if (sport == 53) {
            dns_handle_packet(udp_payload, payload_len);
        } else {
            socket_deliver_udp(src_ip, sport, dport, udp_payload, payload_len);
        }
    } else if (proto == 6) { /* TCP */
        const uint8_t *tcp_seg = ip + ihl;
        uint16_t tcp_len = (ip_boy > ihl) ? (uint16_t)(ip_boy - ihl) : 0;
        state.rx_tcp++;
        tcp_handle_segment(src_ip, tcp_seg, tcp_len);
    } else if (proto == 1) { /* ICMP */
        state.rx_icmp++;
        serial_write("ULAK: ICMP paketi alindi\n");
    }
}

int ulak_poll(void) {
    bsp_denetle(__func__);
    int processed = 0;

    while (rx_ring[rx_cur].status & RXD_STAT_DD) {
        uint16_t len = rx_ring[rx_cur].length;
        const uint8_t *pkt = rx_buffers[rx_cur];

        state.rx_packets++;
        processed++;

        uint16_t ethertype = ((uint16_t)pkt[12] << 8) | pkt[13];
        if (ethertype == 0x0806) {
            handle_arp(pkt, len);
        } else if (ethertype == 0x0800) {
            handle_ipv4(pkt, len);
        }

        rx_ring[rx_cur].status = 0;
        mw32(E1000_RDT, rx_cur);
        rx_cur = (rx_cur + 1) % NUM_RX_DESC;
    }
    return processed;
}

const ulak_state_t *ulak_get_state(void) { return &state; }

void ulak_set_ip(const uint8_t ip[4]) {
    for (int i = 0; i < 4; i++) state.ip[i] = ip[i];
    serial_write("ULAK: IP adresi ayarlandi=");
    for (int i = 0; i < 4; i++) { serial_write_hex(ip[i]); if (i<3) serial_write("."); }
    serial_write("\n");
}
