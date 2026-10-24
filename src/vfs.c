#include "vfs.h"
#include "depo_kilit.h"

ozkilit_t depo_kilit;   /* bkz. depo_kilit.h */
#include "fat16_ic.h"
#include "heap.h"
#include "serial.h"
#include "sched.h"
#include "spinlock.h"

typedef struct {
    int active;
    char mount_point[VFS_MAX_PATH];
    const vfs_ops_t *ops;
    void *fs_ctx;
} vfs_mount_t;

/* =====================================================================
 * 1.4.7 — DOSYA TANITICISI SAHIPLIGI VE KUSAK (generation)
 *
 * Eskiden tanitici = yuva numarasi (0..15) ve tablo TEK/global idi: A
 * surecinin actigi dosyayi, numarayi tahmin eden B sureci okuyup
 * kapatabiliyordu; kapanip yeniden kullanilan yuvanin ESKI numarasi yeni
 * dosyaya erisiyordu; olen surecin dosyalari hic kapanmiyordu.
 *
 *   tanitici = (kusak << VFS_YUVA_BIT) | yuva
 *   - sahip : dosyayi ACAN gorevin kimligi. Yalniz o gorev okur/kapatir.
 *   - kusak : yuva her ACILISTA artar. Kapanmis/yeniden kullanilmis yuvanin
 *             eski taniticisi (kusagi tutmaz) GECERSIZDIR (bayat tanitici).
 *   - kusak 1'den baslar: tanitici hep >= 16; 0/1/2 (standart akis,
 *     UMAY'in konsol=1 tanitici gibi) ile karismaz. < 2^30: UMAY'in ozel
 *     UP_YAZ_TUTAMAK (0x7FFF0001) degeriyle de karismaz.
 *   - olen gorevin acik dosyalari reap'te kapanir (vfs_gorev_bitti).
 *
 * Paylasilan nesne yok: dup/fork olmadigindan her acilis BAGIMSIZ bir
 * giristir (kendi ofseti); ayni dosyanin iki acilisi ayni girisi paylasmaz,
 * dolayisiyla giris icin referans sayaci GEREKMEZ (belgelendi).
 *
 * Kilit: yuva/sahip/kusak alanlari fd_kilit (kisa DONEN kilit) altinda.
 * depo_kilit (UYUYAN) disk G/C icindir. Sira: depo_kilit -> fd_kilit;
 * ASLA tersi. reap yalniz fd_kilit alir (uyumaz): her baglamdan guvenli.
 * ===================================================================== */
#define VFS_YUVA_BIT     4
#define VFS_KUSAK_AZAMI  (1u << 26)          /* tanitici < 2^30 kalsin */
_Static_assert(VFS_MAX_OPEN_FILES <= (1 << VFS_YUVA_BIT), "yuva bitleri yetmiyor");

typedef struct {
    int active;
    vfs_mount_t *mount;
    char path[VFS_MAX_PATH]; /* mount noktasina GORE (bagimsiz) yol */
    uint64_t offset;
    uint64_t size;
    uint32_t sahip;          /* acan gorevin kimligi */
    uint32_t kusak;          /* yuvanin kusagi (acilis sayaci, 1..) */
} vfs_file_t;

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_file_t  open_files[VFS_MAX_OPEN_FILES];
static ozspin_t    fd_kilit = OZSPIN_BASLANGIC;

static uint32_t cagiran_kimlik(void) {
    task_t *t = scheduler_current_task();
    return t ? t->id : 0;    /* zamanlayici oncesi akis = ana gorev (0) */
}

/* Taniticiyi cozer; gecerli, GUNCEL kusakli ve CAGIRANIN ise girisi doner.
 * fd_kilit TUTULURKEN cagrilir. */
static vfs_file_t *tut_coz(int fd) {
    if (fd < 0) return 0;
    uint32_t u = (uint32_t)fd;
    uint32_t yuva = u & ((1u << VFS_YUVA_BIT) - 1u), kusak = u >> VFS_YUVA_BIT;
    if (yuva >= VFS_MAX_OPEN_FILES) return 0;
    vfs_file_t *f = &open_files[yuva];
    if (!f->active || f->kusak != kusak) return 0;   /* kapali ya da BAYAT tanitici */
    if (f->sahip != cagiran_kimlik()) return 0;      /* baska gorevin taniticisi */
    return f;
}

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int starts_with(const char *s, const char *prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) mounts[i].active = 0;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].active = 0;
        open_files[i].kusak = 0;      /* ilk acilista 1 olur */
        open_files[i].sahip = 0;
    }
    serial_write("vfs: baslatildi\n");
}

int vfs_mount(const char *mount_point, const vfs_ops_t *ops, void *fs_ctx) {
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    str_copy(mounts[slot].mount_point, mount_point, VFS_MAX_PATH);
    mounts[slot].ops = ops;
    mounts[slot].fs_ctx = fs_ctx;
    mounts[slot].active = 1;

    serial_write("vfs: baglandi noktasi='");
    serial_write(mount_point);
    serial_write("'\n");
    return 0;
}

/* En uzun eslesen mount noktasini bulur, alt-dosya-sistemine gore
 * BAGIMSIZ yolu (relative) hesaplar. Su an sadece "/" mount noktasi
 * kullanildigindan basit tutuldu; birden fazla/ic ice mount eklenirse
 * bu fonksiyon genisletilmelidir (bilinen, dokumante edilmis sinir). */
static vfs_mount_t *resolve_mount(const char *path, const char **relative_out) {
    vfs_mount_t *best = 0;
    int best_len = -1;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) continue;
        int mp_len = str_len(mounts[i].mount_point);
        /* bag noktasi bir YOL BILESENI olmali: "/fat32x" "/fat32"e dusmez */
        if (starts_with(path, mounts[i].mount_point) && mp_len > best_len &&
            (mp_len == 1 || path[mp_len] == 0 || path[mp_len] == '/')) {
            best = &mounts[i];
            best_len = mp_len;
        }
    }
    if (!best) return 0;

    if (best_len == 1) { /* mount noktasi "/" ise yolun tamamini kullan */
        *relative_out = path;
    } else {
        const char *rest = path + best_len;
        *relative_out = (rest[0] == 0) ? "/" : rest; /* mount'un KENDISI istendiyse -> "/" */
    }
    return best;
}

static int vfs_list_dir__ic(const char *path, vfs_dirent_t *out, int max) {
    const char *relative;
    vfs_mount_t *m = resolve_mount(path, &relative);
    if (!m || !m->ops->list_dir) return -1;
    return m->ops->list_dir(relative, out, max, m->fs_ctx);
}

static int vfs_open__ic(const char *path) {
    const char *relative;
    vfs_mount_t *m = resolve_mount(path, &relative);
    if (!m || !m->ops->read_file) return -1;

    /* Dosyanin var olup olmadigini ve boyutunu ogrenmek icin 0 baytlik
     * bir okuma deneriz (out_filesize her zaman doldurulur). */
    uint64_t filesize = 0;
    uint8_t dummy;
    int r = m->ops->read_file(relative, &dummy, 0, 0, &filesize, m->fs_ctx);
    if (r < 0) return -1;

    ozspin_al(&fd_kilit);
    int slot = -1;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!open_files[i].active) { slot = i; break; }
    }
    if (slot < 0) { ozspin_birak(&fd_kilit); return -1; }

    vfs_file_t *f = &open_files[slot];
    uint32_t k = f->kusak + 1u;
    if (k == 0 || k >= VFS_KUSAK_AZAMI) k = 1;   /* sarmalama: 2^26 acilista bir */
    f->kusak = k;
    f->sahip = cagiran_kimlik();
    f->mount = m;
    str_copy(f->path, relative, VFS_MAX_PATH);
    f->offset = 0;
    f->size = filesize;
    f->active = 1;                               /* alanlar dolduktan SONRA */
    int tut = (int)((k << VFS_YUVA_BIT) | (uint32_t)slot);
    ozspin_birak(&fd_kilit);
    return tut;
}

static int vfs_read__ic(int fd, void *buf, uint64_t size) {
    ozspin_al(&fd_kilit);
    vfs_file_t *f = tut_coz(fd);
    ozspin_birak(&fd_kilit);
    if (!f) return -1;
    /* Giris burada fd_kilit DISINDA kullanilir: yalniz SAHIBI (su an cagiran,
     * canli gorev) ona erisir; baskasi tut_coz'dan gecemez, reap ise yalniz
     * OLU gorevin girislerini kapatir. depo_kilit disk G/C'yi sirayla korur. */
    uint64_t filesize = 0;
    int n = f->mount->ops->read_file(f->path, (uint8_t *)buf, size,
                                      f->offset, &filesize, f->mount->fs_ctx);
    if (n < 0) return -1;

    f->offset += (uint64_t)n;
    return n;
}

static int vfs_close__ic(int fd) {
    ozspin_al(&fd_kilit);
    vfs_file_t *f = tut_coz(fd);
    if (f) f->active = 0;        /* kusak KORUNUR: yeniden acilista artar */
    ozspin_birak(&fd_kilit);
    return f ? 0 : -1;           /* ikinci kapatma / bayat / yabanci: -1 */
}

static uint64_t vfs_lseek__ic(int fd, uint64_t new_offset) {
    ozspin_al(&fd_kilit);
    vfs_file_t *f = tut_coz(fd);
    if (f) f->offset = new_offset;
    ozspin_birak(&fd_kilit);
    return f ? new_offset : (uint64_t)-1;
}

/* Reap'ten: olen gorevin ACIK birakilmis dosyalarini kapatir. Yalniz
 * fd_kilit (donen) alir; uyumaz. Kapatilan sayiyi doner. */
int vfs_gorev_bitti(uint32_t gorev_id) {
    int n = 0;
    ozspin_al(&fd_kilit);
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++)
        if (open_files[i].active && open_files[i].sahip == gorev_id) {
            open_files[i].active = 0;
            n++;
        }
    ozspin_birak(&fd_kilit);
    return n;
}

/* Sinama: acik tanitici sayisi (sahip fark etmez). */
int vfs_acik_sayisi(void) {
    int n = 0;
    ozspin_al(&fd_kilit);
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) if (open_files[i].active) n++;
    ozspin_birak(&fd_kilit);
    return n;
}

/* =====================================================================
 * Asama 42: vfs_stat — dosya var mi, boyu ne?
 * FAT16 adlari 8.3 ve BUYUK harflidir; kullanici kucuk harfle sorar.
 * Karsilastirma harf duyarsiz yapilir, yoksa /belge1.txt hic bulunamaz.
 * ===================================================================== */
static char buyut(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

/* Kok olmayan bag noktasi (orn. /fat32): ust dizini O dosya sisteminin
 * list_dir'i ile listeleyip adi harf duyarsiz arar. */
static int bagli_stat(vfs_mount_t *m, const char *rel, uint64_t *boyut, int *dizin_mi) {
    if (rel[0] == '/' && rel[1] == 0) {                 /* bag noktasinin kendisi */
        if (boyut) *boyut = 0;
        if (dizin_mi) *dizin_mi = 1;
        return 0;
    }
    int n = str_len(rel), son = 0;
    while (n > 1 && rel[n - 1] == '/') n--;             /* sondaki '/' */
    for (int i = 0; i < n; i++) if (rel[i] == '/') son = i;
    char ust[VFS_MAX_PATH];
    if (son == 0) { ust[0] = '/'; ust[1] = 0; }
    else {
        if (son >= VFS_MAX_PATH) return -1;
        for (int i = 0; i < son; i++) ust[i] = rel[i];
        ust[son] = 0;
    }
    const char *ad = rel + son + 1;
    int ad_n = n - son - 1;
    if (ad_n <= 0 || !m->ops->list_dir) return -1;
    static vfs_dirent_t girdi[32];                      /* depo_kilit altinda */
    int k = m->ops->list_dir(ust, girdi, 32, m->fs_ctx);
    for (int i = 0; i < k; i++) {
        const char *a = girdi[i].name;
        int j = 0;
        while (j < ad_n && a[j] && buyut(a[j]) == buyut(ad[j])) j++;
        if (j == ad_n && a[j] == 0) {
            if (boyut) *boyut = girdi[i].size;
            if (dizin_mi) *dizin_mi = girdi[i].is_dir;
            return 0;
        }
    }
    return -1;
}

static int vfs_stat__ic(const char *path, uint64_t *boyut, int *dizin_mi) {
    if (!path || path[0] != '/') return -1;
    {
        const char *rel;
        vfs_mount_t *m = resolve_mount(path, &rel);
        if (m && str_len(m->mount_point) > 1) return bagli_stat(m, rel, boyut, dizin_mi);
    }
    {
        /* Alt dizin katmani tam yolu cozebiliyorsa onu kullan. */
        fat16_dirent_t d;
        if (fat16_yol_bul(path, &d) == 0) {
            if (boyut)    *boyut = d.size;
            if (dizin_mi) *dizin_mi = d.is_dir;
            return 0;
        }
    }
    const char *ad = path + 1;
    vfs_dirent_t girdi[32];
    int n = vfs_list_dir("/", girdi, 32);
    if (n <= 0) return -1;
    for (int i = 0; i < n; i++) {
        const char *a = girdi[i].name;
        const char *b = ad;
        while (*a && *b && buyut(*a) == buyut(*b)) { a++; b++; }
        if (*a == 0 && *b == 0) {
            if (boyut)   *boyut = girdi[i].size;
            if (dizin_mi) *dizin_mi = girdi[i].is_dir;
            return 0;
        }
    }
    return -1;
}

/* =====================================================================
 * Asama 42: dizin olusturma.
 * BITIG(FAT16) artik alt dizin destekliyor; "desteksiz" donmek yerine
 * gercekten olusturur. Basarisizlik kodlari surucuden gelir.
 * ===================================================================== */
static int vfs_mkdir__ic(const char *path) {
    if (!path || path[0] != '/') return -1;
    return fat16_dizin_yarat(path);
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
int vfs_list_dir(const char *path, vfs_dirent_t *out, int max) {
    ozkilit_al(&depo_kilit);
    int r = vfs_list_dir__ic(path, out, max);
    ozkilit_birak(&depo_kilit);
    return r;
}

int vfs_stat(const char *path, uint64_t *boyut, int *dizin_mi) {
    ozkilit_al(&depo_kilit);
    int r = vfs_stat__ic(path, boyut, dizin_mi);
    ozkilit_birak(&depo_kilit);
    return r;
}

int vfs_mkdir(const char *path) {
    ozkilit_al(&depo_kilit);
    int r = vfs_mkdir__ic(path);
    ozkilit_birak(&depo_kilit);
    return r;
}

int vfs_open(const char *path) {
    ozkilit_al(&depo_kilit);
    int r = vfs_open__ic(path);
    ozkilit_birak(&depo_kilit);
    return r;
}

int vfs_read(int fd, void *buf, uint64_t size) {
    ozkilit_al(&depo_kilit);
    int r = vfs_read__ic(fd, buf, size);
    ozkilit_birak(&depo_kilit);
    return r;
}

int vfs_close(int fd) {
    ozkilit_al(&depo_kilit);
    int r = vfs_close__ic(fd);
    ozkilit_birak(&depo_kilit);
    return r;
}

uint64_t vfs_lseek(int fd, uint64_t new_offset) {
    ozkilit_al(&depo_kilit);
    uint64_t r = vfs_lseek__ic(fd, new_offset);
    ozkilit_birak(&depo_kilit);
    return r;
}
