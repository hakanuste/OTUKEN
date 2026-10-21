#include "vfs.h"
#include "sched.h"
#include "depo_kilit.h"

ozkilit_t depo_kilit;   /* bkz. depo_kilit.h */
#include "fat16_ic.h"
#include "heap.h"
#include "serial.h"

typedef struct {
    int active;
    char mount_point[VFS_MAX_PATH];
    const vfs_ops_t *ops;
    void *fs_ctx;
} vfs_mount_t;

typedef struct {
    int active;
    vfs_mount_t *mount;
    char path[VFS_MAX_PATH]; /* mount noktasina GORE (bagimsiz) yol */
    uint64_t offset;
    uint64_t size;
    uint32_t sahip;          /* fd'yi acan gorev kimligi (0xFFFFFFFF: gorevsiz) */
} vfs_file_t;

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_file_t  open_files[VFS_MAX_OPEN_FILES];

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
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].active = 0;
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
        /* "/fat32" yalniz "/fat32" ya da "/fat32/..." ile eslesir; "/fat32x" ile degil */
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

    int slot = -1;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!open_files[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    open_files[slot].active = 1;
    open_files[slot].mount = m;
    str_copy(open_files[slot].path, relative, VFS_MAX_PATH);
    open_files[slot].offset = 0;
    open_files[slot].size = filesize;
    { task_t *cg = scheduler_current_task(); open_files[slot].sahip = cg ? cg->id : 0xFFFFFFFFu; }

    return slot;
}

static int vfs_read__ic(int fd, void *buf, uint64_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !open_files[fd].active) return -1;
    vfs_file_t *f = &open_files[fd];

    uint64_t filesize = 0;
    int n = f->mount->ops->read_file(f->path, (uint8_t *)buf, size,
                                      f->offset, &filesize, f->mount->fs_ctx);
    if (n < 0) return -1;

    f->offset += (uint64_t)n;
    return n;
}

static int vfs_close__ic(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !open_files[fd].active) return -1;
    open_files[fd].active = 0;
    return 0;
}

static uint64_t vfs_lseek__ic(int fd, uint64_t new_offset) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !open_files[fd].active) return (uint64_t)-1;
    open_files[fd].offset = new_offset;
    return new_offset;
}

/* =====================================================================
 * Asama 42: vfs_stat — dosya var mi, boyu ne?
 * FAT16 adlari 8.3 ve BUYUK harflidir; kullanici kucuk harfle sorar.
 * Karsilastirma harf duyarsiz yapilir, yoksa /belge1.txt hic bulunamaz.
 * ===================================================================== */
static char buyut(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int vfs_stat__ic(const char *path, uint64_t *boyut, int *dizin_mi) {
    if (!path || path[0] != '/') return -1;
    {
        const char *goreli;
        vfs_mount_t *m = resolve_mount(path, &goreli);
        if (m && m->ops->stat) return m->ops->stat(goreli, boyut, dizin_mi, m->fs_ctx);
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
    const char *goreli;
    vfs_mount_t *m = resolve_mount(path, &goreli);
    if (!m || !m->ops->mkdir) return -1;
    return m->ops->mkdir(goreli, m->fs_ctx);
}

static int vfs_write_file__ic(const char *path, const void *data, uint64_t size) {
    if (!path || path[0] != '/') return -1;
    const char *goreli;
    vfs_mount_t *m = resolve_mount(path, &goreli);
    if (!m || !m->ops->write_file) return -1;
    return m->ops->write_file(goreli, (const uint8_t *)data, size, m->fs_ctx);
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

int vfs_write_file(const char *path, const void *data, uint64_t size) {
    ozkilit_al(&depo_kilit);
    int r = vfs_write_file__ic(path, data, size);
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

int vfs_fd_gorevin_mi(int fd, uint32_t gorev_id) {
    ozkilit_al(&depo_kilit);
    int r = (fd >= 0 && fd < VFS_MAX_OPEN_FILES && open_files[fd].active &&
             open_files[fd].sahip == gorev_id) ? 1 : 0;
    ozkilit_birak(&depo_kilit);
    return r;
}

void vfs_gorev_dosyalarini_kapat(uint32_t gorev_id) {
    ozkilit_al(&depo_kilit);
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++)
        if (open_files[i].active && open_files[i].sahip == gorev_id)
            open_files[i].active = 0;
    ozkilit_birak(&depo_kilit);
}
