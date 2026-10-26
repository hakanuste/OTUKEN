#include "gui.h"
#include "ulgen.h"
#include "fb.h"
#include "mouse.h"
#include "fat16.h"
#include "log.h"
#include "gorsel_kilit.h"
#include "ekran.h"
#include "uygulama.h"
#include "kervan2.h"
#include "keyboard.h"
#include "ulgen_theme.h"
#include "tema.h"
#include "timer.h"
#include "vfs.h"

/* --- Renkler --- */
#define COL_DESKTOP        0x123B4A
#define COL_TASKBAR        0x0D1F27
#define COL_WINBG          0xE8E4DA
#define COL_TITLEBAR       0x2E5B4F
#define COL_TITLEBAR_FOCUS 0x3E8B72
#define COL_TITLEFG        0xFFFFFF
#define COL_BORDER         0x0D1F27
#define COL_TEXTFG         0x1A1A1A
#define COL_TASKFG         0xE8E4DA
#define COL_TASKBTN        0x1A3038
#define COL_TASKBTN_ACTIVE 0x2E5B4F
#define COL_CURSOR         0xFFFFFF
#define COL_CLOSEBOX       0xB33A3A

#define TITLEBAR_H 22
#define TASKBAR_H  GUI_GOREV_CUBUGU_H
#define TASKBTN_W  150

#define MAX_WINDOWS  4
#define WIN_TERMINAL 0
#define WIN_FILEMGR  1
#define WIN_VIEWER   2
#define WIN_LOG      3

typedef struct {
    int x, y, w, h;
    char title[40];
    int visible;
    int z;          /* 1.4.8: ULGEN yuzeyleriyle ORTAK z (ulgen_z_ver) */
} window_t;

static window_t windows[MAX_WINDOWS];
static int zorder[MAX_WINDOWS];

static int dragging = -1;
static int yuzey_surukle = -1;          /* surukelenen ULGEN yuzeyi */
static int drag_off_x = 0, drag_off_y = 0;
static int prev_buttons = 0;

/* --- Terminal penceresi durumu --- */
#define TERM_COLS 50
#define TERM_ROWS 20
/* 1.4.8: terminal HARF (kod noktasi) hucreleri tutar. Eskiden bayt
 * tutuyordu: satir 50 BAYTTA kiriliyordu, cok baytli Turkce harf iki
 * satira bolunup iki bozuk glif ("A" + "±") olarak ciziliyordu. */
static uint32_t term_buf[TERM_ROWS][TERM_COLS];
static int  term_row = 0, term_col = 0;
/* UTF-8 cozucu durumu (baytlar tek tek gelir; ISO-8859-9 tek bayt da olabilir) */
static uint32_t u8_kod;
static int      u8_kalan, u8_bas;

/* --- Dosya Yoneticisi durumu ---
 * 1.4.9: VFS yoluyla listelenir (alt dizinler ve /fat32 dahil), pencere
 * her acilista tazelenir, sigmayan liste sayfalanir. Eskiden kok dizinin
 * ilk 16 girdisi ACILISTA bir kez okunuyor, pencere ilk ~10'unu
 * gosteriyordu; sonradan yazilan dosyalar ve alt dizinler hic gorunmuyordu. */
#define MAX_FILES 96
static vfs_dirent_t file_list[MAX_FILES];
static int file_count = 0;
static int fat16_ok = 0;
static char fm_yol[VFS_MAX_PATH] = "/";
static int fm_bas = 0;                   /* ilk gosterilen girdi */

/* --- Goruntuleyici durumu --- */
#define VIEWER_COLS 50
#define VIEWER_ROWS 11
static char viewer_lines[VIEWER_ROWS][VIEWER_COLS + 1];

static int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void u32_to_dec_str(uint32_t v, char *out) {
    char tmp[12];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

static void term_clear(void) {
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++) term_buf[r][c] = ' ';
    term_row = 0;
    term_col = 0;
    u8_kalan = 0;
}

static void term_scroll(void) {
    for (int r = 1; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            term_buf[r - 1][c] = term_buf[r][c];
    for (int c = 0; c < TERM_COLS; c++) term_buf[TERM_ROWS - 1][c] = ' ';
}

static void viewer_clear(void) {
    for (int r = 0; r < VIEWER_ROWS; r++) {
        for (int c = 0; c < VIEWER_COLS; c++) viewer_lines[r][c] = ' ';
        viewer_lines[r][VIEWER_COLS] = 0;
    }
}

static void viewer_load(const char *yol, const char *ad) {
    static uint8_t filebuf[4096];
    int n = 0;
    int fd = vfs_open(yol);
    if (fd >= 0) {
        int r;
        while (n < (int)sizeof(filebuf) - 1 &&
               (r = vfs_read(fd, filebuf + n, sizeof(filebuf) - 1 - (uint64_t)n)) > 0) n += r;
        vfs_close(fd);
    }
    filebuf[n] = 0;

    viewer_clear();
    int row = 0, col = 0;
    for (int i = 0; i < n && row < VIEWER_ROWS; i++) {
        char c = (char)filebuf[i];
        if (c == '\r') continue;
        if (c == '\n') { row++; col = 0; continue; }
        if (col < VIEWER_COLS) viewer_lines[row][col++] = c;
    }
    /* 1.4.8: satir VIEWER_COLS baytta kesilir; sonda yarim kalan cok
     * baytli harf varsa atilir (bozuk glif cizilmez). */
    for (int r = 0; r < VIEWER_ROWS; r++) {
        int son = VIEWER_COLS - 1;
        while (son >= 0 && viewer_lines[r][son] == ' ') son--;
        int bas = son;
        while (bas >= 0 && ((uint8_t)viewer_lines[r][bas] & 0xC0) == 0x80) bas--;
        if (bas >= 0 && (uint8_t)viewer_lines[r][bas] >= 0xC0) {
            int gerek = ((uint8_t)viewer_lines[r][bas] & 0xE0) == 0xC0 ? 2 :
                        ((uint8_t)viewer_lines[r][bas] & 0xF0) == 0xE0 ? 3 : 4;
            if (son - bas + 1 < gerek)
                for (int k = bas; k <= son; k++) viewer_lines[r][k] = ' ';
        }
    }

    str_copy(windows[WIN_VIEWER].title, "Görüntüleyici: ", sizeof(windows[WIN_VIEWER].title));
    int base = str_len(windows[WIN_VIEWER].title);
    str_copy(windows[WIN_VIEWER].title + base, ad,
             (int)sizeof(windows[WIN_VIEWER].title) - base);
}

/* Dosya yoneticisi: gecerli dizini yeniden listeler */
static void fm_yenile(void) {
    int n = vfs_list_dir(fm_yol, file_list, MAX_FILES);
    if (n < 0 && !(fm_yol[0] == '/' && fm_yol[1] == 0)) {      /* dizin gitti: koke don */
        fm_yol[0] = '/'; fm_yol[1] = 0;
        n = vfs_list_dir(fm_yol, file_list, MAX_FILES);
    }
    file_count = 0;
    for (int i = 0; i < n; i++) {                 /* "." ve ".." girdileri gosterilmez */
        const char *a = file_list[i].name;
        if (a[0] == '.' && (a[1] == 0 || (a[1] == '.' && a[2] == 0))) continue;
        if (i != file_count) file_list[file_count] = file_list[i];
        file_count++;
    }
    fat16_ok = n >= 0;
    if (fm_bas >= file_count) fm_bas = 0;
}
static int fm_kokte(void) { return fm_yol[0] == '/' && fm_yol[1] == 0; }
/* yerlesim: satir 0 = yol; sonra (kok degilse) ".." ve girdiler; en alt
 * satir sayfa dugmeleri. Cizim ve tiklama AYNI hesabi kullanir. */
static int fm_sigan(const window_t *w) {
    int s = (w->h - TITLEBAR_H - 12) / FB_SATIR_YUK - 3;   /* yol + bos + sayfa satiri */
    return s < 1 ? 1 : s;
}

static void bring_to_front(int idx) {
    int pos = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) if (zorder[i] == idx) { pos = i; break; }
    if (pos < 0) return;
    for (int i = pos; i < MAX_WINDOWS - 1; i++) zorder[i] = zorder[i + 1];
    zorder[MAX_WINDOWS - 1] = idx;
    windows[idx].z = ulgen_z_ver();      /* ULGEN yuzeylerinin de ustune */
}

static void draw_window(int idx) {
    window_t *w = &windows[idx];
    if (!w->visible) return;

    int focused = (zorder[MAX_WINDOWS - 1] == idx);
    uint32_t tb_color = focused ? COL_TITLEBAR_FOCUS : COL_TITLEBAR;

    fb_fill_rect(w->x, w->y, w->w, w->h, COL_WINBG);
    fb_fill_rect(w->x, w->y, w->w, TITLEBAR_H, tb_color);
    fb_draw_string(w->x + 6, w->y + 3, w->title, COL_TITLEFG, tb_color);

    fb_fill_rect(w->x + w->w - 18, w->y + 4, 14, 14, COL_CLOSEBOX);
    fb_draw_char(w->x + w->w - 15, w->y + 3, 'x', 0xFFFFFF, COL_CLOSEBOX);

    fb_draw_rect(w->x, w->y, w->w, w->h, COL_BORDER);
    fb_draw_rect(w->x, w->y, w->w, TITLEBAR_H, COL_BORDER);

    int cx = w->x + 8;
    int cy = w->y + TITLEBAR_H + 6;

    /* Icerik, pencerenin ic alanina kirpilir: uzun satir ya da fazla
     * satir pencere disina (ve kenarin ustune) tasamaz. */
    fb_kirp_t dis_kirp = fb_kirp_al();
    fb_kirp_daralt(w->x + 1, w->y + TITLEBAR_H, w->w - 2, w->h - TITLEBAR_H - 1);

    if (idx == WIN_TERMINAL) {
        /* sigan satir sayisi kadar, EN YENI satirlar gosterilir */
        int sigan = (w->h - TITLEBAR_H - 12) / FB_SATIR_YUK;
        if (sigan < 1) sigan = 1;
        int ilk = term_row - sigan + 1;
        if (ilk < 0) ilk = 0;
        for (int r = ilk; r < TERM_ROWS && r < ilk + sigan; r++)
            for (int c = 0; c < TERM_COLS; c++)
                fb_kod_ciz(cx + c * FB_YAZI_EN, cy + (r - ilk) * FB_SATIR_YUK,
                           term_buf[r][c], COL_TEXTFG, COL_WINBG);
    } else if (idx == WIN_FILEMGR) {
        if (!fat16_ok) {
            fb_draw_string(cx, cy, "Disk bulunamadı", COL_TEXTFG, COL_WINBG);
        } else {
            fb_draw_string(cx, cy, fm_yol, COL_TEXTFG, COL_WINBG);
            int sigan = fm_sigan(w), satir = 1;
            if (!fm_kokte() && satir <= sigan)
                fb_draw_string(cx, cy + (satir++) * FB_SATIR_YUK, "[..]  üst dizin", COL_TEXTFG, COL_WINBG);
            if (file_count == 0)
                fb_draw_string(cx, cy + satir * FB_SATIR_YUK, "(boş dizin)", COL_TEXTFG, COL_WINBG);
            for (int i = fm_bas; i < file_count && satir <= sigan; i++, satir++) {
                char ad[VFS_MAX_NAME + 4];
                if (file_list[i].is_dir) { ad[0] = '['; str_copy(ad + 1, file_list[i].name, VFS_MAX_NAME); int u = str_len(ad); ad[u] = ']'; ad[u + 1] = 0; }
                else str_copy(ad, file_list[i].name, sizeof ad);
                fb_draw_string(cx, cy + satir * FB_SATIR_YUK, ad, COL_TEXTFG, COL_WINBG);
                if (!file_list[i].is_dir) {
                    char sizebuf[12];
                    u32_to_dec_str((uint32_t)file_list[i].size, sizebuf);
                    fb_draw_string(cx + 17 * FB_YAZI_EN, cy + satir * FB_SATIR_YUK, sizebuf, COL_TEXTFG, COL_WINBG);
                }
            }
            /* sayfa satiri: sol yari "onceki", sag yari "sonraki" */
            int alt = cy + (sigan + 2) * FB_SATIR_YUK;
            char bilgi[48]; char t[12];
            str_copy(bilgi, "", sizeof bilgi);
            u32_to_dec_str((uint32_t)file_count, t);
            str_copy(bilgi, t, sizeof bilgi); int u = str_len(bilgi);
            str_copy(bilgi + u, " girdi · tıkla: aç", (int)sizeof bilgi - u);
            fb_draw_string(cx, alt, fm_bas > 0 ? "◂ önceki" : "        ", COL_TEXTFG, COL_WINBG);
            fb_draw_string(cx + 11 * FB_YAZI_EN, alt, bilgi, COL_TEXTFG, COL_WINBG);
            int sonraki_var = fm_bas + (sigan - (fm_kokte() ? 0 : 1)) < file_count;
            if (sonraki_var) fb_draw_string(cx + w->w - 12 - 12 * FB_YAZI_EN, alt, "sonraki ▸", COL_TEXTFG, COL_WINBG);
        }
    } else if (idx == WIN_VIEWER) {
        for (int r = 0; r < VIEWER_ROWS; r++)
            fb_draw_string(cx, cy + r * FB_SATIR_YUK, viewer_lines[r], COL_TEXTFG, COL_WINBG);
    } else if (idx == WIN_LOG) {
        /* Stage 18: canli cekirdek gunlugu - EN YENI kayit en ustte */
        int max_rows = (w->h - TITLEBAR_H - 14) / FB_SATIR_YUK;
        int n = log_entry_count();
        if (n > max_rows) n = max_rows;
        for (int i = 0; i < n; i++) {
            const log_entry_t *e = log_get_entry(i);
            if (!e) break;
            /* seviyeye gore renk: hata kirmizi, uyari koyu sari */
            uint32_t color = COL_TEXTFG;
            if (e->level == LOG_HATA)  color = 0xB33A3A;
            if (e->level == LOG_UYARI) color = 0x8A6D1F;

            /* 1.4.8: satir tamponu iletinin TAMAMINI alir; pencereye sigmayan
             * kisim kirpilir. Eskiden 68. BAYTTA kesiliyordu: cok baytli
             * harf ortadan bolunebiliyordu. */
            char line[160];
            int p = 0;
            line[p++] = '[';
            const char *lv = log_level_name(e->level);
            for (int k = 0; lv[k] && p < 20; k++) line[p++] = lv[k];
            line[p++] = ']';
            line[p++] = ' ';
            for (int k = 0; e->subsystem[k] && p < 40; k++) line[p++] = e->subsystem[k];
            line[p++] = ':';
            line[p++] = ' ';
            for (int k = 0; e->message[k] && p < (int)sizeof(line) - 1; k++) line[p++] = e->message[k];
            line[p] = 0;
            fb_draw_string(cx, cy + i * FB_SATIR_YUK, line, color, COL_WINBG);
        }
        if (n == 0)
            fb_draw_string(cx, cy, "Günlük boş", COL_TEXTFG, COL_WINBG);
    }
    fb_kirp_geri(dis_kirp);
}

/* --- Birlestirici icin (gorsel_kilit tutulurken) --- */
void gui_pencere_ciz_ic(int idx) {
    if (idx >= 0 && idx < MAX_WINDOWS) draw_window(idx);
}
int gui_pencere_z(int idx) {
    return (idx >= 0 && idx < MAX_WINDOWS) ? windows[idx].z : 0;
}

#define YUZEY_BTN_W 96
/* Gorev cubugu etiketi: basligin " — " oncesi (ornegin "TULPAR"), en cok
 * 10 harf; cok baytli harf ortadan bolunmez. */
static void gorev_adi(char *o, int n, const char *baslik) {
    int i = 0, harf = 0;
    while (baslik[i] && i < n - 1 && harf < 10) {
        if ((uint8_t)baslik[i] == 0xE2 && (uint8_t)baslik[i + 1] == 0x80) break;   /* — */
        if (baslik[i] == ' ' && baslik[i + 1] == '-') break;
        int u = 1;
        uint8_t c = (uint8_t)baslik[i];
        if (c >= 0xF0) u = 4; else if (c >= 0xE0) u = 3; else if (c >= 0xC0) u = 2;
        if (i + u > n - 1) break;
        for (int k = 0; k < u; k++) o[i + k] = baslik[i + k];
        i += u; harf++;
    }
    while (i > 0 && o[i - 1] == ' ') i--;
    o[i] = 0;
}

static void draw_taskbar(void) {
    int sw = fb_width();
    int sh = fb_height();
    static const char *labels[MAX_WINDOWS] = {
        "Terminal",
        "Dosya Yöneticisi",
        "Görüntüleyici",
        "Günlük"
    };

    fb_fill_rect(0, sh - TASKBAR_H, sw, TASKBAR_H, COL_TASKBAR);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        int bx = 8 + i * (TASKBTN_W + 6);
        int active = windows[i].visible;
        uint32_t bg = active ? COL_TASKBTN_ACTIVE : COL_TASKBTN;
        fb_fill_rect(bx, sh - TASKBAR_H + 3, TASKBTN_W, TASKBAR_H - 6, bg);
        fb_draw_string(bx + 6, sh - TASKBAR_H + 3 + 3, labels[i], COL_TASKFG, bg);
    }

    /* 1.4.9: acik uygulama pencereleri (ULGEN yuzeyleri) de gorev
     * cubugunda; tiklama one getirir ve klavyeyi ona verir. */
    const char *tag = "ÖTÜKEN · 64-bit";
    int sinir = sw - 16 - fb_yazi_en(tag);
    int bx = 8 + MAX_WINDOWS * (TASKBTN_W + 6);
    int odak = ulgen_odak_yuzey();
    for (int id = 0; id < ULGEN_MAX_YUZEY; id++) {
        const ulgen_yuzey_t *y = ulgen_yuzey_al(id);
        if (!y || !y->used || !y->gorunur) continue;
        if (bx + YUZEY_BTN_W > sinir) break;
        char ad[ULGEN_BASLIK_LEN];
        gorev_adi(ad, sizeof ad, y->baslik);
        uint32_t bg = (id == odak && klavye_odak() == KLAVYE_ODAK_PENCERE) ? COL_TASKBTN_ACTIVE : COL_TASKBTN;
        fb_fill_rect(bx, sh - TASKBAR_H + 3, YUZEY_BTN_W, TASKBAR_H - 6, bg);
        fb_draw_string(bx + 6, sh - TASKBAR_H + 3 + 3, ad, COL_TASKFG, bg);
        bx += YUZEY_BTN_W + 6;
    }
    fb_draw_string(sw - 8 - fb_yazi_en(tag), sh - TASKBAR_H + 6, tag, COL_TASKFG, COL_TASKBAR);
}

static void draw_cursor(int x, int y) {
    static const uint8_t cursor_shape[8] = {
        0b10000000, 0b11000000, 0b10100000, 0b10010000,
        0b10001000, 0b10111000, 0b10100000, 0b00010000,
    };
    for (int r = 0; r < 8; r++) {
        uint8_t bits = cursor_shape[r];
        for (int c = 0; c < 8; c++)
            if (bits & (1 << (7 - c)))
                fb_put_pixel(x + c, y + r, COL_CURSOR);
    }
}

/* Gorev cubugu ve imlec: birlestirici en son cizer (her zaman ustte). */
void gui_ust_katman_ciz_ic(void) {
    /* Her katman KENDI dikdortgenine kirpilir: ust cubuktaki tamga gorseli
     * verilen boydan uzun cizilip cubugun altina (pencerelerin ustune)
     * tasiyordu (ekran sinamasi yakaladi). */
    fb_kirp_t k = fb_kirp_al();
    fb_kirp_daralt(0, 0, fb_width(), ULGEN_UST_BAR);
    ulgen_theme_ust_bar_ciz();         /* ust cubuk: her zaman ustte */
    fb_kirp_geri(k);
    fb_kirp_daralt(0, fb_height() - TASKBAR_H, fb_width(), TASKBAR_H);
    draw_taskbar();
    fb_kirp_geri(k);
    if (ulgen_theme_uyg_menu_acik()) {       /* 1.4.9: Uygulamalar menüsü */
        int ux, uy, uw, uh;
        ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
        fb_kirp_daralt(ux, uy, uw, uh);
        ulgen_theme_uyg_menu_ciz();
        fb_kirp_geri(k);
    }
    int mx, my, mb;
    mouse_get_state(&mx, &my, &mb);
    (void)mb;
    draw_cursor(mx, my);
}

/* Tam ekran: birlestiriciye tum ekran hasari */
static void redraw_all(void) {
    ekran_tam_hasar();
    ekran_isle();
}

static void handle_taskbar_click(int mx) {
    {   /* uygulama pencereleri (draw_taskbar ile ayni yerlesim) */
        int sw = fb_width();
        int sinir = sw - 16 - fb_yazi_en("ÖTÜKEN · 64-bit");
        int bx = 8 + MAX_WINDOWS * (TASKBTN_W + 6);
        for (int id = 0; id < ULGEN_MAX_YUZEY; id++) {
            const ulgen_yuzey_t *y = ulgen_yuzey_al(id);
            if (!y || !y->used || !y->gorunur) continue;
            if (bx + YUZEY_BTN_W > sinir) break;
            if (mx >= bx && mx < bx + YUZEY_BTN_W) {
                ulgen_one_getir(id);
                klavye_odak_ata(KLAVYE_ODAK_PENCERE);
                return;
            }
            bx += YUZEY_BTN_W + 6;
        }
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        int bx = 8 + i * (TASKBTN_W + 6);
        if (mx >= bx && mx < bx + TASKBTN_W) {
            if (windows[i].visible && zorder[MAX_WINDOWS - 1] == i) {
                windows[i].visible = 0;
            } else {
                if (i == WIN_FILEMGR) fm_yenile();
                windows[i].visible = 1;
                bring_to_front(i);
            }
            return;
        }
    }
}

static void varsayilan_yerlesim(void) {
    windows[WIN_TERMINAL].x = 126; windows[WIN_TERMINAL].y = ULGEN_UST_BAR + 8;
    windows[WIN_TERMINAL].w = 318; windows[WIN_TERMINAL].h = 352;
    windows[WIN_FILEMGR].x = 160; windows[WIN_FILEMGR].y = 110;
    windows[WIN_FILEMGR].w = 460; windows[WIN_FILEMGR].h = 360;
    windows[WIN_VIEWER].x = 260; windows[WIN_VIEWER].y = 170;
    windows[WIN_VIEWER].w = 460; windows[WIN_VIEWER].h = 260;
    windows[WIN_LOG].x = 126; windows[WIN_LOG].y = 410;
    windows[WIN_LOG].w = 886; windows[WIN_LOG].h = 300;
}

void gui_init(void) {
    int sw = fb_width();
    int sh = fb_height();
    (void)sw; (void)sh;

    /* 1.4.8 duzen: terminal solda, UMAY penceresi (ULGEN) sagda,
     * gunluk altta; dosya yoneticisi ve goruntuleyici gorev cubugundan
     * acilir. Pencereler birbirinin ustune binmez. */
    varsayilan_yerlesim();
    str_copy(windows[WIN_TERMINAL].title, "Terminal", sizeof(windows[WIN_TERMINAL].title));
    windows[WIN_TERMINAL].visible = 1;

    str_copy(windows[WIN_FILEMGR].title, "Dosya Yöneticisi", sizeof(windows[WIN_FILEMGR].title));
    windows[WIN_FILEMGR].visible = 0;     /* gorev cubugundan acilir */

    str_copy(windows[WIN_VIEWER].title, "Görüntüleyici (boş)", sizeof(windows[WIN_VIEWER].title));
    windows[WIN_VIEWER].visible = 0;      /* dosya acilinca gorunur */

    str_copy(windows[WIN_LOG].title, "Çekirdek günlüğü (canlı)",
             sizeof(windows[WIN_LOG].title));
    windows[WIN_LOG].visible = 0;         /* gorev cubugundan acilir; rihtim gorunsun */

    zorder[0] = WIN_FILEMGR;
    zorder[1] = WIN_VIEWER;
    zorder[2] = WIN_TERMINAL;
    zorder[3] = WIN_LOG;
    for (int i = 0; i < MAX_WINDOWS; i++) windows[zorder[i]].z = ulgen_z_ver();

    term_clear();
    viewer_clear();

    tema_init();
    ekran_masaustu_hazirla();          /* duvar kagidi + simgeler + rihtim */

    fat16_ok = (fat16_mount() == 0);
    if (fat16_ok) fm_yenile();
    else file_count = 0;

    redraw_all();
}

/* Asama 25 performans duzeltmesi: bir dizeyi terminal tamponuna
 * islerken HER KARAKTERDE tum ekrani yeniden cizmek felaket yavastir
 * (1024x768 = ~786 bin piksel islemi / karakter). Once tamponu
 * guncelleyip SONUNDA TEK BIR yeniden cizim yapiyoruz. */
static void term_satir_atla(void) {
    term_col = 0;
    term_row++;
    if (term_row >= TERM_ROWS) { term_scroll(); term_row = TERM_ROWS - 1; }
}

/* Pencereye sigan sutun (en fazla TERM_COLS): satir PENCERE eninde kirilir */
static int term_sutun(void) {
    int c = (windows[WIN_TERMINAL].w - 16) / FB_YAZI_EN;
    return c < 1 ? 1 : (c > TERM_COLS ? TERM_COLS : c);
}

static void term_kod_koy(uint32_t k) {
    if (k == 13) { term_satir_atla(); return; }
    if (k == 8) {
        if (term_col > 0) { term_col--; term_buf[term_row][term_col] = ' '; }
        return;
    }
    term_buf[term_row][term_col] = k;
    term_col++;
    if (term_col >= term_sutun()) term_satir_atla();
}

static uint32_t latin5_kod(uint32_t b) {
    switch (b) {
    case 0xD0: return 0x011E; case 0xDD: return 0x0130; case 0xDE: return 0x015E;
    case 0xF0: return 0x011F; case 0xFD: return 0x0131; case 0xFE: return 0x015F;
    default: return b;
    }
}

/* Bayt akisini harflere cevirir. Gecersiz dizide bekleyen bayt ISO-8859-9 sayilir. */
static void term_put_nodraw(char ch) {
    uint32_t b = (uint8_t)ch;
    if (u8_kalan) {
        if ((b & 0xC0) == 0x80) {
            u8_kod = (u8_kod << 6) | (b & 0x3F);
            if (--u8_kalan == 0) term_kod_koy(u8_kod);
            return;
        }
        u8_kalan = 0;
        term_kod_koy(latin5_kod((uint32_t)u8_bas));    /* yarim dizi: tek bayt harf */
    }
    if (b < 0x80) { term_kod_koy(b); return; }
    if ((b & 0xE0) == 0xC0) { u8_kod = b & 0x1F; u8_kalan = 1; u8_bas = (int)b; return; }
    if ((b & 0xF0) == 0xE0) { u8_kod = b & 0x0F; u8_kalan = 2; u8_bas = (int)b; return; }
    term_kod_koy(latin5_kod(b));
}

static void terminal_hasar(void) {
    window_t *t = &windows[WIN_TERMINAL];
    if (t->visible) { ekran_hasar(t->x, t->y, t->w, t->h); ekran_isle(); }
}

static void gui_write_string__ic(const char *s) {
    for (int i = 0; s[i]; i++) {
        term_put_nodraw(s[i] == '\n' ? (char)13 : s[i]);
    }

    /* 1.4.8: yalniz terminalin dikdortgeni HASARLI isaretlenir ve
     * birlestirici o bolgeyi z-sirasiyla yeniden cizer. Eskiden terminal
     * "en usttekiyse" dogrudan ciziliyordu: GUI'nin kendi sirasinda en
     * ustte olmasi, ULGEN yuzeylerinin (UMAY) USTUNE boyanmasi demekti.
     * Degilse tam ekran cizimi ULGEN yuzeylerini siliyordu. */
    terminal_hasar();
}

static void gui_type_char__ic(char c) {
    term_put_nodraw(c);
    terminal_hasar();
}

/* --- 1.4.8: masaustu tiklamalari (rihtim ve simgeler) --- */
static void pencere_ac(int idx) {
    if (idx == WIN_FILEMGR) fm_yenile();          /* her acilista guncel liste */
    windows[idx].visible = 1;
    bring_to_front(idx);
}

/* Yardim: Goruntuleyicide yerlesik kisa kilavuz */
static void yardim_goster(void) {
    static const char *SATIR[VIEWER_ROWS] = {
        "ÖTÜKEN — kısa kılavuz",
        "Rıhtım: tamgaya tıkla, açılır.",
        "Uygulamalar: 15 tamganın hepsi.",
        "Pencereye tıkla: klavye ona geçer.",
        "Tab: klavye kabuk <-> pencere.",
        "Esc: menüyü ya da uygulamayı kapatır.",
        "Görev çubuğu: açık pencereler.",
        "Dosya: Dosya Yöneticisi açılır.",
        "Düzen: pencereleri yerlerine dizer.",
        "Kabuk: 'yardim' yaz. Klavye Türkçe-Q.",
        "Günlük: görev çubuğunda 'Günlük'."
    };
    viewer_clear();
    for (int r = 0; r < VIEWER_ROWS; r++) {
        int k = 0;
        while (SATIR[r][k] && k < VIEWER_COLS) { viewer_lines[r][k] = SATIR[r][k]; k++; }
    }
    str_copy(windows[WIN_VIEWER].title, "Yardım", sizeof(windows[WIN_VIEWER].title));
    windows[WIN_VIEWER].visible = 1;
    bring_to_front(WIN_VIEWER);
}

static void varsayilan_yerlesim(void);
/* Duzen: GUI pencereleri ilk yerlerine, uygulama pencereleri basamakli */
static void yerlesimi_duzenle(void) {
    varsayilan_yerlesim();
    int n = 0;
    for (int id = 0; id < ULGEN_MAX_YUZEY; id++) {
        const ulgen_yuzey_t *y = ulgen_yuzey_al(id);
        if (!y || !y->used || !y->gorunur) continue;
        int x = 160 + n * 28, yy = ULGEN_UST_BAR + 40 + n * 28;
        int azx = fb_width() - y->w - 8, azy = fb_height() - TASKBAR_H - y->h - 8;
        if (x > azx) x = azx;                          /* ekranda kalsin */
        if (yy > azy) yy = azy;
        if (x < 8) x = 8;
        if (yy < ULGEN_UST_BAR + 24) yy = ULGEN_UST_BAR + 24;   /* baslik cubugu ust cubugun altinda */
        ulgen_tasi(id, x, yy);
        n++;
    }
    ulgen_tam_yenile();
}

static int yuzey_bul(const char *onek) {
    for (int i = 0; i < ULGEN_MAX_YUZEY; i++) {
        const ulgen_yuzey_t *y = ulgen_yuzey_al(i);
        if (!y) continue;
        int k = 0;
        while (onek[k] && y->baslik[k] == onek[k]) k++;
        if (!onek[k]) return i;
    }
    return -1;
}

static void rihtim_ac(int tamga) {
    switch (tamga) {
    case 1: {                                   /* UMAY */
        int id = yuzey_bul("UMAY");
        if (id >= 0) { ulgen_gorunurluk(id, 1); ulgen_one_getir(id); }
        else log_write(LOG_BILGI, "ULGEN", "UMAY henuz acilmadi");
        break;
    }
    case 0:  uygulama_ac(UYG_SISTEM); break;    /* OTUKEN: dizge izleyicisi */
    case 2:  uygulama_ac(UYG_DEDE); break;      /* DEDE KORKUT: soz ve ses (1.4.9) */
    case 3:  uygulama_ac(UYG_GORUNUM); break;   /* ULGEN: gorunum (tema) */
    case 5:  uygulama_ac(UYG_TIMUR); break;     /* TIMUR: korumalar (1.4.9) */
    case 6:  uygulama_ac(UYG_KUT); break;       /* KUT: kimlik (1.4.9) */
    case 10: uygulama_ac(UYG_ULAK); break;      /* ULAK: ag durumu (1.4.9) */
    case 12: uygulama_ac(UYG_TORE); break;      /* TORE: politikalar (1.4.9) */
    case 7:  uygulama_ac(UYG_ASENA); break;     /* ASENA: kurtarma (1.4.9) */
    case 8:  uygulama_ac(UYG_ERGENEKON); break; /* ERGENEKON: yeniden kurulum (1.4.9) */
    case 13: uygulama_ac(UYG_TAMGA); break;     /* TAMGA: dijital kimlik (1.4.9) */
    case 4:  uygulama_ac(UYG_TULPAR); break;    /* TULPAR: ag gezgini (1.4.9) */
    case 11: uygulama_ac(UYG_KERVAN); break;    /* KERVAN: paket yoneticisi (1.4.9) */
    case 9:  pencere_ac(WIN_FILEMGR); break;    /* BITIG: dosyalar */
    case 14: pencere_ac(WIN_TERMINAL); break;   /* ORKUN: gelistirici (kabuk) */
    default:
        log_write(LOG_BILGI, "ULGEN", "bu uygulama henuz yok");
        break;
    }
}

static void masaustu_tikla(int mx, int my) {
    int t = ulgen_theme_dock_hit(mx, my);
    if (t >= 0) { rihtim_ac(t); return; }
    int m = ulgen_theme_simge_hit(mx, my);
    if (m >= 0 && m <= 3) pencere_ac(WIN_FILEMGR);      /* disk ve klasorler */
    else if (m == 4) log_write(LOG_BILGI, "ULGEN", "Cop kutusu bos");
}

static void gui_fare_isle__ic(int mx, int my, int buttons) {
    int sw = fb_width();
    int sh = fb_height();

    int pressed  = (buttons & 1) && !(prev_buttons & 1);
    int released = !(buttons & 1) && (prev_buttons & 1);

    if (pressed && ulgen_theme_uyg_menu_acik()) {
        /* 1.4.9: açık menü tıklamayı yutar: içeride seçim, dışarıda kapanış */
        int ux, uy, uw, uh;
        ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
        int t = ulgen_theme_uyg_menu_hit(mx, my);
        if (t == ULGEN_MENU_PAKET_SAYFA) {                /* sayfa değişir, menü açık kalır */
            ulgen_theme_uyg_menu_sonraki();
            ekran_hasar(ux, uy, uw, uh);
            prev_buttons = buttons; redraw_all(); return;
        }
        int menu_dugmesi = (my < ULGEN_UST_BAR && ulgen_theme_menu_hit(mx, my) == ULGEN_MENU_UYGULAMALAR);
        ulgen_theme_uyg_menu_ata(0);
        ekran_hasar(ux, uy, uw, uh);
        ekran_isle();
        if (t >= ULGEN_MENU_PAKET) {                      /* 1.4.9: kurulu paket uygulamasi */
            char dosya[13];
            if (kervan2_uygulama_al(t - ULGEN_MENU_PAKET, dosya, 0) == 0) uygulama_paket_ac(dosya);
        } else if (t >= 0 && ulgen_theme_uyg_var(t)) rihtim_ac(t);
        if (t != -1 || menu_dugmesi) { prev_buttons = buttons; redraw_all(); return; }
    }
    if (pressed) {
        if (my >= sh - TASKBAR_H) {
            handle_taskbar_click(mx);
        } else if (my < ULGEN_UST_BAR) {
            /* ust cubuk her zaman ustte. 1.4.8: OTUKEN tamgasi ve "Sistem"
             * dizge izleyicisini, "Gorunum" tema seciciyi acar; diger
             * menuler henuz eylem tanimlamaz. */
            int m = ulgen_theme_menu_hit(mx, my);
            if (m == ULGEN_MENU_DOSYA) pencere_ac(WIN_FILEMGR);
            else if (m == ULGEN_MENU_DUZEN) yerlesimi_duzenle();
            else if (m == ULGEN_MENU_YARDIM) yardim_goster();
            else if (m == ULGEN_MENU_TAMGA || m == ULGEN_MENU_SISTEM) uygulama_ac(UYG_SISTEM);
            else if (m == ULGEN_MENU_GORUNUM) uygulama_ac(UYG_GORUNUM);
            else if (m == ULGEN_MENU_UYGULAMALAR) {
                int ux, uy, uw, uh;
                ulgen_theme_uyg_menu_ata(1);
                ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
                ekran_hasar(ux, uy, uw, uh);
                ekran_isle();
            }
        } else {
            /* 1.4.8: EN USTTEKI oge ORTAK z ile secilir. Eskiden yalniz GUI
             * pencerelerine bakiliyordu: UMAY penceresine tiklamak altindaki
             * terminali one getiriyor, UMAY hic olay almiyordu. */
            int gui_idx = -1, gui_z = -1;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                window_t *w = &windows[i];
                if (w->visible && mx >= w->x && mx < w->x + w->w &&
                    my >= w->y && my < w->y + w->h && w->z > gui_z) { gui_z = w->z; gui_idx = i; }
            }
            int yid = ulgen_noktadaki_yuzey(mx, my);
            const ulgen_yuzey_t *ys = yid >= 0 ? ulgen_yuzey_al(yid) : 0;
            if (ys && ys->z > gui_z) {
                ulgen_one_getir(yid);
                /* 1.4.9: odak tiklamayi izler (Tab da hala calisir) */
                if (klavye_odak() != KLAVYE_ODAK_PENCERE) klavye_odak_ata(KLAVYE_ODAK_PENCERE);
                if (ulgen_kapat_dugmesi_mi(yid, mx, my))
                    ulgen_olay_it(yid, ULGEN_OLAY_KAPAT, 0, 0, 0);
                else if (my < ys->y) {                        /* baslik: surukle */
                    yuzey_surukle = yid;
                    drag_off_x = mx - ys->x;
                    drag_off_y = my - ys->y;
                } else
                    ulgen_olay_it(yid, ULGEN_OLAY_TIK, 0, mx - ys->x, my - ys->y);
            }
            for (int i = MAX_WINDOWS - 1; i >= 0 && !(ys && ys->z > gui_z); i--) {
                int idx = zorder[i];
                window_t *w = &windows[idx];
                if (idx != gui_idx) continue;
                {
                    bring_to_front(idx);
                    if (idx == WIN_TERMINAL && klavye_odak() != KLAVYE_ODAK_KABUK)
                        klavye_odak_ata(KLAVYE_ODAK_KABUK);           /* 1.4.9 */
                    if (my < w->y + TITLEBAR_H) {
                        if (mx >= w->x + w->w - 18 && mx < w->x + w->w - 4) {
                            w->visible = 0;
                        } else {
                            dragging = idx;
                            drag_off_x = mx - w->x;
                            drag_off_y = my - w->y;
                        }
                    } else if (idx == WIN_FILEMGR && fat16_ok) {
                        int cy = w->y + TITLEBAR_H + 6;
                        int row = (my - cy) / FB_SATIR_YUK;
                        int sigan = fm_sigan(w);
                        int gorunen = sigan - (fm_kokte() ? 0 : 1);   /* girdi satiri sayisi */
                        if (row == sigan + 2) {                        /* sayfa satiri */
                            if (mx < w->x + w->w / 2) { if (fm_bas > 0) fm_bas -= gorunen; if (fm_bas < 0) fm_bas = 0; }
                            else if (fm_bas + gorunen < file_count) fm_bas += gorunen;
                        } else if (row >= 1 && row <= sigan) {
                            int r = row - 1;
                            if (!fm_kokte()) {
                                if (r == 0) {                          /* ".." */
                                    int u = str_len(fm_yol);
                                    while (u > 1 && fm_yol[u - 1] != '/') u--;
                                    fm_yol[u > 1 ? u - 1 : 1] = 0;
                                    fm_bas = 0; fm_yenile();
                                    r = -1;
                                } else r--;
                            }
                            int gi = fm_bas + r;
                            if (r >= 0 && gi < file_count) {
                                char yol[VFS_MAX_PATH];
                                str_copy(yol, fm_yol, sizeof yol);
                                int u = str_len(yol);
                                if (!(u == 1 && yol[0] == '/') && u < (int)sizeof yol - 1) { yol[u++] = '/'; yol[u] = 0; }
                                str_copy(yol + u, file_list[gi].name, (int)sizeof yol - u);
                                if (file_list[gi].is_dir) {
                                    str_copy(fm_yol, yol, sizeof fm_yol);
                                    fm_bas = 0; fm_yenile();
                                } else {
                                    viewer_load(yol, file_list[gi].name);
                                    windows[WIN_VIEWER].visible = 1;
                                    bring_to_front(WIN_VIEWER);
                                }
                            }
                        }
                    }
                    break;
                }
            }
            if (gui_idx < 0 && !ys) masaustu_tikla(mx, my);   /* bos masaustu: rihtim/simge */
        }
    }

    if (released) { dragging = -1; yuzey_surukle = -1; }

    if (yuzey_surukle >= 0 && (buttons & 1))
        ulgen_tasi(yuzey_surukle, mx - drag_off_x, my - drag_off_y);

    if (dragging >= 0 && (buttons & 1)) {
        window_t *w = &windows[dragging];
        int nx = mx - drag_off_x;
        int ny = my - drag_off_y;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > sw - w->w) nx = sw - w->w;
        if (ny > sh - TASKBAR_H - w->h) ny = sh - TASKBAR_H - w->h;
        w->x = nx;
        w->y = ny;
    }

    prev_buttons = buttons;
    redraw_all();
}

static void gui_on_mouse_event__ic(void) {
    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);
    gui_fare_isle__ic(mx, my, buttons);
}

/* Sinama/duzen: pencere geometrisi (salt okunur) */
int gui_pencere_bilgi(int idx, int *x, int *y, int *w, int *h, int *gorunur) {
    if (idx < 0 || idx >= MAX_WINDOWS) return -1;
    *x = windows[idx].x; *y = windows[idx].y; *w = windows[idx].w; *h = windows[idx].h;
    *gorunur = windows[idx].visible;
    return 0;
}

static void gui_refresh__ic(void) {
    redraw_all();
}

/* --- SMP adim 6: kilit sarmalayicilari (sarmala.py uretti) ---
 * Ic isleve (ad__ic) yalniz kilit altinda girilir; erken donusler
 * kilidi sizdiramaz. Ozyinelemeli: ic ice katmanlar guvenle cagirir. */
void gui_type_char(char c) {
    ozkilit_al(&gorsel_kilit);
    gui_type_char__ic(c);
    ozkilit_birak(&gorsel_kilit);
}

void gui_on_mouse_event(void) {
    ozkilit_al(&gorsel_kilit);
    gui_on_mouse_event__ic();
    ozkilit_birak(&gorsel_kilit);
}

void gui_refresh(void) {
    ozkilit_al(&gorsel_kilit);
    gui_refresh__ic();
    ozkilit_birak(&gorsel_kilit);
}

void gui_write_string(const char *s) {
    ozkilit_al(&gorsel_kilit);
    gui_write_string__ic(s);
    ozkilit_birak(&gorsel_kilit);
}

/* Pencereyi yerlestir/boyutlandir (duzen ve sinama). z-sirasini degistirmez. */
void gui_pencere_yerlestir(int idx, int x, int y, int w, int h, int gorunur) {
    if (idx < 0 || idx >= MAX_WINDOWS || w < 40 || h < TITLEBAR_H + 8) return;
    ozkilit_al(&gorsel_kilit);
    windows[idx].x = x; windows[idx].y = y; windows[idx].w = w; windows[idx].h = h;
    windows[idx].visible = gorunur ? 1 : 0;
    redraw_all();
    ozkilit_birak(&gorsel_kilit);
}

/* Fare olayini verilen konum/dugmeyle isler (fare surucusu ve sinama). */
void gui_fare_isle(int mx, int my, int buttons) {
    ozkilit_al(&gorsel_kilit);
    gui_fare_isle__ic(mx, my, buttons);
    ozkilit_birak(&gorsel_kilit);
}

/* Terminali bosaltir (kabuk 'temizle' ve sinama). */
void gui_terminal_temizle(void) {
    ozkilit_al(&gorsel_kilit);
    term_clear();
    redraw_all();
    ozkilit_birak(&gorsel_kilit);
}

/* Ana dongunun ekran adimi. Cekirdek ana dongusu ve ekran sinamasi
 * AYNI islevi cagirir (sinama baska bir yol izlemez). */
void masaustu_tur(int periyodik) {
    if (klavye_menu_kapat_istegi_al()) {               /* Esc: acik menu kapanir */
        ozkilit_al(&gorsel_kilit);
        if (ulgen_theme_uyg_menu_acik()) {
            int ux, uy, uw, uh;
            ulgen_theme_uyg_menu_kutu(&ux, &uy, &uw, &uh);
            ulgen_theme_uyg_menu_ata(0);
            ekran_hasar(ux, uy, uw, uh);
            redraw_all();
        }
        ozkilit_birak(&gorsel_kilit);
    }
    {   /* ust cubuktaki calisma suresi saniyede bir */
        static uint64_t son_sn = (uint64_t)-1;
        uint64_t hz = timer_hz() ? timer_hz() : 100;   /* 1.4.8: 50 Hz idi, /100 yarim sayiyordu */
        uint64_t sn = timer_get_ticks() / hz;
        if (sn != son_sn) {
            son_sn = sn;
            ozkilit_al(&gorsel_kilit);
            ekran_hasar(0, 0, fb_width(), ULGEN_UST_BAR);
            ekran_hasar(0, fb_height() - TASKBAR_H, fb_width(), TASKBAR_H);   /* odak rengi (Tab) */
            ekran_isle();
            ozkilit_birak(&gorsel_kilit);
        }
    }
    if (periyodik) {
        gui_refresh();
        ulgen_tam_yenile();   /* GUI ekrani sildi -> yuzeyleri yeniden ciz */
    }
    ulgen_birlestir();
    ozkilit_al(&gorsel_kilit);
    fb_sun();
    ozkilit_birak(&gorsel_kilit);
}
