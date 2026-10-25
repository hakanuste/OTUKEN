#include "gui.h"
#include "fb.h"
#include "mouse.h"
#include "fat16.h"
#include "log.h"
#include "gorsel_kilit.h"

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
#define TASKBAR_H  28
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
} window_t;

static window_t windows[MAX_WINDOWS];
static int zorder[MAX_WINDOWS];

static int dragging = -1;
static int drag_off_x = 0, drag_off_y = 0;
static int prev_buttons = 0;

/* --- Terminal penceresi durumu --- */
#define TERM_COLS 50
#define TERM_ROWS 13
static char term_buf[TERM_ROWS][TERM_COLS + 1];
static int  term_row = 0, term_col = 0;

/* --- Dosya Yoneticisi durumu --- */
#define MAX_FILES 16
static fat16_dirent_t file_list[MAX_FILES];
static int file_count = 0;
static int fat16_ok = 0;

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
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) term_buf[r][c] = ' ';
        term_buf[r][TERM_COLS] = 0;
    }
    term_row = 0;
    term_col = 0;
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

static void viewer_load(const fat16_dirent_t *entry) {
    static uint8_t filebuf[4096];
    int n = fat16_read_file(entry, filebuf, sizeof(filebuf) - 1);
    if (n < 0) n = 0;
    filebuf[n] = 0;

    viewer_clear();
    int row = 0, col = 0;
    for (int i = 0; i < n && row < VIEWER_ROWS; i++) {
        char c = (char)filebuf[i];
        if (c == '\r') continue;
        if (c == '\n') { row++; col = 0; continue; }
        if (col < VIEWER_COLS) viewer_lines[row][col++] = c;
    }

    str_copy(windows[WIN_VIEWER].title, "GORUNTULEYICI: ", sizeof(windows[WIN_VIEWER].title));
    int base = str_len(windows[WIN_VIEWER].title);
    str_copy(windows[WIN_VIEWER].title + base, entry->name,
             (int)sizeof(windows[WIN_VIEWER].title) - base);
}

static void bring_to_front(int idx) {
    int pos = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) if (zorder[i] == idx) { pos = i; break; }
    if (pos < 0) return;
    for (int i = pos; i < MAX_WINDOWS - 1; i++) zorder[i] = zorder[i + 1];
    zorder[MAX_WINDOWS - 1] = idx;
}

static void draw_window(int idx) {
    window_t *w = &windows[idx];
    if (!w->visible) return;

    int focused = (zorder[MAX_WINDOWS - 1] == idx);
    uint32_t tb_color = focused ? COL_TITLEBAR_FOCUS : COL_TITLEBAR;

    fb_fill_rect(w->x, w->y, w->w, w->h, COL_WINBG);
    fb_fill_rect(w->x, w->y, w->w, TITLEBAR_H, tb_color);
    fb_draw_string(w->x + 6, w->y + 6, w->title, COL_TITLEFG, tb_color);

    fb_fill_rect(w->x + w->w - 18, w->y + 4, 14, 14, COL_CLOSEBOX);
    fb_draw_char(w->x + w->w - 16, w->y + 4, 'X', 0xFFFFFF, COL_CLOSEBOX);

    fb_draw_rect(w->x, w->y, w->w, w->h, COL_BORDER);
    fb_draw_rect(w->x, w->y, w->w, TITLEBAR_H, COL_BORDER);

    int cx = w->x + 8;
    int cy = w->y + TITLEBAR_H + 6;

    if (idx == WIN_TERMINAL) {
        for (int r = 0; r < TERM_ROWS; r++)
            fb_draw_string(cx, cy + r * 10, term_buf[r], COL_TEXTFG, COL_WINBG);
    } else if (idx == WIN_FILEMGR) {
        if (!fat16_ok) {
            fb_draw_string(cx, cy, "DISK BULUNAMADI", COL_TEXTFG, COL_WINBG);
        } else if (file_count == 0) {
            fb_draw_string(cx, cy, "DOSYA YOK", COL_TEXTFG, COL_WINBG);
        } else {
            for (int i = 0; i < file_count; i++) {
                fb_draw_string(cx, cy + i * 12, file_list[i].name, COL_TEXTFG, COL_WINBG);
                char sizebuf[12];
                u32_to_dec_str(file_list[i].size, sizebuf);
                fb_draw_string(cx + 15 * 8, cy + i * 12, sizebuf, COL_TEXTFG, COL_WINBG);
            }
            fb_draw_string(cx, cy + (file_count + 1) * 12,
                            "(bir satira tiklayip ac)", COL_TEXTFG, COL_WINBG);
        }
    } else if (idx == WIN_VIEWER) {
        for (int r = 0; r < VIEWER_ROWS; r++)
            fb_draw_string(cx, cy + r * 10, viewer_lines[r], COL_TEXTFG, COL_WINBG);
    } else if (idx == WIN_LOG) {
        /* Stage 18: canli cekirdek gunlugu - EN YENI kayit en ustte */
        int max_rows = (w->h - TITLEBAR_H - 14) / 10;
        int n = log_entry_count();
        if (n > max_rows) n = max_rows;
        for (int i = 0; i < n; i++) {
            const log_entry_t *e = log_get_entry(i);
            if (!e) break;
            /* seviyeye gore renk: hata kirmizi, uyari koyu sari */
            uint32_t color = COL_TEXTFG;
            if (e->level == LOG_HATA)  color = 0xB33A3A;
            if (e->level == LOG_UYARI) color = 0x8A6D1F;

            char line[72];
            int p = 0;
            line[p++] = '[';
            const char *lv = log_level_name(e->level);
            for (int k = 0; lv[k] && p < 60; k++) line[p++] = lv[k];
            line[p++] = ']';
            line[p++] = ' ';
            for (int k = 0; e->subsystem[k] && p < 60; k++) line[p++] = e->subsystem[k];
            line[p++] = ':';
            line[p++] = ' ';
            for (int k = 0; e->message[k] && p < 68; k++) line[p++] = e->message[k];
            line[p] = 0;
            fb_draw_string(cx, cy + i * 10, line, color, COL_WINBG);
        }
        if (n == 0)
            fb_draw_string(cx, cy, "GUNLUK BOS", COL_TEXTFG, COL_WINBG);
    }
}

static void draw_taskbar(void) {
    int sw = fb_width();
    int sh = fb_height();
    static const char *labels[MAX_WINDOWS] = {
        "TERMINAL",
        "DOSYA Y\xD6NET\xDD" "C\xDDS\xDD",
        "G\xD6R\xDCNT\xDCLEY\xDD" "C\xDD",
        "G\xDCNL\xDCK"
    };

    fb_fill_rect(0, sh - TASKBAR_H, sw, TASKBAR_H, COL_TASKBAR);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        int bx = 8 + i * (TASKBTN_W + 6);
        int active = windows[i].visible;
        uint32_t bg = active ? COL_TASKBTN_ACTIVE : COL_TASKBTN;
        fb_fill_rect(bx, sh - TASKBAR_H + 3, TASKBTN_W, TASKBAR_H - 6, bg);
        fb_draw_string(bx + 6, sh - TASKBAR_H + 3 + 5, labels[i], COL_TASKFG, bg);
    }

    const char *tag = "otukenOS - 64-bit";
    fb_draw_string(sw - 8 - str_len(tag) * 8, sh - TASKBAR_H + 8, tag, COL_TASKFG, COL_TASKBAR);
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

static void redraw_all(void) {
    int sw = fb_width();
    int sh = fb_height();
    fb_fill_rect(0, 0, sw, sh, COL_DESKTOP);
    for (int i = 0; i < MAX_WINDOWS; i++) draw_window(zorder[i]);
    draw_taskbar();
    int mx, my, mb;
    mouse_get_state(&mx, &my, &mb);
    (void)mb;
    draw_cursor(mx, my);
}

static void handle_taskbar_click(int mx) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        int bx = 8 + i * (TASKBTN_W + 6);
        if (mx >= bx && mx < bx + TASKBTN_W) {
            if (windows[i].visible && zorder[MAX_WINDOWS - 1] == i) {
                windows[i].visible = 0;
            } else {
                windows[i].visible = 1;
                bring_to_front(i);
            }
            return;
        }
    }
}

void gui_init(void) {
    int sw = fb_width();
    int sh = fb_height();
    (void)sw; (void)sh;

    windows[WIN_TERMINAL].x = 60;  windows[WIN_TERMINAL].y = 50;
    windows[WIN_TERMINAL].w = 470; windows[WIN_TERMINAL].h = 280;
    str_copy(windows[WIN_TERMINAL].title, "TERMINAL", sizeof(windows[WIN_TERMINAL].title));
    windows[WIN_TERMINAL].visible = 1;

    windows[WIN_FILEMGR].x = 160; windows[WIN_FILEMGR].y = 110;
    windows[WIN_FILEMGR].w = 420; windows[WIN_FILEMGR].h = 240;
    str_copy(windows[WIN_FILEMGR].title, "DOSYA Y\xD6NET\xDD" "C\xDDS\xDD", sizeof(windows[WIN_FILEMGR].title));
    windows[WIN_FILEMGR].visible = 1;

    windows[WIN_VIEWER].x = 260; windows[WIN_VIEWER].y = 170;
    windows[WIN_VIEWER].w = 460; windows[WIN_VIEWER].h = 260;
    str_copy(windows[WIN_VIEWER].title, "G\xD6R\xDCNT\xDCLEY\xDD" "C\xDD (bos)", sizeof(windows[WIN_VIEWER].title));
    windows[WIN_VIEWER].visible = 1;

    windows[WIN_LOG].x = 40; windows[WIN_LOG].y = 380;
    windows[WIN_LOG].w = 620; windows[WIN_LOG].h = 240;
    str_copy(windows[WIN_LOG].title, "CEK\xDDRDEK G\xDCNL\xDCG\xDC (canli)",
             sizeof(windows[WIN_LOG].title));
    windows[WIN_LOG].visible = 1;

    zorder[0] = WIN_FILEMGR;
    zorder[1] = WIN_VIEWER;
    zorder[2] = WIN_TERMINAL;
    zorder[3] = WIN_LOG;

    term_clear();
    viewer_clear();

    fat16_ok = (fat16_mount() == 0);
    if (fat16_ok) {
        file_count = fat16_list_root(file_list, MAX_FILES);
    } else {
        file_count = 0;
    }

    redraw_all();
}

/* Asama 25 performans duzeltmesi: bir dizeyi terminal tamponuna
 * islerken HER KARAKTERDE tum ekrani yeniden cizmek felaket yavastir
 * (1024x768 = ~786 bin piksel islemi / karakter). Once tamponu
 * guncelleyip SONUNDA TEK BIR yeniden cizim yapiyoruz. */
static void term_put_nodraw(char c) {
    if (c == 13) {
        term_row++;
        term_col = 0;
        if (term_row >= TERM_ROWS) { term_scroll(); term_row = TERM_ROWS - 1; }
    } else if (c == 8) {
        if (term_col > 0) { term_col--; term_buf[term_row][term_col] = ' '; }
    } else {
        term_buf[term_row][term_col] = c;
        term_col++;
        if (term_col >= TERM_COLS) {
            term_col = 0;
            term_row++;
            if (term_row >= TERM_ROWS) { term_scroll(); term_row = TERM_ROWS - 1; }
        }
    }
}

static void gui_write_string__ic(const char *s) {
    for (int i = 0; s[i]; i++) {
        term_put_nodraw(s[i] == '\n' ? (char)13 : s[i]);
    }

    /* PERFORMANS (gercek kullanim testinde bulundu):
     * Her cikti satirinda TUM ekrani (masaustu + 4 pencere + gorev
     * cubugu) yeniden cizmek, kabugu tuslari kacIracak kadar
     * yavaslatiyordu. Terminal EN USTTEKI pencereyse yalnizca onu
     * ciziyoruz - gorsel sonuc ayni, maliyet cok daha dusuk.
     * Terminal altta kalmissa dogru katmanlama icin tam cizim sart. */
    if (zorder[MAX_WINDOWS - 1] == WIN_TERMINAL && windows[WIN_TERMINAL].visible) {
        draw_window(WIN_TERMINAL);
    } else {
        redraw_all();
    }
}

static void gui_type_char__ic(char c) {
    if (c == 13) {
        term_row++;
        term_col = 0;
        if (term_row >= TERM_ROWS) { term_scroll(); term_row = TERM_ROWS - 1; }
    } else if (c == 8) {
        if (term_col > 0) { term_col--; term_buf[term_row][term_col] = ' '; }
    } else {
        term_buf[term_row][term_col] = c;
        term_col++;
        if (term_col >= TERM_COLS) {
            term_col = 0;
            term_row++;
            if (term_row >= TERM_ROWS) { term_scroll(); term_row = TERM_ROWS - 1; }
        }
    }
    redraw_all();
}

static void gui_on_mouse_event__ic(void) {
    int mx, my, buttons;
    mouse_get_state(&mx, &my, &buttons);
    int sw = fb_width();
    int sh = fb_height();

    int pressed  = (buttons & 1) && !(prev_buttons & 1);
    int released = !(buttons & 1) && (prev_buttons & 1);

    if (pressed) {
        if (my >= sh - TASKBAR_H) {
            handle_taskbar_click(mx);
        } else {
            for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                int idx = zorder[i];
                window_t *w = &windows[idx];
                if (!w->visible) continue;
                if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
                    bring_to_front(idx);
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
                        int row = (my - cy) / 12;
                        if (row >= 0 && row < file_count) {
                            viewer_load(&file_list[row]);
                            windows[WIN_VIEWER].visible = 1;
                            bring_to_front(WIN_VIEWER);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (released) dragging = -1;

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
