#include "heap.h"
#include "spinlock.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"

/* --- Yerlesim --- */
/* 16GB sanal adresten baslar: 0-4GB kimlik haritasinin, VMM testlerinin
 * (0x500000000, 0x700000) ve gelecekte eklenecek diger sabit bolgelerin
 * disinda, cakisma riski olmayan bos bir alan. */
#define HEAP_START         0x0000000400000000ULL
#define HEAP_INITIAL_SIZE  (1ULL * 1024 * 1024)   /* 1MB ile basla */
#define HEAP_MAX_SIZE      (64ULL * 1024 * 1024)  /* en fazla 64MB'a buyu */
#define HEAP_GROW_MIN_STEP (64ULL * 1024)         /* buyutme adimi en az 64KB */
#define HEAP_ALIGN         16
#define HEAP_MIN_SPLIT      32
#define HEAP_MAGIC          0x4B48454150000000ULL /* "KHEAP" + isaretleyici ust bitler */

typedef struct heap_block {
    uint64_t size;      /* kullanilabilir veri boyutu (bu header haric) */
    uint64_t magic;
    uint8_t  is_free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static heap_block_t *heap_head = 0;
static uint64_t heap_current_end = 0;
static uint64_t heap_total_bytes = 0;

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

/* Stage 19 duzeltmesi (YARIS DURUMU): heap serbest listesi paylasilan
 * bir veri yapisidir. kmalloc/kfree/krealloc bir GOREV baglaminda
 * calisirken zamanlayici kesmesi araya girip BASKA bir gorevi
 * calistirabilir; o gorev de heap'e dokunursa liste BOZULUR (klasik
 * yaris durumu). Bu yuzden heap islemleri boyunca kesmeleri kapatiyoruz.
 *
 * ONEMLI: onceki kesme durumunu KAYDEDIP geri yukluyoruz - kosulsuz
 * sti() cagirmak, zaten kesmelerin kapali oldugu bir baglamdan (ornegin
 * bir kesme isleyicisi icinden ya da ic ice kritik bolgeden) cagrildiginda
 * kesmeleri ERKEN acar ve daha kotu bir hataya yol acardi. */
/* --- SMP: heap'i KENDI kilidiyle koru ---
 * Yalnizca kesme kapatmak (cli) TEK CEKIRDEKTE yeterliydi; ikinci bir
 * cekirdek ayni serbest listeye rahatca girer ve listeyi BOZAR.
 * Artik her kritik bolge once kilidi alir. Kilit alma zaten kesmeleri
 * de kapatir, yani eski koruma KAYBOLMAZ - ustune SMP korumasi gelir. */
static spinlock_t heap_kilit;
static int heap_kilit_hazir = 0;

static inline uint64_t heap_kilitle(void) {
    if (!heap_kilit_hazir) { spinlock_init(&heap_kilit); heap_kilit_hazir = 1; }
    return spin_lock_irqsave(&heap_kilit);
}
static inline void heap_kilit_ac(uint64_t f) {
    spin_unlock_irqrestore(&heap_kilit, f);
}

static inline uint64_t irq_save_disable(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static inline void irq_restore(uint64_t flags) {
    __asm__ volatile ("push %0; popfq" : : "r"(flags) : "memory", "cc");
}

static void serial_hex64_h(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) buf[2 + i] = hex[(v >> (60 - i * 4)) & 0xF];
    buf[18] = 0;
    serial_write(buf);
}

static int heap_extend(uint64_t additional_bytes) {
    uint64_t pages_needed = (additional_bytes + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    uint64_t start_virt = heap_current_end;

    if (start_virt + pages_needed * PMM_FRAME_SIZE > HEAP_START + HEAP_MAX_SIZE) {
        serial_write("heap: buyutme reddedildi - HEAP_MAX_SIZE sinirina ulasilirdi\n");
        return -1;
    }

    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t phys = pmm_alloc_frame();
        if (phys == 0) {
            serial_write("heap: PMM'de bellek kalmadi, buyutme yarim kaldi\n");
            return -1;
        }
        if (vmm_map_page(start_virt + i * PMM_FRAME_SIZE, phys,
                          VMM_PRESENT | VMM_WRITABLE) != 0) {
            pmm_free_frame(phys);
            serial_write("heap: vmm_map_page basarisiz\n");
            return -1;
        }
    }

    uint64_t new_region_size = pages_needed * PMM_FRAME_SIZE;
    heap_block_t *new_block = (heap_block_t *)start_virt;
    new_block->size    = new_region_size - sizeof(heap_block_t);
    new_block->magic   = HEAP_MAGIC;
    new_block->is_free = 1;
    new_block->next    = 0;
    new_block->prev    = 0;

    if (heap_head == 0) {
        heap_head = new_block;
    } else {
        heap_block_t *last = heap_head;
        while (last->next) last = last->next;
        if (last->is_free) {
            last->size += sizeof(heap_block_t) + new_block->size;
        } else {
            last->next = new_block;
            new_block->prev = last;
        }
    }

    heap_current_end  = start_virt + new_region_size;
    heap_total_bytes += new_region_size;
    return 0;
}

void heap_init(void) {
    heap_current_end = HEAP_START;
    heap_head = 0;
    heap_total_bytes = 0;

    if (heap_extend(HEAP_INITIAL_SIZE) != 0) {
        serial_write("heap: BASLANGIC ayirmasi basarisiz!\n");
        return;
    }
    serial_write("heap: baslatildi, baslangic=1MB, adres=");
    serial_hex64_h(HEAP_START);
    serial_write("\n");
}

static void split_block(heap_block_t *b, uint64_t needed_size) {
    if (b->size >= needed_size + sizeof(heap_block_t) + HEAP_MIN_SPLIT) {
        heap_block_t *remainder =
            (heap_block_t *)((uint8_t *)b + sizeof(heap_block_t) + needed_size);
        remainder->size    = b->size - needed_size - sizeof(heap_block_t);
        remainder->magic   = HEAP_MAGIC;
        remainder->is_free = 1;
        remainder->next    = b->next;
        remainder->prev    = b;
        if (b->next) b->next->prev = remainder;
        b->next = remainder;
        b->size = needed_size;
    }
}

static void check_magic_or_die(heap_block_t *b, const char *who) {
    if (b->magic != HEAP_MAGIC) {
        serial_write(who);
        serial_write(": HATA - heap bozulmasi tespit edildi (magic uyusmuyor)! Adres=");
        serial_hex64_h((uint64_t)(uintptr_t)b);
        serial_write("\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;
    uint64_t irqf = heap_kilitle();
    uint64_t needed = align_up((uint64_t)size, HEAP_ALIGN);

    heap_block_t *b = heap_head;
    while (b) {
        check_magic_or_die(b, "kmalloc");
        if (b->is_free && b->size >= needed) {
            split_block(b, needed);
            b->is_free = 0;
            void *result = (void *)((uint8_t *)b + sizeof(heap_block_t));
            heap_kilit_ac(irqf);
            return result;
        }
        b = b->next;
    }

    uint64_t grow_amount = needed + sizeof(heap_block_t);
    if (grow_amount < HEAP_GROW_MIN_STEP) grow_amount = HEAP_GROW_MIN_STEP;
    if (heap_extend(grow_amount) != 0) {
        heap_kilit_ac(irqf);
        serial_write("kmalloc: basarisiz (bellek yetersiz)\n");
        return 0;
    }

    /* simdi kesinlikle uyacak bir bos blok var - listeyi tekrar tara
     * (recursion yerine yeniden basit bir dongu, derin cagrilardan kacinmak icin) */
    b = heap_head;
    while (b) {
        if (b->is_free && b->size >= needed) {
            split_block(b, needed);
            b->is_free = 0;
            void *result = (void *)((uint8_t *)b + sizeof(heap_block_t));
            heap_kilit_ac(irqf);
            return result;
        }
        b = b->next;
    }
    heap_kilit_ac(irqf);
    return 0; /* buraya dusmemeli, ama savunmaci */
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment < HEAP_ALIGN) alignment = HEAP_ALIGN;
    uint64_t total = (uint64_t)size + alignment + sizeof(void *);
    void *raw = kmalloc((size_t)total);
    if (!raw) return 0;
    uint64_t aligned = align_up((uint64_t)(uintptr_t)raw + sizeof(void *), alignment);
    ((void **)(uintptr_t)aligned)[-1] = raw;
    return (void *)(uintptr_t)aligned;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uint64_t irqf = heap_kilitle();
    heap_block_t *b = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    check_magic_or_die(b, "kfree");

    if (b->is_free) {
        heap_kilit_ac(irqf);
        serial_write("kfree: UYARI - cift free (double free) tespit edildi, yok sayiliyor\n");
        return;
    }

    b->is_free = 1;

    if (b->next && b->next->is_free) {
        check_magic_or_die(b->next, "kfree(next-coalesce)");
        b->size += sizeof(heap_block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->is_free) {
        check_magic_or_die(b->prev, "kfree(prev-coalesce)");
        b->prev->size += sizeof(heap_block_t) + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
    heap_kilit_ac(irqf);
}

void kfree_aligned(void *ptr) {
    if (!ptr) return;
    void *raw = ((void **)ptr)[-1];
    kfree(raw);
}

void *krealloc(void *ptr, size_t new_size) {
    /* NOT: kmalloc/kfree kendi kesme korumalarini yaptigi icin, bu iki
     * erken donus yolu KENDI kritik bolgemizin DISINDA birakildi -
     * boylece ic ice kesme kapatma/acma karmasasi olusmuyor. */
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return 0; }

    uint64_t irqf = heap_kilitle();
    heap_block_t *b = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    check_magic_or_die(b, "krealloc");

    uint64_t needed = align_up((uint64_t)new_size, HEAP_ALIGN);

    if (needed <= b->size) {
        split_block(b, needed);
        heap_kilit_ac(irqf);
        return ptr;
    }

    if (b->next && b->next->is_free &&
        (b->size + sizeof(heap_block_t) + b->next->size) >= needed) {
        check_magic_or_die(b->next, "krealloc(merge-next)");
        b->size += sizeof(heap_block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
        split_block(b, needed);
        heap_kilit_ac(irqf);
        return ptr;
    }

    /* Yeni blok ayirip kopyalamamiz gerekiyor: kmalloc/kfree KENDI
     * korumalarini yapacagi icin burada kesmeleri ONCE geri aciyoruz
     * (aksi halde ic ice kapatma olurdu). b->size'i simdi okuyoruz
     * cunku sonrasinda blok degisebilir. */
    uint64_t old_size = b->size;
    heap_kilit_ac(irqf);

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;
    uint64_t copy_size = (old_size < (uint64_t)new_size) ? old_size : (uint64_t)new_size;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (uint64_t i = 0; i < copy_size; i++) dst[i] = src[i];
    kfree(ptr);
    return new_ptr;
}
void heap_get_stats(uint64_t *total_bytes, uint64_t *used_bytes,
                     uint64_t *free_bytes, uint64_t *block_count) {
    uint64_t blocks = 0;
    uint64_t used = 0;
    uint64_t free_sum = 0;
    for (heap_block_t *b = heap_head; b; b = b->next) {
        blocks++;
        if (b->is_free) free_sum += b->size;
        else used += b->size;
    }
    if (total_bytes) *total_bytes = heap_total_bytes;
    if (used_bytes)  *used_bytes  = used;
    if (free_bytes)  *free_bytes  = free_sum;
    if (block_count) *block_count = blocks;
}
