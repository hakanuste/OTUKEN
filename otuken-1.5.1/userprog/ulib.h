#ifndef OTUKEN_ULIB_H
#define OTUKEN_ULIB_H

/* ÖTÜKEN userland calisma zamani (libc benzeri temel katman).
 * Cekirdek kutuphanesi yoktur; her sey syscall'lar uzerine kuruludur. */

typedef unsigned long  size_t_u;
typedef unsigned char  uint8_u;
typedef unsigned int   uint32_u;
typedef unsigned long  uint64_u;
typedef long           ssize_u;

/* --- syscall numaralari (cekirdekle AYNI olmali) --- */
#define SYS_WRITE       0
#define SYS_GETPID      1
#define SYS_YIELD       2
#define SYS_SLEEP       3
#define SYS_RING3_PROOF 4
#define SYS_EXIT        5
#define SYS_OPEN        6
#define SYS_READ        7
#define SYS_CLOSE       8
#define SYS_SBRK        9
#define SYS_READKEY    10
#define SYS_LISTDIR    11
#define SYS_PUTPIXEL   12
#define SYS_TICKS      13
#define SYS_PENCERE_AC   14
#define SYS_PEN_DOLDUR   15
#define SYS_PEN_YAZI     16
#define SYS_PEN_DIK      17
#define SYS_PEN_KAPAT    18
#define SYS_SERVIS_LISTE 19
#define SYS_IPC_ACS      20
#define SYS_IPC_BUL      21
#define SYS_IPC_GONDER   22
#define SYS_IPC_AL       23
#define SYS_SISTEM_BIL   24

#define SYSBIL_BOS_CERCEVE   0
#define SYSBIL_TOPLAM_BELLEK 1
#define SYSBIL_GOREV_SAYISI  2
#define SYSBIL_TIK           3

#define IPC_MAX_PAYLOAD_U 96
typedef struct {
    uint32_u opcode;
    uint32_u length;
    uint32_u sender_port;
    uint32_u sequence;
    uint8_u  payload[IPC_MAX_PAYLOAD_U];
} ipc_mesaj_u;

typedef struct {
    char     name[32];
    uint64_u size;
    uint32_u is_dir;
    uint32_u reserved;
} dirent_u;

/* --- ham syscall --- */
long syscall(long num, long a1, long a2, long a3, long a4);

/* --- surec --- */
void  exit(void) __attribute__((noreturn));
long  getpid(void);
void  yield(void);
void  sleep_ticks(long ticks);
long  ticks(void);

/* --- cikti --- */
void  puts(const char *s);
void  putchar(char c);
void  print_dec(uint64_u v);
void  print_hex(uint64_u v);

/* --- giris --- */
char  readkey(void);        /* bloklamaz: tus yoksa 0 */
char  readkey_blocking(void);
int   readline(char *buf, int max);   /* Enter'a kadar okur, echo yapar */

/* --- dosya --- */
int    open(const char *path);
ssize_u read(int fd, void *buf, size_t_u size);
int    close(int fd);
int    listdir(const char *path, dirent_u *out, int max);

/* --- bellek --- */
void  *malloc(size_t_u size);
void   free(void *p);
void  *memset(void *dst, int val, size_t_u n);
void  *memcpy(void *dst, const void *src, size_t_u n);

/* --- dize --- */
size_t_u strlen(const char *s);
int      strcmp(const char *a, const char *b);
int      strncmp(const char *a, const char *b, size_t_u n);
char    *strcpy(char *dst, const char *src);

/* --- grafik --- */
void  putpixel(int x, int y, uint32_u rgb);

/* --- IPC (servisler arasi iletisim) --- */
int  ipc_ac(const char *isim);
int  ipc_bul(const char *isim);
int  ipc_gonder(int hedef, uint32_u opkod, const void *veri, uint32_u uzunluk);
int  ipc_al(int port, ipc_mesaj_u *cikti);

/* --- Sistem bilgisi --- */
uint64_u sistem_bil(int alan);

/* --- Pencere (ULGEN) --- */
int  pencere_ac(const char *baslik, int x, int y, int w, int h);
void pencere_doldur(int id, uint32_u renk);
void pencere_yazi(int id, int x, int y, const char *metin, uint32_u renk);

/* --- Asama 34: Uygulama API'si (ULGEN pencereleri) ---
 * Uygulama ekrani HIC bilmez; yalnizca kendi penceresine cizer.
 * Compositor pencereleri ekranda birlestirir. */
typedef struct {
    char     name[20];
    uint32_u durum;
    uint32_u nabiz;
} servis_bilgi_u;

int  pencere_ac(const char *baslik, int x, int y, int w, int h);
void pencere_doldur(int id, uint32_u renk);
void pencere_yazi(int id, int x, int y, const char *s, uint32_u renk);
void pencere_dikdortgen(int id, int x, int y, int w, int h, uint32_u renk);
void pencere_kapat(int id);
int  servis_listesi(servis_bilgi_u *out, int max);

#endif
