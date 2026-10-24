/* OSES öbeği: tek büyük alan üstünde bölen ve birleştiren ayırıcı.
 *
 * Ses motoru her cümlede binlerce, boyu birkaç bayttan 17 MB'a uzanan
 * tensör ayırıp bırakır. Bölmeyen/birleştirmeyen bir liste (UMAY'ın
 * genel öbeği gibi) bu düzende parçalanır ve büyür; bu yüzden motor
 * kendi alanını bir kez alır ve içini burada yönetir.
 *
 * Blok: [başlık boy|kullanımda] ... [altlık boy]. boy başlık ve altlık
 * dahil, 16'nın katı. Boş bloklar çift bağlı listede. Bırakılan blok
 * komşu boş bloklarla hemen birleşir. */
#include "oses_yigin.h"

typedef struct ob_blok {
    uint64_t boy;            /* bit 0: kullanımda */
    uint64_t _hiza;
    struct ob_blok *onceki, *sonraki;   /* yalnız boşken */
} ob_blok_t;

#define BAS   16u                      /* başlık (boy + hiza) */
#define ALT   16u                      /* altlık (boy, 16'ya tamam) */
#define EN_AZ ((uint64_t)sizeof(ob_blok_t) + ALT)

static uint64_t boy_(const ob_blok_t *b) { return b->boy & ~(uint64_t)15; }
static int dolu_(const ob_blok_t *b) { return (int)(b->boy & 1); }
static void isaretle(ob_blok_t *b, uint64_t boy, int dolu) {
    b->boy = boy | (uint64_t)(dolu ? 1 : 0);
    *(uint64_t *)((uint8_t *)b + boy - ALT) = b->boy;
}
static void cikar(ob_yigin_t *y, ob_blok_t *b) {
    if (b->onceki) b->onceki->sonraki = b->sonraki; else y->bos = b->sonraki;
    if (b->sonraki) b->sonraki->onceki = b->onceki;
}
static void ekle(ob_yigin_t *y, ob_blok_t *b) {
    b->onceki = 0; b->sonraki = y->bos;
    if (y->bos) y->bos->onceki = b;
    y->bos = b;
}

int ob_kur(ob_yigin_t *y, void *alan, uint64_t boy) {
    uint8_t *a = (uint8_t *)(((uintptr_t)alan + 15) & ~(uintptr_t)15);
    boy -= (uint64_t)(a - (uint8_t *)alan);
    boy &= ~(uint64_t)15;
    y->bos = 0; y->kullanilan = 0; y->tepe = 0;
    if (boy < 2 * 16 + EN_AZ) return -1;
    /* uçlarda 16 baytlık dolu bekçiler: birleştirme alanın dışına taşmaz */
    *(uint64_t *)a = 1;                         /* sol bekçinin "altlığı" */
    y->bas = a + 16; y->son = a + boy - 16;
    *(uint64_t *)y->son = 1;                    /* sağ bekçi başlığı */
    ob_blok_t *b = (ob_blok_t *)y->bas;
    isaretle(b, (uint64_t)(y->son - y->bas), 0);
    ekle(y, b);
    return 0;
}

void *ob_ayir(ob_yigin_t *y, uint64_t n) {
    if (n == 0) n = 1;
    uint64_t gerek = (n + BAS + ALT + 15) & ~(uint64_t)15;
    if (gerek < EN_AZ) gerek = EN_AZ;
    for (ob_blok_t *b = y->bos; b; b = b->sonraki) {
        uint64_t bb = boy_(b);
        if (bb < gerek) continue;
        cikar(y, b);
        if (bb - gerek >= EN_AZ) {              /* böl: kalan boş kalır */
            ob_blok_t *k = (ob_blok_t *)((uint8_t *)b + gerek);
            isaretle(k, bb - gerek, 0);
            ekle(y, k);
            bb = gerek;
        }
        isaretle(b, bb, 1);
        y->kullanilan += bb;
        if (y->kullanilan > y->tepe) y->tepe = y->kullanilan;
        return (uint8_t *)b + BAS;
    }
    return 0;
}

void ob_birak(ob_yigin_t *y, void *p) {
    if (!p) return;
    ob_blok_t *b = (ob_blok_t *)((uint8_t *)p - BAS);
    if ((uint8_t *)b < y->bas || (uint8_t *)b >= y->son || !dolu_(b)) return;   /* yabancı / çift bırakma */
    uint64_t bb = boy_(b);
    if (bb < EN_AZ || bb > (uint64_t)(y->son - (uint8_t *)b)) return;          /* bozuk blok */
    if (*(uint64_t *)((uint8_t *)b + bb - ALT) != b->boy) return;
    y->kullanilan -= bb;
    ob_blok_t *s = (ob_blok_t *)((uint8_t *)b + bb);           /* sağ komşu */
    if ((uint8_t *)s < y->son && !dolu_(s)) { cikar(y, s); bb += boy_(s); }
    uint64_t sol_alt = *(uint64_t *)((uint8_t *)b - ALT);      /* sol komşunun altlığı */
    if (!(sol_alt & 1)) {
        ob_blok_t *l = (ob_blok_t *)((uint8_t *)b - (sol_alt & ~(uint64_t)15));
        cikar(y, l); bb += boy_(l); b = l;
    }
    isaretle(b, bb, 0);
    ekle(y, b);
}
