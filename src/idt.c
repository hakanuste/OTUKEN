#include "idt.h"
#include "pic.h"
#include "fb.h"
#include "serial.h"

/* 64-bit IDT girisi: 16 bayt, offset 3 parcaya bolunmus. */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   ip;

extern void idt_flush(uint64_t);

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (uint32_t)(base >> 32);
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].zero        = 0;
}

static irq_handler_t irq_routines[16] = {0};

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16)
        irq_routines[irq] = handler;
}

void idt_ap_yukle(void) { idt_flush((uint64_t)&ip); }

/* SMP adim 4: yerel APIC vektorleri. 0xF0 cekirdekler arasi cagri,
 * 0xFF sahte (spurious) kesme - EOI GEREKTIRMEZ, yalniz iretq. */
extern void lapic_cagri_stub(void);
extern void lapic_sahte_stub(void);
extern void lapic_zaman_stub(void);
static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void idt_lapic_kapilari_kur(void) {
    idt_set_gate(0xF0, (uint64_t)lapic_cagri_stub, 0x08, 0x8E);
    idt_set_gate(0xFF, (uint64_t)lapic_sahte_stub, 0x08, 0x8E);
    idt_set_gate(0xEF, (uint64_t)lapic_zaman_stub, 0x08, 0x8E);
}

void idt_install(void) {
    ip.limit = sizeof(struct idt_entry) * 256 - 1;
    ip.base  = (uint64_t)&idt;

    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0, 0);

    idt_set_gate(0,  (uint64_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint64_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint64_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint64_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint64_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint64_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint64_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint64_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E);

    pic_remap();

    idt_set_gate(32, (uint64_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E);

    idt_flush((uint64_t)&ip);
}

/* struct regs'in boyutu degisirse asagidaki offset hesabi da bozulur -
 * bu yuzden derleme zamaninda kontrol ediyoruz. */
_Static_assert(sizeof(struct regs) == 16 * 8,
    "struct regs boyutu degisti, get_pf_error_code offsetini guncelle");

/* #PF (vektor 14) HER ZAMAN gercek bir donanim hata koduyla gelir
 * (ISR_ERR yolu). Donanimin push sirasi [rflags,cs,rip,error_code]
 * seklindedir (error_code EN SON pushlanir, yani bizim push'umuz olan
 * int_no'ya EN YAKIN olan odur). Bellek duzeni (dusukten yuksek adrese):
 * [PUSH_ALL 120][int_no][error_code][rip][cs][rflags].
 * int_no, struct regs'in son alani -> raw[15]. error_code hemen ustunde
 * -> raw[16]. (Bu offset bir onceki denemede raw[19] varsayilmisti ve
 * PF-DEBUG ham veri dokumuyle YANLIS oldugu kanitlanip duzeltildi -
 * raw[18]=0x08 tam CS degeriyle eslesiyordu, bu da raw[17]=rip,
 * raw[16]=error_code oldugunu dogruluyor.) */
static uint64_t get_pf_error_code(struct regs *r) {
    uint64_t *raw = (uint64_t *)r;
    return raw[16];
}

static inline uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static void page_fault_handler(struct regs *r) {
    uint64_t fault_addr = read_cr2();
    uint64_t err = get_pf_error_code(r);

    serial_write("\n*** PAGE FAULT ***\n");
    serial_write("hatali adres (CR2): ");
    serial_write_hex((uint32_t)(fault_addr >> 32));
    serial_write_hex((uint32_t)fault_addr);
    serial_write("\nRIP: ");
    serial_write_hex((uint32_t)(((uint64_t *)r)[17] >> 32));
    serial_write_hex((uint32_t)((uint64_t *)r)[17]);
    serial_write("\nhata kodu: ");
    serial_write_hex((uint32_t)err);
    serial_write("\n  ");
    serial_write((err & 0x1) ? "present(korumali erisim) " : "not-present(haritali degil) ");
    serial_write((err & 0x2) ? "write " : "read ");
    serial_write((err & 0x4) ? "user-mode " : "kernel-mode ");
    if (err & 0x8) serial_write("reserved-bit-ihlali ");
    if (err & 0x10) serial_write("instruction-fetch ");
    serial_write("\n");

    /* Su an demand-paging/heap-buyutme mantigi yok - yani her page
     * fault, tanim geregi beklenmeyen bir durumdur. Ekranda da
     * gosterip duruyoruz (fb_panic zaten int_no=14 gosterecek). */
    fb_panic(14);
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* Stage 17: CPU istisnalarinin insan-okunur adlari - panik ciktisini
 * "EXCEPTION #13" gibi anlamsiz bir sayidan, gercekten teshis
 * edilebilir bir mesaja donusturur. */
static const char *exception_name(uint64_t n) {
    switch (n) {
        case 0:  return "Bolme hatasi (#DE)";
        case 1:  return "Hata ayiklama (#DB)";
        case 2:  return "Maskelenemeyen kesme (NMI)";
        case 3:  return "Kesme noktasi (#BP)";
        case 4:  return "Tasma (#OF)";
        case 5:  return "Sinir asimi (#BR)";
        case 6:  return "Gecersiz komut (#UD)";
        case 7:  return "Aygit yok (#NM)";
        case 8:  return "Cift hata (#DF)";
        case 10: return "Gecersiz TSS (#TS)";
        case 11: return "Segment yok (#NP)";
        case 12: return "Yigin hatasi (#SS)";
        case 13: return "Genel koruma hatasi (#GP)";
        case 14: return "Sayfa hatasi (#PF)";
        case 16: return "x87 kayan nokta (#MF)";
        case 17: return "Hizalama denetimi (#AC)";
        case 18: return "Makine denetimi (#MC)";
        case 19: return "SIMD kayan nokta (#XM)";
        default: return "Bilinmeyen istisna";
    }
}

static void dump_registers(struct regs *r) {
    serial_write("  RAX="); serial_write_hex((uint32_t)(r->rax >> 32)); serial_write_hex((uint32_t)r->rax);
    serial_write("  RBX="); serial_write_hex((uint32_t)(r->rbx >> 32)); serial_write_hex((uint32_t)r->rbx);
    serial_write("\n  RCX="); serial_write_hex((uint32_t)(r->rcx >> 32)); serial_write_hex((uint32_t)r->rcx);
    serial_write("  RDX="); serial_write_hex((uint32_t)(r->rdx >> 32)); serial_write_hex((uint32_t)r->rdx);
    serial_write("\n  RSI="); serial_write_hex((uint32_t)(r->rsi >> 32)); serial_write_hex((uint32_t)r->rsi);
    serial_write("  RDI="); serial_write_hex((uint32_t)(r->rdi >> 32)); serial_write_hex((uint32_t)r->rdi);
    serial_write("\n  RBP="); serial_write_hex((uint32_t)(r->rbp >> 32)); serial_write_hex((uint32_t)r->rbp);
    serial_write("\n");
    uint64_t *raw = (uint64_t *)r;
    serial_write("  hata_kodu="); serial_write_hex((uint32_t)raw[16]);
    serial_write("  RIP="); serial_write_hex((uint32_t)(raw[17] >> 32)); serial_write_hex((uint32_t)raw[17]);
    serial_write("  CS="); serial_write_hex((uint32_t)raw[18]);
    serial_write("\n");
}

void isr_handler(struct regs *r) {
    if (r->int_no == 14) {
        page_fault_handler(r);
        return; /* asla buraya donmez, ama derleyiciyi mutlu et */
    }

    serial_write("\n*** OTUKEN CEKIRDEK PANIGI ***\n");
    serial_write("  Istisna: ");
    serial_write(exception_name(r->int_no));
    serial_write(" (vektor=");
    serial_write_hex((uint32_t)r->int_no);
    serial_write(")\n");
    dump_registers(r);
    serial_write("  Sistem durduruluyor.\n");

    fb_panic((uint32_t)r->int_no);
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void irq_handler(struct regs *r) {
    int n = (int)(r->int_no - 32);
    /* EOI'yi handler'i CAGIRMADAN ONCE gonderiyoruz. Neden onemli:
     * handler (ornegin scheduler_tick) baska bir goreve context_switch
     * yapip bu fonksiyona hic geri donmeyebilir - EOI sonra gonderilseydi,
     * PIC bu IRQ hattinin bir sonraki olusumunu SONSUZA KADAR bloke
     * ederdi (ISR biti temizlenmeden ayni ya da dusuk oncelikli hicbir
     * kesme gelmez), sistem donardi. Erken EOI bunu onler. */
    if (n >= 0 && n < 16) pic_send_eoi((uint8_t)n);
    if (n >= 0 && n < 16 && irq_routines[n] != 0) {
        irq_routines[n](r);
    }
}
