#include "timur.h"
#include "vmm.h"
#include "serial.h"

#define MSR_EFER 0xC0000080
#define EFER_NXE (1ULL << 11)
#define PTE_NX   (1ULL << 63)

static int nx_supported = 0;

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

int timur_enable_nx(void) {
    uint32_t eax, ebx, ecx, edx;
    /* CPUID 0x80000001: EDX bit 20 = NX destegi */
    eax = 0x80000001;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));

    if (!(edx & (1 << 20))) {
        serial_write("TIMUR: CPU NX (calistirilamaz sayfa) DESTEKLEMIYOR\n");
        nx_supported = 0;
        return -1;
    }

    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_NXE;
    wrmsr(MSR_EFER, efer);

    nx_supported = 1;
    serial_write("TIMUR: NX destegi VAR, EFER.NXE etkinlestirildi\n");
    return 0;
}

int timur_nx_available(void) { return nx_supported; }

/* Bir sayfa tablosu girisini bulup bayraklarini degistirir.
 * VMM'in ic yapisini kullanir ama SADECE yaprak (PT) girisine dokunur -
 * ara tablolara dokunmaz (onlar zaten izin verici olmali, gercek
 * kisitlama yaprakta uygulanir). */
static uint64_t *find_pte(uint64_t virt) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & 0x000FFFFFFFFFF000ULL);

    int i4 = (int)((virt >> 39) & 0x1FF);
    int i3 = (int)((virt >> 30) & 0x1FF);
    int i2 = (int)((virt >> 21) & 0x1FF);
    int i1 = (int)((virt >> 12) & 0x1FF);

    if (!(pml4[i4] & 1)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i4] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[i3] & 1)) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[i3] & 0x000FFFFFFFFFF000ULL);
    if (!(pd[i2] & 1)) return 0;
    if (pd[i2] & (1ULL << 7)) return 0; /* 2MB sayfa - 4KB giris yok */
    uint64_t *pt = (uint64_t *)(uintptr_t)(pd[i2] & 0x000FFFFFFFFFF000ULL);
    if (!(pt[i1] & 1)) return 0;
    return &pt[i1];
}

static void invlpg(uint64_t v) {
    __asm__ volatile ("invlpg (%0)" : : "r"(v) : "memory");
}

int timur_protect_data(uint64_t virt) {
    uint64_t *pte = find_pte(virt & ~0xFFFULL);
    if (!pte) return -1;
    *pte |= VMM_WRITABLE;          /* yazilabilir */
    if (nx_supported) *pte |= PTE_NX;  /* ama CALISTIRILAMAZ */
    invlpg(virt);
    return 0;
}

int timur_protect_code(uint64_t virt) {
    uint64_t *pte = find_pte(virt & ~0xFFFULL);
    if (!pte) return -1;
    *pte &= ~VMM_WRITABLE;         /* YAZILAMAZ */
    *pte &= ~PTE_NX;                /* ama calistirilabilir */
    invlpg(virt);
    return 0;
}

void timur_report_protection(const char *label, uint64_t virt) {
    uint64_t *pte = find_pte(virt & ~0xFFFULL);
    serial_write("TIMUR: ");
    serial_write(label);
    if (!pte) {
        serial_write(" -> sayfa haritali degil\n");
        return;
    }
    serial_write(" -> ");
    serial_write((*pte & VMM_WRITABLE) ? "YAZILABILIR " : "salt-okunur ");
    serial_write((*pte & PTE_NX) ? "CALISTIRILAMAZ(NX)" : "calistirilabilir");
    serial_write((*pte & VMM_USER) ? " kullanici-erisimli" : " sadece-cekirdek");
    serial_write("\n");
}
