#ifndef EXP_H
#define EXP_H
#include "vbe.h"
#include "vga.h"
#include "vfs.h"
#include <stdint.h>
#include <stddef.h>
#include "image_decode.h"

/* Sizes: v0.9 uses one stable coordinate system, physical VBE pixels at
 * 100%. The legacy scale symbols remain ABI-compatible but are immutable. */
extern int exp_ui_scale_pct;
#define EXP_SCALE(v)    (v)
#define EXP_UNSCALE(v)  (v)
#define DE_SCR_W        ((int)vbe.width)
#define DE_SCR_H        ((int)vbe.height)
#define DE_TASKBAR_H     28
#define DE_TITLEBAR_H    22
#define DE_BORDER         2
#define DE_MAX_WIN        8
#define DE_TERM_HIST    200
#define DE_TERM_COLS     80
#define DE_NOTIF_MAX      4
#define FM_MAX_ENTRIES  128
#define FM_NAME_LEN      64
#define SYSMON_HISTORY   60
#define EXP_TASK_MAX     10
#define TASK_TEXT_LEN    48

/* Exp themes affect only the framebuffer desktop. VGA terminal schemes stay unchanged. */
typedef struct {
    color32_t base,mantle,crust,surface0,surface1,surface2,overlay0;
    color32_t text,subtext,lavender,blue,sapphire,sky,teal,green,yellow;
    color32_t peach,maroon,red,mauve,pink;
} exp_palette_t;
typedef enum { EXP_THEME_DARK=0, EXP_THEME_WHITE=1 } exp_theme_t;
extern exp_palette_t exp_palette;
#define C_BASE      (exp_palette.base)
#define C_MANTLE    (exp_palette.mantle)
#define C_CRUST     (exp_palette.crust)
#define C_SURFACE0  (exp_palette.surface0)
#define C_SURFACE1  (exp_palette.surface1)
#define C_SURFACE2  (exp_palette.surface2)
#define C_OVERLAY0  (exp_palette.overlay0)
#define C_TEXT      (exp_palette.text)
#define C_SUBTEXT   (exp_palette.subtext)
#define C_LAVENDER  (exp_palette.lavender)
#define C_BLUE      (exp_palette.blue)
#define C_SAPPHIRE  (exp_palette.sapphire)
#define C_SKY       (exp_palette.sky)
#define C_TEAL      (exp_palette.teal)
#define C_GREEN     (exp_palette.green)
#define C_YELLOW    (exp_palette.yellow)
#define C_PEACH     (exp_palette.peach)
#define C_MAROON    (exp_palette.maroon)
#define C_RED       (exp_palette.red)
#define C_MAUVE     (exp_palette.mauve)
#define C_PINK      (exp_palette.pink)

/* App IDs */
typedef enum {
    APP_NONE=0, APP_TERMINAL, APP_FILES, APP_EDITOR, APP_NOTEPAD,
    APP_SYSMON, APP_SETTINGS, APP_VIEWER, APP_ABOUT, APP_CALCULATOR,
    APP_TASKS, APP_JOURNAL, APP_CLOCK, APP_CALENDAR, APP_TINYGL, APP_MINES, APP_SNAKE,
    APP_IMAGE_VIEWER, /* legacy internal compatibility ID */
    APP_ARCHIVEEX,
    APP_EXTERNAL=0x70,
} app_id_t;

/* Native Exp GUI ABI v1. Registered callbacks run in the trusted native
 * kernel/module address space; this is not an ELF/X11/Linux GUI ABI. */
#define EXP_GUI_ABI_MAJOR 1
#define EXP_GUI_ABI_MINOR 0
#define EXP_GUI_MAX_APPS  12
typedef struct {
    int window_id, x, y, width, height, scale_percent;
    color32_t fg, bg;
    void *state;
} exp_gui_context_t;
typedef void (*exp_gui_draw_fn)(exp_gui_context_t *ctx);
typedef void (*exp_gui_key_fn)(exp_gui_context_t *ctx,int key,int ctrl,int alt);
typedef void (*exp_gui_pointer_fn)(exp_gui_context_t *ctx,int x,int y,uint32_t buttons);
typedef void (*exp_gui_lifecycle_fn)(exp_gui_context_t *ctx);
typedef struct {
    uint16_t abi_major,abi_minor;
    const char *id,*title,*category;
    int default_w,default_h;
    uint32_t icon_color;
    void *state;
    exp_gui_draw_fn draw;
    exp_gui_key_fn key;
    exp_gui_pointer_fn pointer;
    exp_gui_lifecycle_fn open,close;
} exp_gui_app_t;
int exp_gui_register(const exp_gui_app_t *app);
int exp_gui_open(const char *id);
int exp_gui_count(void);
void exp_gui_fill(exp_gui_context_t *ctx,int x,int y,int w,int h,color32_t color);
void exp_gui_frame(exp_gui_context_t *ctx,int x,int y,int w,int h,color32_t color);
void exp_gui_text(exp_gui_context_t *ctx,int x,int y,const char *utf8,color32_t fg,color32_t bg);

/* Terminal line */
typedef struct {
    char    text[DE_TERM_COLS+4];
    uint32_t color;
    uint8_t  is_prompt;
} exp_tline_t;

/* File entry */
typedef struct {
    char     name[FM_NAME_LEN];
    int      is_dir;
    uint32_t size;
} fm_entry_t;

/* Sysmon history */
typedef struct {
    uint8_t  cpu[SYSMON_HISTORY];
    uint8_t  mem[SYSMON_HISTORY];
    uint8_t  io[SYSMON_HISTORY];
    int      head;
    uint32_t last_tick;
    uint32_t last_idle_tick;
    uint32_t last_read_ops;
    uint32_t last_write_ops;
    uint32_t last_free;
} sysmon_t;

/* Notification */
typedef struct { int x,y; } exp_game_pt_t;
typedef struct {
    uint8_t mine[10][10], open[10][10], flag[10][10];
    int cursor_x, cursor_y, mines, opened, state;
} exp_mines_t;
typedef struct {
    exp_game_pt_t body[96], food;
    int length, dx, dy, score, state;
    uint32_t last_tick;
} exp_snake_t;

typedef struct {
    char      msg[64];
    uint32_t  expire;
    int       active;
    color32_t color;
} exp_notif_t;

/* Window */
typedef struct {
    int       id;
    app_id_t  app;
    char      title[48];
    int       x, y, w, h;
    int       minimized;
    int       maximized;
    int       px, py, pw, ph;

    /* Terminal */
    exp_tline_t tlines[DE_TERM_HIST];
    int        tcount, tscroll;
    char       tinput[256];
    int        tinput_pos;
    char       thist[32][256];
    int        thist_count, thist_idx;

    /* Files */
    char       fm_path[128];
    fm_entry_t fm_ent[FM_MAX_ENTRIES];
    int        fm_count, fm_sel, fm_scroll;
    char       fm_preview[512];

    /* Editor */
    char       ed_path[128];
    char       ed_buf[8192];
    int        ed_len, ed_scroll;
    int        ed_readonly, ed_cursor, ed_dirty;

    /* Sysmon */
    sysmon_t   sysmon;

    /* Settings */
    int       ext_slot;
    int        cfg_tab;

    /* Image Viewer */
    char       image_path[128];
    atm_image_t image;
    int        image_zoom; /* percent, 0 = fit */
    int        image_pan_x, image_pan_y;

    /* ArchiveEx: bounded raw archive buffer, owned by this window. */
    char       archive_path[128];
    char       archive_message[128];
    char       archive_name[64];
    char       archive_version[32];
    char       archive_description[96];
    uint8_t   *archive_data;
    uint32_t   archive_size, archive_entries;
    int        archive_atpk, archive_valid;

    /* Calculator */
    char       calc_expr[64];
    int        calc_len, calc_result, calc_valid;

    /* Tasks: per-window bounded local checklist with inline creation. */
    uint8_t    todo_done[EXP_TASK_MAX];
    char       todo_text[EXP_TASK_MAX][TASK_TEXT_LEN];
    char       todo_input[TASK_TEXT_LEN];
    int        todo_sel, todo_count, todo_edit, todo_input_len;

    /* Calendar is manual because ATMKoala has no RTC/NTP wall-clock yet. */
    int        cal_year, cal_month;

    /* TinyGL-Lite scene: 0=cube, 1=gears; both remain software-only. */
    int        tinygl_scene;

    /* Native GUI games */
    exp_mines_t mines;
    exp_snake_t snake;
} exp_win_t;

/* State */
typedef struct {
    exp_win_t   wins[DE_MAX_WIN];
    int        win_count;
    int        active;
    int        running;
    int        launcher_open;
    int        launcher_sel;
    int        alttab_open;
    int        alttab_sel;
    exp_notif_t notifs[DE_NOTIF_MAX];
    uint32_t   clock_sec;
    uint32_t   last_clock;
    /* Desktop-owned image cache. Decoded once on apply/startup and never
     * shared with Viewer windows, so window close cannot invalidate it. */
    atm_image_t wallpaper_image;
    char       wallpaper_path[128];
    int        wallpaper_file_active;
} exp_state_t;

/* Public API */
void exp_init(void);
void exp_run(void);
int  exp_open_app(app_id_t app, const char *path);
int  exp_open_tinygl_scene(int scene);
/* Active Exp session wallpaper controls. `apply` decodes once and persists the
 * image path; Viewer key A uses the same internal path. */
int  exp_wallpaper_apply(const char *path);
const char *exp_wallpaper_current(void);
void exp_notify(const char *msg, color32_t color);
void exp_capture_char(char c);  /* called by vbe_console_putchar */
int  exp_is_active(void);        /* true while Exp owns framebuffer output */
void exp_request_full_redraw(void); /* restore desktop after temporary overlay */
int  exp_ui_scale_get(void); /* always 100 */
int  exp_ui_scale_set(int percent, int persist); /* only accepts 100 */

#endif
