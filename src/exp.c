/* exp.c — Exp Black desktop for atmkoala */
#include "exp.h"
#include "vbe.h"
#include "vga.h"
#include "keyboard.h"
#include "vfs.h"
#include "util.h"
#include "pit.h"
#include "rtc.h"
#include "atm_time.h"
#include "sched.h"
#include "kmalloc.h"
#include "net.h"
#include "disk.h"
#include "config.h"
#include "locale.h"
#include "fileformat.h"
#include "tarzst.h"
#include "ossdk.h"
#include "users.h"
#include "mouse.h"
#include "tinygl_lite.h"
#include "ttf.h"
#include "font.h"
#include "hw_y116.h"
#include "gui_demo.h"
#include "atminit.h"
#include "catfs_vfs.h"
#include <stdint.h>
#include <stddef.h>

extern void dispatch(char *line);

static exp_state_t DE;
static int next_id = 1;
static int icon_sel = 0;
static char cap_buf[4096];
static int  cap_len = 0;
static int  capturing = 0;
static int  mouse_drag_win = -1;
static int  mouse_drag_dx = 0, mouse_drag_dy = 0;
static int  mouse_resize_win = -1;
static int  mouse_last_win = -1, mouse_last_item = -1;
static uint32_t mouse_last_tick = 0;
/* Set by diagnostic overlays that temporarily own the framebuffer. */
static int  exp_force_redraw = 0;
/* Persistent desktop background selected in Settings > Appearance. */
static int  wallpaper_id = 0;
int exp_ui_scale_pct = 100; /* v0.9: fixed physical-pixel layout */
static exp_gui_app_t gui_apps[EXP_GUI_MAX_APPS];
static int gui_app_count=0;
exp_palette_t exp_palette;
static exp_theme_t exp_theme=EXP_THEME_DARK;
static const exp_palette_t PAL_DARK={
 RGB(0x0A,0x0A,0x0A),RGB(0x11,0x11,0x11),RGB(0x05,0x05,0x05),RGB(0x1B,0x1B,0x1B),RGB(0x29,0x29,0x29),RGB(0x3B,0x3B,0x3B),RGB(0x78,0x78,0x78),
 RGB(0xF0,0xF0,0xF0),RGB(0xA6,0xA6,0xA6),RGB(0xD2,0xD2,0xD2),RGB(0xC8,0xC8,0xC8),RGB(0xB8,0xB8,0xB8),RGB(0xD8,0xD8,0xD8),RGB(0xB0,0xB0,0xB0),RGB(0xC0,0xC0,0xC0),RGB(0xD1,0xC8,0xB8),RGB(0xB6,0xA6,0x92),RGB(0x91,0x6F,0x6F),RGB(0xA8,0x4F,0x4F),RGB(0xB8,0xAE,0xAE),RGB(0xB5,0x9B,0x9B)};
static const exp_palette_t PAL_WHITE={
 RGB(0xFA,0xFA,0xF7),RGB(0xF2,0xF2,0xED),RGB(0xE7,0xE7,0xE0),RGB(0xFF,0xFF,0xFC),RGB(0xE0,0xE0,0xD8),RGB(0xC4,0xC4,0xBA),RGB(0x6D,0x6D,0x66),
 RGB(0x1E,0x1E,0x1B),RGB(0x4F,0x4F,0x49),RGB(0x30,0x30,0x2B),RGB(0x3C,0x55,0x68),RGB(0x3A,0x5F,0x72),RGB(0x2F,0x57,0x6B),RGB(0x2F,0x67,0x5A),RGB(0x3B,0x6D,0x3F),RGB(0x7B,0x66,0x28),RGB(0x8C,0x5B,0x35),RGB(0x7B,0x50,0x50),RGB(0x9E,0x35,0x35),RGB(0x67,0x56,0x67),RGB(0x8B,0x52,0x65)};
static void exp_theme_set(exp_theme_t id,int save){
    exp_theme=id==EXP_THEME_WHITE?EXP_THEME_WHITE:EXP_THEME_DARK;
    exp_palette=exp_theme==EXP_THEME_WHITE?PAL_WHITE:PAL_DARK;
    if(save){sysconf_set("desktop","ui_theme",exp_theme==EXP_THEME_WHITE?"white":"dark");sysconf_save();}
}
static const char *exp_theme_name(void){return exp_theme==EXP_THEME_WHITE?"White Paper":"Dark Mono";}
static tgl_context_t tinygl_ctx;
static int inside(int px,int py,int x,int y,int w,int h);
static int flappy_state_selftest(void);
static int sysinfo_state_selftest(void);
static int events_state_selftest(void);
static int paint_state_selftest(void);
static int tetris_state_selftest(void);
static int power_state_selftest(void);
static int saver_state_selftest(void);
static void saver_begin(void);
static int screenshot_state_selftest(void);
static int wallpaper_state_selftest(void);
static int clock_zone_state_selftest(void);
static void gui_context(exp_gui_context_t *ctx,exp_win_t *w){ctx->window_id=w->id;ctx->x=w->x+2;ctx->y=w->y+DE_TITLEBAR_H+1;ctx->width=w->w-4;ctx->height=w->h-DE_TITLEBAR_H-3;ctx->scale_percent=exp_ui_scale_pct;ctx->fg=C_TEXT;ctx->bg=C_BASE;ctx->state=(w->ext_slot>=0&&w->ext_slot<gui_app_count)?gui_apps[w->ext_slot].state:NULL;}
void exp_gui_fill(exp_gui_context_t *ctx,int x,int y,int w,int h,color32_t c){if(ctx)vbe_fill_rect(EXP_SCALE(ctx->x+x),EXP_SCALE(ctx->y+y),EXP_SCALE(w),EXP_SCALE(h),c);}
void exp_gui_frame(exp_gui_context_t *ctx,int x,int y,int w,int h,color32_t c){if(ctx)vbe_draw_rect(EXP_SCALE(ctx->x+x),EXP_SCALE(ctx->y+y),EXP_SCALE(w),EXP_SCALE(h),c);}
void exp_gui_text(exp_gui_context_t *ctx,int x,int y,const char*s,color32_t fg,color32_t bg){if(ctx)ttf_render_string_percent(EXP_SCALE(ctx->x+x),EXP_SCALE(ctx->y+y),s,fg,bg,exp_ui_scale_pct);}
int exp_gui_count(void){return gui_app_count;}
int exp_gui_register(const exp_gui_app_t *app){
    if(!app||app->abi_major!=EXP_GUI_ABI_MAJOR||!app->id||!app->title||!app->draw||gui_app_count>=EXP_GUI_MAX_APPS)return -1;
    for(int i=0;i<gui_app_count;i++)if(!kstrcmp(gui_apps[i].id,app->id))return -1;
    gui_apps[gui_app_count]=*app;awm_register_app(app->id,app->title,app->category?app->category:"External",0);return gui_app_count++;
}

/* ─── Draw helpers ──────────────────────────────────────── */
#define R(x,y,w,h,c)  vbe_fill_rect(EXP_SCALE(x),EXP_SCALE(y),EXP_SCALE(w),EXP_SCALE(h),c)
#define HL(x,y,w,c)   vbe_draw_hline(EXP_SCALE(x),EXP_SCALE(y),EXP_SCALE(w),c)
#define VL(x,y,h,c)   vbe_draw_vline(EXP_SCALE(x),EXP_SCALE(y),EXP_SCALE(h),c)
#define BOX(x,y,w,h,c) vbe_draw_rect(EXP_SCALE(x),EXP_SCALE(y),EXP_SCALE(w),EXP_SCALE(h),c)
#define T(x,y,s,f,b)  ttf_render_string_percent(EXP_SCALE(x),EXP_SCALE(y),s,f,b,exp_ui_scale_pct)
#define TC(x,y,s,f,b,w) ttf_render_string_clipped(EXP_SCALE(x),EXP_SCALE(y),s,f,b,EXP_SCALE(w))
#define TW(x,y,s,f,b,w,rows) ttf_render_string_wrapped(EXP_SCALE(x),EXP_SCALE(y),s,f,b,EXP_SCALE(w),rows)
static int exp_text_width(const char *s){return s?utf8_strlen(s)*ttf_glyph_width():0;}
int exp_text_layout_selftest(void){
    static const char russian[]="Русский";
    int glyphs=utf8_strlen(russian), bytes=(int)kstrlen(russian);
    if(glyphs!=7||bytes!=14)return -1;
    if(exp_text_width(russian)!=glyphs*ttf_glyph_width())return -1;
    if(flappy_state_selftest()<0||sysinfo_state_selftest()<0||events_state_selftest()<0||paint_state_selftest()<0||tetris_state_selftest()<0||power_state_selftest()<0||saver_state_selftest()<0||screenshot_state_selftest()<0||wallpaper_state_selftest()<0||clock_zone_state_selftest()<0)return -1;
    return 0;
}
#define CX(w) ((w)->x+2)
#define CY(w) ((w)->y+DE_TITLEBAR_H+1)
#define CW(w) ((w)->w-4)
#define CH(w) ((w)->h-DE_TITLEBAR_H-3)

static void TF(int x,int y,color32_t fg,color32_t bg,const char*fmt,...){
    char buf[128]; int i=0;
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    while(*fmt&&i<124){
        if(*fmt!='%'){buf[i++]=*fmt++;continue;}
        fmt++;
        if(*fmt=='s'){const char*s=__builtin_va_arg(ap,const char*);if(s)while(*s&&i<124)buf[i++]=*s++;}
        else if(*fmt=='d'){int n=__builtin_va_arg(ap,int);char t[12];kitoa(n,t,10);for(int j=0;t[j]&&i<124;j++)buf[i++]=t[j];}
        else if(*fmt=='u'){uint32_t n=__builtin_va_arg(ap,uint32_t);char t[12];kuitoa(n,t,10);for(int j=0;t[j]&&i<124;j++)buf[i++]=t[j];}
        else if(*fmt=='%'){buf[i++]='%';}
        fmt++;
    }
    buf[i]=0; __builtin_va_end(ap);
    ttf_render_string(x,y,buf,fg,bg);
}

/* Rounded rect (2px corners) */
static void RR(int x,int y,int w,int h,color32_t c){
    R(x+2,y,w-4,h,c); R(x,y+2,2,h-4,c); R(x+w-2,y+2,2,h-4,c);
    vbe_putpixel(x+1,y+1,c); vbe_putpixel(x+w-2,y+1,c);
    vbe_putpixel(x+1,y+h-2,c); vbe_putpixel(x+w-2,y+h-2,c);
}

/* ─── Notifications ─────────────────────────────────────── */
void exp_notify(const char *msg, color32_t color){
    for(int i=0;i<DE_NOTIF_MAX;i++){
        if(!DE.notifs[i].active){
            kstrncpy(DE.notifs[i].msg,msg,63);
            DE.notifs[i].color=color;
            DE.notifs[i].expire=pit_get_ticks()+300;
            DE.notifs[i].active=1;
            return;
        }
    }
}
static void draw_notifs(void){
    int y=DE_SCR_H-DE_TASKBAR_H-8;
    for(int i=DE_NOTIF_MAX-1;i>=0;i--){
        if(!DE.notifs[i].active) continue;
        if(pit_get_ticks()>DE.notifs[i].expire){DE.notifs[i].active=0;continue;}
        int nw=200,nh=24,nx=DE_SCR_W-nw-8;
        RR(nx,y,nw,nh,C_SURFACE0); BOX(nx,y,nw,nh,DE.notifs[i].color);
        T(nx+8,y+4,DE.notifs[i].msg,DE.notifs[i].color,C_SURFACE0);
        y-=nh+4;
    }
}

/* ─── Clock ─────────────────────────────────────────────── */
static void clock_update(void){
    uint32_t now=pit_get_ticks();
    if(now-DE.last_clock>=100){
        DE.clock_sec+=(now-DE.last_clock)/100;
        DE.last_clock=now;
    }
}
static void clock_str(char *out){
    uint32_t s=DE.clock_sec; char t[8]; out[0]=0;
    if(s/3600<10)kstrcat(out,"0"); kuitoa(s/3600,t,10); kstrcat(out,t); kstrcat(out,":");
    if((s/60)%60<10)kstrcat(out,"0"); kuitoa((s/60)%60,t,10); kstrcat(out,t); kstrcat(out,":");
    if(s%60<10)kstrcat(out,"0"); kuitoa(s%60,t,10); kstrcat(out,t);
}

/* ─── Native app icons ───────────────────────────────────── */
/* Each icon is drawn from simple geometric primitives, keeping Exp entirely
 * freestanding and crisp at all VBE resolutions.  The shared muted palette
 * preserves Exp Mono's black, minimal visual language. */
static void icon_line(int x,int y,int w,int h,color32_t c){ R(x,y,w,h,c); }
static void draw_app_icon(app_id_t app,int x,int y,int size,color32_t fg,color32_t bg){
    int u=size/16;if(u<1)u=1; int s=16*u, i;
    RR(x,y,s,s,bg);
    BOX(x,y,s,s,C_SURFACE2);
    R(x+u,y+u,s-2*u,u,C_SURFACE0);
    if(app==APP_TERMINAL){
        RR(x+u,y+2*u,s-2*u,s-4*u,C_CRUST);BOX(x+u,y+2*u,s-2*u,s-4*u,fg);
        icon_line(x+4*u,y+6*u,3*u,u,fg); icon_line(x+6*u,y+8*u,u,3*u,fg); icon_line(x+4*u,y+11*u,4*u,u,fg);
        icon_line(x+10*u,y+11*u,3*u,u,fg);
    } else if(app==APP_FILES){
        R(x+2*u,y+5*u,12*u,8*u,fg);R(x+3*u,y+3*u,5*u,3*u,fg);R(x+3*u,y+6*u,10*u,2*u,C_SURFACE1);BOX(x+2*u,y+5*u,12*u,8*u,C_TEXT);
    } else if(app==APP_NOTEPAD||app==APP_JOURNAL||app==APP_EVENTLOG||app==APP_EDITOR||app==APP_VIEWER){
        R(x+4*u,y+2*u,8*u,12*u,C_TEXT);BOX(x+4*u,y+2*u,8*u,12*u,fg);
        for(i=0;i<3;i++)icon_line(x+6*u,y+(5+i*2)*u,5*u,u,fg);
        if(app==APP_NOTEPAD)icon_line(x+10*u,y+11*u,3*u,2*u,C_PEACH);
    } else if(app==APP_SYSMON||app==APP_SYSINFO){
        BOX(x+2*u,y+2*u,12*u,12*u,fg);icon_line(x+4*u,y+10*u,2*u,2*u,fg);icon_line(x+6*u,y+7*u,2*u,5*u,fg);icon_line(x+8*u,y+5*u,2*u,7*u,fg);icon_line(x+10*u,y+8*u,2*u,4*u,fg);
    } else if(app==APP_POWER){
        BOX(x+4*u,y+2*u,8*u,11*u,fg);R(x+7*u,y+2*u,2*u,6*u,bg);R(x+7*u,y+7*u,2*u,5*u,fg);
    } else if(app==APP_SCREENSHOTS){
        BOX(x+2*u,y+5*u,12*u,8*u,fg);R(x+5*u,y+3*u,6*u,3*u,fg);R(x+6*u,y+7*u,4*u,4*u,bg);BOX(x+6*u,y+7*u,4*u,4*u,C_TEXT);
    } else if(app==APP_SETTINGS){
        R(x+5*u,y+2*u,6*u,12*u,fg);R(x+2*u,y+5*u,12*u,6*u,fg);R(x+4*u,y+4*u,8*u,8*u,fg);R(x+6*u,y+6*u,4*u,4*u,bg);BOX(x+5*u,y+5*u,6*u,6*u,C_TEXT);
    } else if(app==APP_CALCULATOR){
        RR(x+3*u,y+2*u,10*u,12*u,fg);R(x+4*u,y+3*u,8*u,3*u,C_CRUST);for(i=0;i<2;i++)for(int j=0;j<3;j++)R(x+(5+j*3)*u,y+(7+i*3)*u,2*u,2*u,C_CRUST);
    } else if(app==APP_TASKS){
        R(x+3*u,y+2*u,10*u,12*u,C_TEXT);BOX(x+3*u,y+2*u,10*u,12*u,fg);for(i=0;i<3;i++){BOX(x+5*u,y+(5+i*3)*u,2*u,2*u,fg);icon_line(x+8*u,y+(5+i*3)*u,3*u,u,fg);}
    } else if(app==APP_CLOCK){
        BOX(x+3*u,y+3*u,10*u,10*u,fg);R(x+7*u,y+7*u,2*u,2*u,fg);icon_line(x+8*u,y+5*u,u,3*u,fg);icon_line(x+8*u,y+8*u,3*u,u,fg);
    } else if(app==APP_CALENDAR){
        R(x+3*u,y+3*u,10*u,10*u,fg);R(x+4*u,y+6*u,8*u,6*u,C_BASE);HL(x+3*u,y+5*u,10*u,C_BASE);R(x+5*u,y+7*u,2*u,2*u,fg);R(x+9*u,y+7*u,2*u,2*u,fg);
    } else if(app==APP_TINYGL){
        BOX(x+3*u,y+3*u,8*u,8*u,fg);BOX(x+6*u,y+6*u,8*u,8*u,C_TEXT);VL(x+6*u,y+3*u,3*u,fg);HL(x+3*u,y+11*u,3*u,fg);
    } else if(app==APP_MINES){
        BOX(x+2*u,y+2*u,12*u,12*u,fg);for(i=0;i<3;i++)for(int j=0;j<3;j++)BOX(x+(3+j*3)*u,y+(3+i*3)*u,2*u,2*u,fg);R(x+7*u,y+7*u,2*u,2*u,C_RED);
    } else if(app==APP_SNAKE){
        R(x+3*u,y+9*u,3*u,3*u,fg);R(x+5*u,y+7*u,3*u,3*u,fg);R(x+7*u,y+5*u,3*u,3*u,fg);R(x+9*u,y+5*u,3*u,3*u,fg);R(x+11*u,y+6*u,u,u,C_CRUST);
    } else if(app==APP_FLAPPY){
        R(x+2*u,y+2*u,3*u,12*u,C_GREEN);R(x+11*u,y+2*u,3*u,4*u,C_GREEN);R(x+11*u,y+10*u,3*u,4*u,C_GREEN);R(x+6*u,y+7*u,4*u,3*u,fg);R(x+9*u,y+8*u,u,u,C_CRUST);
    } else if(app==APP_TETRIS){
        R(x+3*u,y+3*u,3*u,3*u,fg);R(x+6*u,y+3*u,3*u,3*u,fg);R(x+9*u,y+3*u,3*u,3*u,fg);R(x+9*u,y+6*u,3*u,3*u,fg);R(x+5*u,y+10*u,6*u,2*u,fg);
    } else if(app==APP_ABOUT){
        R(x+6*u,y+2*u,4*u,4*u,fg);R(x+7*u,y+7*u,2*u,5*u,fg);R(x+7*u,y+13*u,2*u,u,fg);
    } else if(app==APP_IMAGE_VIEWER){
        BOX(x+2*u,y+3*u,12*u,10*u,fg);R(x+4*u,y+5*u,8*u,6*u,bg);R(x+4*u,y+10*u,3*u,u,fg);R(x+7*u,y+8*u,2*u,2*u,fg);R(x+9*u,y+7*u,3*u,4*u,fg);R(x+10*u,y+5*u,u,u,C_TEXT);
    } else if(app==APP_EXTERNAL){
        BOX(x+3*u,y+3*u,10*u,10*u,fg);BOX(x+5*u,y+5*u,6*u,6*u,fg);
        R(x+6*u,y+6*u,4*u,4*u,fg);R(x+4*u,y+12*u,8*u,u,fg);
    } else {
        BOX(x+3*u,y+3*u,10*u,10*u,fg);
    }
}

/* ─── System tray ────────────────────────────────────────── */
static void tray_refresh_hardware(void){
    /* The UI never probes PCI/USB/MMIO. Battery providers, when available,
     * are refreshed at most once a second and otherwise remain explicitly
     * unavailable rather than showing a fabricated percentage. */
    static uint32_t last_tick=0;uint32_t now=sched_uptime_ticks();
    if(now-last_tick>=100u){battery_update(&g_battery);last_tick=now;}
}
static void draw_tray_battery(int x,int y){
    color32_t c=!g_battery.valid?C_OVERLAY0:(!g_battery.present?C_SUBTEXT:(g_battery.capacity_pct<20?C_RED:(g_battery.charging?C_GREEN:C_TEXT)));
    BOX(x,y+3,14,10,c);R(x+14,y+6,2,4,c);
    if(g_battery.valid&&g_battery.present){int fill=(int)(g_battery.capacity_pct*10u/100u);if(fill<1)fill=1;R(x+2,y+5,fill,6,c);if(g_battery.charging){R(x+8,y+4,2,3,C_CRUST);R(x+6,y+7,2,3,C_CRUST);R(x+8,y+10,2,2,C_CRUST);}}
    else {HL(x+3,y+8,8,c);VL(x+7,y+5,6,c);}
}
static void draw_tray_wifi(int x,int y){
    color32_t c=g_radio.wifi_connected?C_GREEN:(g_radio.wifi_driver_ready?C_YELLOW:(g_radio.wifi_controller_present?C_YELLOW:C_OVERLAY0));
    /* A green glyph is reserved for association reported by a Wi-Fi driver. */
    R(x+1,y+3,12,1,c);R(x+2,y+4,10,1,c);R(x+3,y+6,8,1,c);R(x+4,y+7,6,1,c);R(x+5,y+9,4,1,c);R(x+6,y+11,2,2,c);
    if(!g_radio.wifi_connected){R(x+11,y+10,3,2,c);R(x+12,y+8,2,2,c);}
}
static void draw_tray_bluetooth(int x,int y){
    color32_t c=g_radio.bluetooth_connected?C_GREEN:(g_radio.bluetooth_driver_ready?C_YELLOW:(g_radio.bluetooth_controller_present?C_YELLOW:C_OVERLAY0));
    /* Green means an HCI driver reports an active connection, not merely USB presence. */
    VL(x+7,y+2,12,c);R(x+8,y+3,2,2,c);R(x+10,y+5,2,2,c);R(x+8,y+7,2,2,c);R(x+8,y+9,2,2,c);R(x+10,y+11,2,2,c);R(x+8,y+13,2,2,c);
    if(!g_radio.bluetooth_connected)R(x+1,y+12,3,3,c);
}

/* ─── Taskbar ────────────────────────────────────────────── */
static void draw_taskbar(void){
    int ty=DE_SCR_H-DE_TASKBAR_H;
    /* Small alpha overlay keeps desktop wallpaper visible under the dock. */
    vbe_blend_rect(0,ty,DE_SCR_W,DE_TASKBAR_H,C_CRUST,224);
    HL(0,ty,DE_SCR_W,C_SURFACE2);
    /* Bottom dock: quiet, flat and compact. */
    tray_refresh_hardware();
    color32_t lb=DE.launcher_open?C_SURFACE2:C_SURFACE1;
    RR(6,ty+4,70,DE_TASKBAR_H-8,lb);
    /* Four-cell menu glyph is crisp at VBE scale and distinct from app icons. */
    color32_t mc=DE.launcher_open?C_TEXT:C_SUBTEXT;R(12,ty+8,5,5,mc);R(19,ty+8,5,5,mc);R(12,ty+15,5,5,mc);R(19,ty+15,5,5,mc);
    T(31,ty+6,"Menu",mc,lb);
    int tray_left=DE_SCR_W-182;
    int wx=84;
    for(int i=0;i<DE.win_count;i++){
        if(wx+104>tray_left) break;
        exp_win_t *w=&DE.wins[i];
        int isel=(i==DE.active);
        color32_t bg=w->minimized?C_CRUST:(isel?C_SURFACE2:C_MANTLE);
        color32_t fg=isel?C_TEXT:C_SUBTEXT;
        RR(wx,ty+4,104,DE_TASKBAR_H-8,bg);
        if(isel) HL(wx+6,ty+DE_TASKBAR_H-5,92,C_TEXT);
        draw_app_icon(w->app,wx+7,ty+6,16,fg,bg);
        char ttl[10]; kstrncpy(ttl,w->title,8); ttl[8]=0;
        T(wx+28,ty+6,ttl,fg,bg);
        wx+=110;
    }
    extern int keyboard_ru(void);
    char clk[16]; clock_str(clk);
    int cw2=(int)kstrlen(clk)*8+4;
    int rx=DE_SCR_W-cw2-8;
    T(rx,ty+6,clk,C_TEXT,C_CRUST);
    rx-=28; draw_tray_battery(rx,ty+5);
    if(g_battery.valid&&g_battery.present) TF(rx-25,ty+6,g_battery.capacity_pct<20?C_RED:C_SUBTEXT,C_CRUST,"%u%%",g_battery.capacity_pct);
    else T(rx-17,ty+6,"--",C_OVERLAY0,C_CRUST);
    rx-=24; draw_tray_wifi(rx,ty+5);
    rx-=22; draw_tray_bluetooth(rx,ty+5);
    rx-=28; T(rx,ty+6,keyboard_ru()?"RU":"EN",C_SUBTEXT,C_CRUST);
}

/* ─── Desktop ────────────────────────────────────────────── */
typedef struct { int x,y; app_id_t app; const char *label; color32_t ic; } DI;
static const DI ICONS[18]={
    {20,  24, APP_TERMINAL,  "Terminal", RGB(0x48,0x6E,0x85)},
    {90,  24, APP_FILES,     "Files",    RGB(0x8F,0x7A,0x3A)},
    {160, 24, APP_NOTEPAD,   "Notepad",  RGB(0x44,0x76,0x4B)},
    {230, 24, APP_SYSMON,    "Monitor",  RGB(0x75,0x5B,0x75)},
    {300, 24, APP_SETTINGS,  "Settings", RGB(0x98,0x5F,0x39)},
    {370, 24, APP_CALCULATOR,"Calc",     RGB(0x52,0x52,0x4B)},
    {440, 24, APP_TASKS,     "Tasks",    RGB(0x43,0x72,0x69)},
    {510, 24, APP_ABOUT,     "About",    RGB(0x77,0x77,0x70)},
    {20, 104, APP_JOURNAL,   "Journal",  RGB(0x70,0x70,0x68)},
    {90, 104, APP_CALENDAR,  "Calendar", RGB(0x38,0x38,0x34)},
    {160,104, APP_TINYGL,    "TinyGL",   RGB(0x46,0x70,0x82)},
    {230,104, APP_VIEWER,"Viewer", RGB(0x60,0x75,0x8B)},
    {300,104, APP_SYSINFO,"System", RGB(0x5A,0x70,0x7A)},
    {370,104, APP_EVENTLOG,"Events", RGB(0x70,0x70,0x68)},
    {440,104, APP_TETRIS,"Tetris", RGB(0x88,0x62,0x91)},
    {510,104, APP_POWER,"Power", RGB(0x9A,0x4D,0x4D)},
    {20, 184, APP_SCREENSHOTS,"Shots", RGB(0x5A,0x70,0x7A)},
    {90, 184, APP_PAINT,"Paint", RGB(0x9A,0x58,0x50)},
};
#define NICONS 18

#define EXP_BUILTIN_WALLPAPERS 14
static const char *wallpaper_name(int id){
    static const char *names[]={"Paper","Porcelain","Grid","Lines","Azure","Horizon","Orbit","Cobalt","Aurora","Ink","Snow","Checkers","Scanline","Mono Rings"};
    return names[(id>=0&&id<EXP_BUILTIN_WALLPAPERS)?id:0];
}
static int wallpaper_state_selftest(void){return EXP_BUILTIN_WALLPAPERS==14&&!kstrcmp(wallpaper_name(4),"Azure")&&!kstrcmp(wallpaper_name(8),"Aurora")&&!kstrcmp(wallpaper_name(9),"Ink")&&!kstrcmp(wallpaper_name(13),"Mono Rings")&&!kstrcmp(wallpaper_name(99),"Paper")?0:-1;}
static void wallpaper_file_clear(int save){
    atm_image_release(&DE.wallpaper_image);DE.wallpaper_path[0]=0;DE.wallpaper_file_active=0;
    if(save){sysconf_set("desktop","wallpaper_file","");sysconf_save();}
}
static int wallpaper_file_set(const char *path,int save){
    atm_image_t decoded;
    if(!path||!path[0]||kstrlen(path)>=sizeof(DE.wallpaper_path))return -1;
    if(atm_image_decode_file(path,&decoded)<0)return -1;
    wallpaper_file_clear(0);DE.wallpaper_image=decoded;kstrcpy(DE.wallpaper_path,path);DE.wallpaper_file_active=1;
    if(save){sysconf_set("desktop","wallpaper_file",path);sysconf_save();}
    return 0;
}
static const char *wallpaper_label(void){return DE.wallpaper_file_active?DE.wallpaper_path:wallpaper_name(wallpaper_id);}
int exp_wallpaper_apply(const char *path){
    if(!DE.running||wallpaper_file_set(path,1)<0)return -1;
    exp_notify("Image wallpaper applied",C_GREEN);exp_force_redraw=1;return 0;
}
const char *exp_wallpaper_current(void){return wallpaper_label();}
static void draw_wallpaper(void){
    int usable=DE_SCR_H-DE_TASKBAR_H;if(wallpaper_id<0||wallpaper_id>=EXP_BUILTIN_WALLPAPERS)wallpaper_id=0;
    if(DE.wallpaper_file_active&&DE.wallpaper_image.rgba&&DE.wallpaper_image.width>0&&DE.wallpaper_image.height>0){
        /* Cover the usable desktop while preserving aspect ratio. The source
         * was decoded at apply/startup, so this is only a bounded scaled blit. */
        int dw=DE_SCR_W,dh=(DE.wallpaper_image.height*dw)/DE.wallpaper_image.width;
        if(dh<usable){dh=usable;dw=(DE.wallpaper_image.width*dh)/DE.wallpaper_image.height;}
        vbe_fill_rect(0,0,DE_SCR_W,usable,C_BASE);
        vbe_blit_rgba_scaled(DE.wallpaper_image.rgba,DE.wallpaper_image.width,DE.wallpaper_image.height,(DE_SCR_W-dw)/2,(usable-dh)/2,dw,dh);
        return;
    }
    for(int py=0;py<usable;py++){
        uint32_t q=(uint32_t)py*255u/(uint32_t)(usable?usable:1);color32_t c;
        switch(wallpaper_id){
        case 0:{uint8_t s=exp_theme==EXP_THEME_DARK?(uint8_t)(9u+q/52u):(uint8_t)(250u-q/64u);c=RGB(s,s,s);break;}
        case 1:{uint8_t s=exp_theme==EXP_THEME_DARK?(uint8_t)(20u+q/42u):(uint8_t)(255u-q/85u);c=RGB(s,s,s);break;}
        case 2:c=exp_theme==EXP_THEME_DARK?RGB(12,12,12):RGB(242,242,242);break;
        case 3:{uint8_t s=exp_theme==EXP_THEME_DARK?(uint8_t)(12u+((py/24)%2?4:0)):(uint8_t)(248u-((py/24)%2?5:0));c=RGB(s,s,s);break;}
        case 4:c=RGB(4u+q/48u,22u+q/8u,58u+q/3u);break;
        case 5:c=RGB(5u+q/64u,28u+q/10u,68u+q/3u);break;
        case 6:c=RGB(7u+q/80u,18u+q/20u,44u+q/9u);break;
        case 7:c=RGB(4u+q/64u,24u+q/14u,66u+q/5u);break;
        case 8:c=RGB(5u+q/70u,30u+q/8u,52u+q/6u);break;
        case 9:{uint8_t s=(uint8_t)(5u+q/48u);c=RGB(s,s,s);break;}
        case 10:{uint8_t s=(uint8_t)(242u-q/80u);c=RGB(s,s,s);break;}
        case 11:c=RGB(24,24,24);break;
        case 12:{uint8_t s=(uint8_t)(18u+((py/12)%2?12:0));c=RGB(s,s,s);break;}
        default:{uint8_t s=(uint8_t)(12u+q/36u);c=RGB(s,s,s);break;}
        }
        HL(0,py,DE_SCR_W,c);
    }
    if(wallpaper_id==2){for(int x=0;x<DE_SCR_W;x+=32)VL(x,0,usable,C_SURFACE0);for(int y=0;y<usable;y+=32)HL(0,y,DE_SCR_W,C_SURFACE0);}
    else if(wallpaper_id==3){for(int y=12;y<usable;y+=24)HL(0,y,DE_SCR_W,C_SURFACE0);}
    else if(wallpaper_id==5){for(int y=usable/3;y<usable;y+=76)HL(0,y,DE_SCR_W,RGB(20,70,126));}
    else if(wallpaper_id==6){int cx=DE_SCR_W*3/4,cy=usable/3;for(int r=28;r<150;r+=28)BOX(cx-r,cy-r,r*2,r*2,RGB(24,76,138));}
    else if(wallpaper_id==7){for(int base=-usable;base<DE_SCR_W;base+=36)for(int y=0;y<usable;y+=2){int x=base+y/2;if(x>=0&&x<DE_SCR_W)vbe_putpixel(x,y,RGB(35,104,184));}}
    else if(wallpaper_id==8){for(int x=0;x<DE_SCR_W;x+=64)vbe_blend_rect(x,0,18,usable,(x/64)%2?RGB(42,132,154):RGB(34,86,176),42);}
    else if(wallpaper_id==11){for(int y=0;y<usable;y+=32)for(int x=0;x<DE_SCR_W;x+=32)if(((x/32)+(y/32))&1)R(x,y,32,32,RGB(226,226,226));}
    else if(wallpaper_id==12){for(int y=6;y<usable;y+=24)HL(0,y,DE_SCR_W,RGB(100,100,100));}
    else if(wallpaper_id==13){int cx=DE_SCR_W/2,cy=usable/2;for(int r=30;r<220;r+=30)BOX(cx-r,cy-r,r*2,r*2,RGB(180,180,180));}
}
static void draw_desktop(void){
    draw_wallpaper();
    /* Desktop icons */
    for(int i=0;i<NICONS;i++){
        int sel=(i==icon_sel);
        int ix=ICONS[i].x, iy=ICONS[i].y, sz=56;
        color32_t bg=sel?C_SURFACE1:C_BASE;
        RR(ix,iy,sz,sz,bg);
        if(sel) BOX(ix,iy,sz,sz,ICONS[i].ic);
        /* Native vector glyph; no generic initial-letter placeholders. */
        draw_app_icon(ICONS[i].app,ix+12,iy+4,32,ICONS[i].ic,bg);
        /* Label */
        int ll=(int)kstrlen(ICONS[i].label)*8;
        T(ix+(sz-ll)/2,iy+sz-14,ICONS[i].label,
          sel?ICONS[i].ic:C_SUBTEXT, C_BASE);
    }
    /* The lower area is owned by draw_taskbar(). */
}

/* ─── Window chrome ──────────────────────────────────────── */
static void draw_chrome(exp_win_t *w, int focused){
    color32_t bd=focused?C_TEXT:C_SURFACE2;
    color32_t tb=focused?C_SURFACE0:C_CRUST;
    color32_t tf=focused?C_TEXT:C_SUBTEXT;
    /* Layered 1px shadows give movable windows depth without gradients or
     * blue/neon styling. The warm accent is only a focus indicator. */
    R(w->x+4,w->y+4,w->w,w->h,RGB(0x00,0x00,0x00));
    R(w->x+2,w->y+2,w->w,w->h,focused?C_CRUST:C_MANTLE);
    R(w->x,w->y,w->w,w->h,C_BASE);
    BOX(w->x,w->y,w->w,w->h,bd);
    /* Title overlay is translucent over the already-rendered chrome; full
     * blur is intentionally absent because Exp has no retained compositor. */
    vbe_blend_rect(w->x+1,w->y+1,w->w-2,DE_TITLEBAR_H-1,tb,224);
    HL(w->x+1,w->y+1,w->w-2,focused?C_SURFACE2:C_SURFACE1);
    HL(w->x+1,w->y+DE_TITLEBAR_H,w->w-2,focused?C_PEACH:C_SURFACE2);
    draw_app_icon(w->app,w->x+14,w->y+3,16,tf,tb);
    /* Title is bounded before the window controls; UTF-8 is clipped only on
     * codepoint boundaries, so Cyrillic titles cannot enter control buttons. */
    TC(w->x+36,w->y+3,w->title,tf,tb,w->w-100);
    /* Compact, consistently framed window controls. */
    int bx=w->x+w->w-20;
    R(bx,w->y+3,16,15,C_RED);BOX(bx,w->y+3,16,15,focused?C_TEXT:C_SURFACE2);
    T(bx+4,w->y+3,"x",C_TEXT,C_RED);
    R(bx-20,w->y+3,16,15,C_SURFACE2);BOX(bx-20,w->y+3,16,15,C_SURFACE1);
    T(bx-16,w->y+3,w->maximized?"v":"^",C_TEXT,C_SURFACE2);
    R(bx-40,w->y+3,16,15,C_SURFACE1);BOX(bx-40,w->y+3,16,15,C_SURFACE2);
    T(bx-36,w->y+3,"_",C_TEXT,C_SURFACE1);
    if(!w->maximized){for(int d=0;d<9;d+=3)R(w->x+w->w-5-d,w->y+w->h-3,2,2,focused?C_SUBTEXT:C_SURFACE2);}
}

/* ─── Terminal ───────────────────────────────────────────── */
static void exp_prompt(char out[48]);

/* The terminal stores rendered rows, not arbitrary logical lines.  Width is
 * calculated from the current window so text never enters the scrollbar or
 * crosses the border when a window is moved/maximised. */
static int term_cols(exp_win_t *w, int prompt){
    int pixels=CW(w)-10;
    if(prompt){ char p[48]; exp_prompt(p); pixels-=4+(int)kstrlen(p)*8; }
    int cols=pixels/8;
    if(cols<1) cols=1;
    if(cols>DE_TERM_COLS) cols=DE_TERM_COLS;
    return cols;
}

static void term_store(exp_win_t *w, const char *txt, color32_t col, int prompt){
    int slot;
    if(w->tcount<DE_TERM_HIST) slot=w->tcount++;
    else { for(int i=0;i<DE_TERM_HIST-1;i++) w->tlines[i]=w->tlines[i+1]; slot=DE_TERM_HIST-1; }
    kstrncpy(w->tlines[slot].text,txt,DE_TERM_COLS+3);
    w->tlines[slot].text[DE_TERM_COLS+3]=0;
    w->tlines[slot].color=col;
    w->tlines[slot].is_prompt=(uint8_t)prompt;
}

static void term_addl(exp_win_t *w, const char *txt, color32_t col, int prompt){
    const char *p=txt?txt:"";
    int first=1;
    do {
        int max=term_cols(w,prompt&&first), n=0, last_space=-1, cut;
        char row[DE_TERM_COLS+4];
        while(p[n] && p[n]!='\n' && n<max){ if(p[n]==' '||p[n]=='\t') last_space=n; n++; }
        if(!p[n] || p[n]=='\n') cut=n;
        else if(last_space>0) cut=last_space;
        else cut=n;
        if(cut<1 && p[0]) cut=1;
        for(int i=0;i<cut;i++) row[i]=p[i];
        row[cut]=0;
        term_store(w,row,col,prompt&&first);
        first=0;
        p+=cut;
        while(*p==' '||*p=='\t') p++;
        if(*p=='\n') p++;
    } while(*p);
    w->tscroll=w->tcount;
}

void exp_capture_char(char c){
    if(capturing && cap_len<4095) cap_buf[cap_len++]=c;
}

int exp_is_active(void){ return DE.running; }
void exp_request_full_redraw(void){ exp_force_redraw=1; }
int exp_ui_scale_get(void){ return 100; }
int exp_ui_scale_set(int percent,int persist){
    (void)persist;
    /* Retained as a compatibility symbol for pre-v0.9 native applications.
       Exp's coordinate contract is now deliberately fixed at physical 100%. */
    if(percent!=100) return -1;
    exp_ui_scale_pct=100;
    return 0;
}

static void exp_prompt(char out[48]) {
    const user_account_t *u=user_current();
    kstrcpy(out,u?u->name:"unknown");
    kstrcat(out,"@"); kstrcat(out,"atmkoala");
    kstrcat(out,(u && u->role==ROLE_ADMIN)?"# ":"$ ");
}

static void draw_terminal(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    R(cx,cy,cw2,ch,C_CRUST);
    int rows=(ch-22)/16; if(rows<1)rows=1;
    int start=w->tscroll-rows; if(start<0)start=0;
    for(int i=0;i<rows&&(start+i)<w->tcount;i++){
        exp_tline_t *ln=&w->tlines[start+i];
        int ly=cy+i*16+2;
        if(ln->is_prompt){
            char prompt[48]; exp_prompt(prompt); int input_x=cx+4+(int)kstrlen(prompt)*8;
            T(cx+4,ly,prompt,C_GREEN,C_CRUST);
            char clipped[DE_TERM_COLS+4]; int max=term_cols(w,1), n=0;
            while(ln->text[n]&&n<max){clipped[n]=ln->text[n];n++;} clipped[n]=0;
            T(input_x,ly,clipped,C_TEXT,C_CRUST);
        } else {
            char clipped[DE_TERM_COLS+4]; int max=term_cols(w,0), n=0;
            while(ln->text[n]&&n<max){clipped[n]=ln->text[n];n++;} clipped[n]=0;
            T(cx+4,ly,clipped,ln->color,C_CRUST);
        }
    }
    /* Scrollbar */
    if(w->tcount>rows){
        int sbh=ch-22;
        int th=sbh*rows/w->tcount; if(th<6)th=6;
        int ty=cy+(sbh-th)*start/w->tcount;
        R(cx+cw2-5,cy,5,sbh,C_SURFACE0);
        R(cx+cw2-4,ty,3,th,C_SURFACE2);
    }
    /* Input line */
    int iy=cy+ch-20;
    HL(cx,iy-1,cw2,C_SURFACE1);
    R(cx,iy,cw2,20,C_MANTLE);
    char prompt[48]; exp_prompt(prompt); int input_x=cx+4+(int)kstrlen(prompt)*8;
    int max_input=term_cols(w,1); char shown[DE_TERM_COLS+4]; int sn=0;
    while(w->tinput[sn]&&sn<max_input){shown[sn]=w->tinput[sn];sn++;} shown[sn]=0;
    T(cx+4,iy+2,prompt,C_GREEN,C_MANTLE);
    T(input_x,iy+2,shown,C_TEXT,C_MANTLE);
    /* Cursor blink: history can contain a longer pre-wrap command, so clamp
     * the visible insertion point to the same width as the rendered input. */
    int visible_pos=w->tinput_pos; if(visible_pos>max_input) visible_pos=max_input;
    if((pit_get_ticks()/50)%2==0)
        R(input_x+visible_pos*8,iy+2,2,14,C_TEXT);
}

static void term_exec(exp_win_t *w){
    if(!w->tinput[0]) return;
    /* Add to history */
    if(w->thist_count<32) kstrcpy(w->thist[w->thist_count++],w->tinput);
    else {
        for(int i=0;i<31;i++) kstrcpy(w->thist[i],w->thist[i+1]);
        kstrcpy(w->thist[31],w->tinput);
    }
    w->thist_idx=w->thist_count;
    term_addl(w,w->tinput,C_LAVENDER,1);
    if(!kstrcmp(w->tinput,"clear")){
        w->tcount=0; w->tscroll=0;
    } else if(!kstrcmp(w->tinput,"exit")||!kstrcmp(w->tinput,"quit")){
        DE.running=0;
    } else {
        /* Capture output */
        cap_len=0; capturing=1;
        dispatch(w->tinput);
        capturing=0;
        if(cap_len>0){
            cap_buf[cap_len]=0;
            char *p=cap_buf;
            while(*p){
                char *e=p; while(*e&&*e!='\n') e++;
                char sv=*e; *e=0;
                if(*p) term_addl(w,p,C_TEXT,0);
                *e=sv; p=(*e=='\n')?e+1:e;
                if(!*p) break;
            }
        }
    }
    w->tinput[0]=0; w->tinput_pos=0;
}

static void term_key(exp_win_t *w, int k){
    int rows=CH(w)/16-2;
    if(k==KEY_PGUP){w->tscroll-=rows/2;if(w->tscroll<0)w->tscroll=0;return;}
    if(k==KEY_PGDN){w->tscroll+=rows/2;if(w->tscroll>w->tcount)w->tscroll=w->tcount;return;}
    if(k==KEY_END){w->tscroll=w->tcount;return;}
    if(k==KEY_HOME&&w->tinput_pos==0){w->tscroll=0;return;}
    if(k=='\n'||k=='\r'){term_exec(w);return;}
    if(k=='\b'||k==127){
        if(w->tinput_pos>0){
            int p=w->tinput_pos-1;
            int l=(int)kstrlen(w->tinput);
            for(int i=p;i<l;i++) w->tinput[i]=w->tinput[i+1];
            w->tinput_pos--;
        }
        return;
    }
    if(k==KEY_DEL){
        int l=(int)kstrlen(w->tinput);
        if(w->tinput_pos<l){
            for(int i=w->tinput_pos;i<l;i++) w->tinput[i]=w->tinput[i+1];
        }
        return;
    }
    if(k==KEY_LEFT){ if(w->tinput_pos>0)w->tinput_pos--; return;}
    if(k==KEY_RIGHT){ if(w->tinput[w->tinput_pos])w->tinput_pos++; return;}
    if(k==KEY_HOME){ w->tinput_pos=0; return;}
    if(k==KEY_UP){
        if(w->thist_idx>0){
            w->thist_idx--;
            kstrcpy(w->tinput,w->thist[w->thist_idx]);
            w->tinput_pos=(int)kstrlen(w->tinput);
        }
        return;
    }
    if(k==KEY_DOWN){
        if(w->thist_idx<w->thist_count-1){
            w->thist_idx++;
            kstrcpy(w->tinput,w->thist[w->thist_idx]);
            w->tinput_pos=(int)kstrlen(w->tinput);
        } else {
            w->thist_idx=w->thist_count;
            w->tinput[0]=0; w->tinput_pos=0;
        }
        return;
    }
    if(k=='\t'){
        /* Tab completion from cwd */
        char *last=w->tinput; char *p=w->tinput;
        while(*p){if(*p==' ')last=p+1;p++;}
        int plen=(int)kstrlen(last);
        char ns[32][VFS_NAME_MAX + 1]; int cnt=0;
        vfs_listdir("/",&ns[0],&cnt);
        char best[VFS_NAME_MAX + 1]={0}; int matches=0;
        for(int i=0;i<cnt;i++){
            char nm[VFS_NAME_MAX + 1]; kstrcpy(nm,ns[i]);
            int nl=(int)kstrlen(nm); if(nl&&nm[nl-1]=='/') nm[nl-1]=0;
            if(plen==0||kstrncmp(nm,last,(size_t)plen)==0){
                if(++matches==1) kstrcpy(best,ns[i]);
            }
        }
        if(matches==1){
            int bl=(int)kstrlen(best); int add=bl-plen;
            int tl=(int)kstrlen(w->tinput);
            if(tl+add<term_cols(w,1)){
                kstrncpy(w->tinput+tl,best+plen,(size_t)add);
                w->tinput[tl+add]=' '; w->tinput[tl+add+1]=0;
                w->tinput_pos=tl+add+1;
            }
        }
        return;
    }
    if(k==3){term_addl(w,"^C",C_RED,0);w->tinput[0]=0;w->tinput_pos=0;return;}
    if(k==12){w->tcount=0;w->tscroll=0;w->tinput[0]=0;w->tinput_pos=0;return;}
    /* UTF-8 Russian */
    extern char keyboard_utf8_buf[4];
    if(k==0x200){
        int ul=(int)kstrlen(keyboard_utf8_buf);
        int tl=(int)kstrlen(w->tinput);
        if(tl+ul<term_cols(w,1)){
            for(int i=tl+ul;i>=w->tinput_pos+ul;i--)
                w->tinput[i]=w->tinput[i-ul];
            for(int i=0;i<ul;i++)
                w->tinput[w->tinput_pos+i]=keyboard_utf8_buf[i];
            w->tinput_pos+=ul;
        }
        return;
    }
    if(k>=0x20&&k<=0x7E){
        int tl=(int)kstrlen(w->tinput);
        if(tl<term_cols(w,1)){
            for(int i=tl;i>=w->tinput_pos;i--) w->tinput[i+1]=w->tinput[i];
            w->tinput[w->tinput_pos]=(char)k;
            w->tinput_pos++;
        }
    }
}

/* ─── File Manager ───────────────────────────────────────── */
static void fm_load(exp_win_t *w){
    w->fm_count=0;
    if(kstrcmp(w->fm_path,"/")!=0){
        kstrcpy(w->fm_ent[0].name,"..");
        w->fm_ent[0].is_dir=1; w->fm_ent[0].size=0;
        w->fm_count=1;
    }
    char ns[FM_MAX_ENTRIES][VFS_NAME_MAX + 1]; int cnt=0;
    if(vfs_listdir(w->fm_path,&ns[0],&cnt)<0) return;
    for(int i=0;i<cnt&&w->fm_count<FM_MAX_ENTRIES;i++){
        kstrncpy(w->fm_ent[w->fm_count].name,ns[i],FM_NAME_LEN-1);
        int nl=(int)kstrlen(ns[i]);
        w->fm_ent[w->fm_count].is_dir=(nl>0&&ns[i][nl-1]=='/');
        char fp[128]; kstrcpy(fp,w->fm_path);
        if(fp[kstrlen(fp)-1]!='/') kstrcat(fp,"/");
        kstrcat(fp,ns[i]);
        vfs_stat_t st; if(vfs_stat(fp,&st)==0) w->fm_ent[w->fm_count].size=st.size;
        else w->fm_ent[w->fm_count].size=0;
        w->fm_count++;
    }
    if(w->fm_sel>=w->fm_count) w->fm_sel=w->fm_count-1;
    if(w->fm_sel<0) w->fm_sel=0;
    w->fm_preview[0]=0;
}

static void fm_preview(exp_win_t *w){
    if(w->fm_sel<0||w->fm_sel>=w->fm_count||w->fm_ent[w->fm_sel].is_dir){
        w->fm_preview[0]=0; return;
    }
    char fp[128]; kstrcpy(fp,w->fm_path);
    if(fp[kstrlen(fp)-1]!='/') kstrcat(fp,"/");
    kstrcat(fp,w->fm_ent[w->fm_sel].name);
    int fd=vfs_open(fp,O_RDONLY, 0); if(fd<0) return;
    int n=vfs_read(fd,(uint8_t*)w->fm_preview,510);
    vfs_close(fd);
    if(n>0) w->fm_preview[n]=0; else w->fm_preview[0]=0;
}

static void draw_files(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    R(cx,cy,cw2,ch,C_BASE);
    /* Path bar */
    R(cx,cy,cw2,18,C_MANTLE);
    TF(cx+4,cy+1,C_LAVENDER,C_MANTLE," %s",w->fm_path);
    HL(cx,cy+18,cw2,C_SURFACE0);
    /* Two panels */
    int lw2=(cw2*3)/5, rw2=cw2-lw2-4, rh=17;
    int start=w->fm_scroll, mr=(ch-22)/rh;
    for(int i=0;i<mr&&(start+i)<w->fm_count;i++){
        fm_entry_t *e=&w->fm_ent[start+i];
        int ry=cy+22+i*rh;
        int sel2=(start+i==w->fm_sel);
        color32_t bg=sel2?C_SURFACE1:((i%2==0)?C_BASE:C_MANTLE);
        R(cx,ry,lw2,rh,bg);
        if(sel2) HL(cx,ry,lw2,C_BLUE);
        T(cx+4,ry+1,e->is_dir?"[D]":"[F]",e->is_dir?C_YELLOW:C_TEXT,bg);
        char nm2[FM_NAME_LEN]; kstrcpy(nm2,e->name);
        int nl2=(int)kstrlen(nm2);
        if(nl2>1&&nm2[nl2-1]=='/') nm2[nl2-1]=0;
        int maxn=(lw2-60)/8; if(maxn<4)maxn=4;
        if((int)kstrlen(nm2)>maxn){nm2[maxn-2]='.';nm2[maxn-1]='.';nm2[maxn]=0;}
        T(cx+32,ry+1,nm2,sel2?C_TEXT:(e->is_dir?C_YELLOW:C_TEXT),bg);
        if(!e->is_dir&&e->size>0){
            char sz[12];
            if(e->size>=1024){kuitoa(e->size/1024,sz,10);kstrcat(sz,"K");}
            else{kuitoa(e->size,sz,10);}
            T(cx+lw2-44,ry+1,sz,C_SUBTEXT,bg);
        }
    }
    VL(cx+lw2+1,cy+20,ch-20,C_SURFACE1);
    /* Preview panel */
    int px2=cx+lw2+4, py2=cy+22;
    R(px2,cy+20,rw2,ch-20,C_MANTLE);
    T(px2+4,py2,"Preview",C_LAVENDER,C_MANTLE); py2+=20;
    HL(px2,py2,rw2,C_SURFACE0); py2+=4;
    if(w->fm_sel>=0&&w->fm_sel<w->fm_count){
        fm_entry_t *e=&w->fm_ent[w->fm_sel];
        char nm3[FM_NAME_LEN]; kstrcpy(nm3,e->name);
        int nl3=(int)kstrlen(nm3);
        if(nl3>1&&nm3[nl3-1]=='/') nm3[nl3-1]=0;
        TF(px2+4,py2,C_LAVENDER,C_MANTLE,"%.16s",nm3); py2+=16;
        TF(px2+4,py2,C_SUBTEXT,C_MANTLE,"%s",e->is_dir?"folder":"file"); py2+=16;
        if(!e->is_dir&&e->size>0){
            char sz2[16]; kuitoa(e->size,sz2,10); kstrcat(sz2," B");
            T(px2+4,py2,sz2,C_SUBTEXT,C_MANTLE); py2+=16;
        }
        py2+=8;
        if(w->fm_preview[0]&&!e->is_dir){
            T(px2+4,py2,"---",C_SURFACE2,C_MANTLE); py2+=12;
            const char *pp=w->fm_preview; int plines=0;
            int mpl=(ch-(py2-cy))/13; if(mpl<0)mpl=0;
            while(*pp&&plines<mpl){
                char ln2[18]; int li=0;
                while(*pp&&*pp!='\n'&&li<(rw2/8)-1) ln2[li++]=*pp++;
                ln2[li]=0; if(*pp=='\n') pp++;
                T(px2+4,py2,ln2,C_SUBTEXT,C_MANTLE);
                py2+=13; plines++;
            }
        }
    }
    /* Scrollbar */
    if(w->fm_count>mr){
        int sbh=ch-22; int th=sbh*mr/w->fm_count; if(th<6)th=6;
        int ty=cy+22+(sbh-th)*start/w->fm_count;
        R(cx+lw2-4,cy+22,4,sbh,C_SURFACE0);
        R(cx+lw2-3,ty,2,th,C_SURFACE2);
    }
    HL(cx,cy+ch-16,cw2,C_SURFACE0);
    R(cx,cy+ch-15,cw2,15,C_MANTLE);
    T(cx+4,cy+ch-14,"Enter=open  N=new folder  T=trash  R=trash  Bksp=up",C_SUBTEXT,C_MANTLE);
}

static int fm_selected_path(exp_win_t *w,char out[128]){
    if(!w||!out||w->fm_sel<0||w->fm_sel>=w->fm_count)return -1;
    fm_entry_t *e=&w->fm_ent[w->fm_sel];if(!e->name[0]||!kstrcmp(e->name,".."))return -1;
    kstrcpy(out,w->fm_path);if(out[kstrlen(out)-1]!='/')kstrcat(out,"/");kstrcat(out,e->name);size_t n=kstrlen(out);if(n>1&&out[n-1]=='/')out[n-1]=0;return 0;
}
static void fm_trash_selected(exp_win_t *w){
    char src[128],dst[128],name[FM_NAME_LEN],suffix[16];
    if(!catfs_vfs_is_mounted()){exp_notify("Trash needs mounted CatFS /data",C_RED);return;}
    if(fm_selected_path(w,src)<0){exp_notify("Select a file or folder to trash",C_RED);return;}
    if(kstrncmp(src,"/data/",6)||!kstrncmp(src,"/data/uiu/Trash",15)){exp_notify("Trash moves only CatFS /data items",C_RED);return;}
    kstrcpy(name,w->fm_ent[w->fm_sel].name);int nl=(int)kstrlen(name);if(nl&&name[nl-1]=='/')name[nl-1]=0;
    (void)vfs_mkdir("/data/uiu",0755);(void)vfs_mkdir("/data/uiu/Trash",0700);
    kstrcpy(dst,"/data/uiu/Trash/");kstrcat(dst,name);vfs_stat_t st;
    if(vfs_stat(dst,&st)==0){kuitoa(pit_get_ticks(),suffix,10);if(kstrlen(dst)+1+kstrlen(suffix)>=sizeof(dst)){exp_notify("Trash name too long",C_RED);return;}kstrcat(dst,"-");kstrcat(dst,suffix);}
    if(vfs_rename(src,dst)<0){exp_notify("Trash move denied or failed",C_RED);return;}
    exp_notify("Moved to CatFS Trash",C_GREEN);w->fm_sel=0;w->fm_scroll=0;fm_load(w);
}
static void fm_open_trash(exp_win_t *w){if(!catfs_vfs_is_mounted()){exp_notify("Trash needs mounted CatFS /data",C_RED);return;}(void)vfs_mkdir("/data/uiu",0755);(void)vfs_mkdir("/data/uiu/Trash",0700);kstrcpy(w->fm_path,"/data/uiu/Trash");w->fm_sel=0;w->fm_scroll=0;fm_load(w);}
static void fm_new_folder(exp_win_t *w){
    char dst[128],suffix[16];size_t base=kstrlen(w->fm_path);int slash=base&&w->fm_path[base-1]!='/';
    for(int n=0;n<32;n++){
        suffix[0]=0;if(n){kitoa(n+1,suffix,10);}
        if(base+(slash?1u:0u)+10u+(n?1u+kstrlen(suffix):0u)>=sizeof(dst))break;
        kstrcpy(dst,w->fm_path);if(slash)kstrcat(dst,"/");kstrcat(dst,"New Folder");if(n){kstrcat(dst," ");kstrcat(dst,suffix);}
        if(vfs_mkdir(dst,0755)==0){exp_notify("Created folder",C_GREEN);w->fm_sel=0;w->fm_scroll=0;fm_load(w);return;}
    }
    exp_notify("Cannot create folder here",C_RED);
}
static int fm_clip_target(exp_win_t *w,char out[128]){if(!w||!w->fm_clip_path[0])return -1;const char *name=w->fm_clip_path,*p=name;while(*p){if(*p=='/')name=p+1;p++;}if(!name[0])return -1;kstrcpy(out,w->fm_path);if(out[kstrlen(out)-1]!='/')kstrcat(out,"/");if(kstrlen(out)+kstrlen(name)>=128)return -1;kstrcat(out,name);return kstrcmp(out,w->fm_clip_path)==0?-1:0;}
static void fm_clip_selected(exp_win_t *w,int move){char src[128];vfs_stat_t st;if(fm_selected_path(w,src)<0||vfs_stat(src,&st)<0||!S_ISREG(st.st_mode)){exp_notify("Clipboard supports regular files only",C_RED);return;}kstrcpy(w->fm_clip_path,src);w->fm_clip_move=move;exp_notify(move?"Move source selected; open destination and press P":"Copy source selected; open destination and press P",C_GREEN);}
static void fm_paste_clip(exp_win_t *w){char dst[128];vfs_stat_t st;if(!w->fm_clip_path[0]||fm_clip_target(w,dst)<0){exp_notify("Open another folder before paste",C_RED);return;}if(vfs_stat(dst,&st)==0){exp_notify("Paste refuses to overwrite existing file",C_RED);return;}if(vfs_stat(w->fm_clip_path,&st)<0||!S_ISREG(st.st_mode)||st.st_size>262144u){exp_notify("Paste supports regular files up to 256 KiB",C_RED);return;}if(w->fm_clip_move){if(vfs_rename(w->fm_clip_path,dst)<0){exp_notify("Move denied or cross-mount failed",C_RED);return;}w->fm_clip_path[0]=0;exp_notify("Moved file",C_GREEN);fm_load(w);return;}int in=vfs_open(w->fm_clip_path,O_RDONLY,0),out=-1;if(in<0||(out=vfs_open(dst,O_WRONLY|O_CREAT|O_EXCL,0644))<0){if(in>=0)vfs_close(in);exp_notify("Cannot create paste target",C_RED);return;}uint8_t buf[512];int bad=0;for(;;){int n=vfs_read(in,buf,sizeof(buf));if(n<0){bad=1;break;}if(n==0)break;if(vfs_write(out,buf,(size_t)n)!=n){bad=1;break;}}vfs_close(in);vfs_close(out);if(bad){(void)vfs_unlink(dst);exp_notify("Copy failed; partial file removed",C_RED);return;}exp_notify("Copied file",C_GREEN);fm_load(w);}

static void fm_key(exp_win_t *w, int k){
    int mr=CH(w)/17;
    if(k==KEY_UP&&w->fm_sel>0){
        w->fm_sel--;
        if(w->fm_sel<w->fm_scroll) w->fm_scroll--;
        fm_preview(w);
    }
    if(k==KEY_DOWN&&w->fm_sel<w->fm_count-1){
        w->fm_sel++;
        if(w->fm_sel>=w->fm_scroll+mr) w->fm_scroll++;
        fm_preview(w);
    }
    if(k==KEY_PGUP){
        w->fm_sel-=mr; if(w->fm_sel<0)w->fm_sel=0;
        w->fm_scroll-=mr; if(w->fm_scroll<0)w->fm_scroll=0;
        fm_preview(w);
    }
    if(k==KEY_PGDN){
        w->fm_sel+=mr; if(w->fm_sel>=w->fm_count)w->fm_sel=w->fm_count-1;
        w->fm_scroll+=mr;
        if(w->fm_scroll>w->fm_count-mr&&w->fm_count>mr)w->fm_scroll=w->fm_count-mr;
        if(w->fm_scroll<0)w->fm_scroll=0;
        fm_preview(w);
    }
    if(k=='\n'||k=='\r'){
        if(w->fm_sel<0||w->fm_sel>=w->fm_count) return;
        fm_entry_t *e=&w->fm_ent[w->fm_sel];
        if(e->is_dir){
            if(kstrcmp(e->name,"..")==0){
                char *last=w->fm_path, *p=w->fm_path;
                while(*p){if(*p=='/')last=p;p++;}
                if(last!=w->fm_path)*last=0;
                else{w->fm_path[0]='/';w->fm_path[1]=0;}
            } else {
                char nm4[FM_NAME_LEN]; kstrcpy(nm4,e->name);
                int nl4=(int)kstrlen(nm4);
                if(nl4>0&&nm4[nl4-1]=='/') nm4[nl4-1]=0;
                if(w->fm_path[kstrlen(w->fm_path)-1]!='/') kstrcat(w->fm_path,"/");
                kstrcat(w->fm_path,nm4);
            }
            w->fm_sel=0; w->fm_scroll=0; fm_load(w);
        } else {
            char fp[128]; kstrcpy(fp,w->fm_path);
            if(fp[kstrlen(fp)-1]!='/') kstrcat(fp,"/");
            kstrcat(fp,e->name);
            file_fmt_t ff=fmt_detect(NULL,0,fp);
            if(ff==FMT_TAR_ZST) exp_open_app(APP_ARCHIVEEX,fp);
            else exp_open_app(APP_VIEWER,fp);
        }
    }
    if(k=='n'||k=='N'){fm_new_folder(w);return;}
    if(k=='c'||k=='C'){fm_clip_selected(w,0);return;}
    if(k=='m'||k=='M'){fm_clip_selected(w,1);return;}
    if(k=='p'||k=='P'){fm_paste_clip(w);return;}
    if(k=='t'||k=='T'){fm_trash_selected(w);return;}
    if(k=='r'||k=='R'){fm_open_trash(w);return;}
    if(k=='\b'){
        char *last=w->fm_path, *p=w->fm_path;
        while(*p){if(*p=='/')last=p;p++;}
        if(last!=w->fm_path)*last=0;
        else{w->fm_path[0]='/';w->fm_path[1]=0;}
        w->fm_sel=0; w->fm_scroll=0; fm_load(w);
    }
}

/* ─── System Monitor ─────────────────────────────────────── */
static void sysmon_upd(exp_win_t *w){
    uint32_t now=sched_uptime_ticks();
    sysmon_t *s=&w->sysmon;
    if(now-s->last_tick<50) return;
    uint32_t idle=sched_idle_ticks(),elapsed=now-s->last_tick,idle_delta=idle-s->last_idle_tick;
    uint32_t cpu=elapsed?(100u-((idle_delta>=elapsed)?100u:(idle_delta*100u)/elapsed)):0u;
    uint32_t used=heap_used_bytes(),total=used+heap_free_bytes();
    uint32_t ro=disk_read_ops,wo=disk_write_ops,ops=(ro-s->last_read_ops)+(wo-s->last_write_ops);
    s->cpu[s->head]=(uint8_t)cpu;
    s->mem[s->head]=total?(uint8_t)((used*100u)/total):0u;
    s->io[s->head]=(uint8_t)(ops>100?100:ops); /* operations per sample interval */
    s->last_tick=now;s->last_idle_tick=idle;s->last_read_ops=ro;s->last_write_ops=wo;
    s->head=(s->head+1)%SYSMON_HISTORY;
}

static void draw_graph(int gx,int gy,int gw,int gh,
                       uint8_t *data,int head,
                       color32_t lc,color32_t bgc,
                       const char *lbl,uint8_t val){
    R(gx,gy,gw,gh,bgc); BOX(gx,gy,gw,gh,C_SURFACE1);
    for(int qi=1;qi<4;qi++) HL(gx+1,gy+gh*qi/4,gw-2,C_SURFACE0);
    for(int qi=0;qi<SYSMON_HISTORY-1;qi++){
        int i1=(head+qi)%SYSMON_HISTORY;
        int i2=(head+qi+1)%SYSMON_HISTORY;
        int x1=gx+1+qi*(gw-2)/SYSMON_HISTORY;
        int y1=gy+gh-1-(int)data[i1]*(gh-2)/100;
        int y2=gy+gh-1-(int)data[i2]*(gh-2)/100;
        int ymin=y1<y2?y1:y2, ymax=y1>y2?y1:y2;
        VL(x1,ymin,ymax-ymin+1,lc);
        /* Fill under curve */
        color32_t fc=RGB(((lc>>16)&0xFF)/4,
                         ((lc>>8)&0xFF)/4,
                         ((lc   )&0xFF)/4);
        VL(x1,y1,gy+gh-y1,fc);
    }
    char vb[8]; kuitoa(val,vb,10); kstrcat(vb,"%");
    TF(gx+4,gy+2,lc,bgc,"%s %s",lbl,vb);
}

static uint32_t image_blend(const uint8_t *p,uint32_t bg){uint32_t a=p[3],r=(p[0]*a+((bg>>16)&255)*(255-a))/255,g=(p[1]*a+((bg>>8)&255)*(255-a))/255,b=(p[2]*a+(bg&255)*(255-a))/255;return RGB(r,g,b);}
static void image_viewer_load(exp_win_t *w,const char *path){
    atm_image_release(&w->image);kstrcpy(w->image_path,path?path:"");w->image_zoom=0;w->image_pan_x=0;w->image_pan_y=0;
    if(atm_image_decode_file(w->image_path,&w->image)<0){if(!w->image.error[0])kstrcpy(w->image.error,"Unable to decode image");}
}
static void draw_image_viewer(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);R(cx,cy,cw2,ch,C_BASE);R(cx,cy,cw2,23,C_MANTLE);
    T(cx+8,cy+5,w->image_path[0]?w->image_path:"No image",C_TEXT,C_MANTLE);
    if(!w->image.rgba){T(cx+12,cy+44,"Image preview unavailable",C_RED,C_BASE);T(cx+12,cy+64,w->image.error[0]?w->image.error:"Select a PNG, JPEG or BMP file in Files.",C_SUBTEXT,C_BASE);return;}
    int vx=EXP_SCALE(cx+8),vy=EXP_SCALE(cy+30),vw=EXP_SCALE(cw2-16),vh=EXP_SCALE(ch-58);if(vw<1||vh<1)return;
    for(int yy=0;yy<vh;yy+=8)for(int xx=0;xx<vw;xx+=8)vbe_fill_rect(vx+xx,vy+yy,(xx+8<vw)?8:vw-xx,(yy+8<vh)?8:vh-yy,((xx/8+yy/8)&1)?RGB(0x31,0x31,0x31):RGB(0x22,0x22,0x22));
    /* image_zoom is a percentage of the proportional fit (0 = fit), rather
     * than a literal source-pixel scale. This keeps + meaningful for small
     * fixtures and clips magnified content safely to the visible canvas. */
    int fitw=vw,fith=(w->image.height*fitw)/w->image.width;
    if(fith>vh){fith=vh;fitw=(w->image.width*fith)/w->image.height;}
    int dw=fitw,dh=fith;
    if(w->image_zoom>0){dw=(fitw*w->image_zoom)/100;dh=(fith*w->image_zoom)/100;}
    if(dw<1)dw=1;if(dh<1)dh=1;
    int ox=vx+(vw-dw)/2,oy=vy+(vh-dh)/2;
    int x0=ox<vx?vx:ox,y0=oy<vy?vy:oy;
    int x1=ox+dw>vx+vw?vx+vw:ox+dw,y1=oy+dh>vy+vh?vy+vh:oy+dh;
    for(int y=y0;y<y1;y++){int sy=((y-oy)*w->image.height)/dh;for(int x=x0;x<x1;x++){int sx=((x-ox)*w->image.width)/dw;const uint8_t *p=w->image.rgba+((sy*w->image.width+sx)*4);vbe_putpixel(x,y,image_blend(p,(((x-vx)/8+(y-vy)/8)&1)?RGB(0x31,0x31,0x31):RGB(0x22,0x22,0x22)));}}
    char inf[112],n[16];kstrcpy(inf,atm_image_format_name(w->image.format));kstrcat(inf,"  ");kitoa(w->image.width,n,10);kstrcat(inf,n);kstrcat(inf,"x");kitoa(w->image.height,n,10);kstrcat(inf,n);kstrcat(inf,"  ");if(w->image_zoom>0){kitoa(w->image_zoom,n,10);kstrcat(inf,n);kstrcat(inf,"% of fit");}else kstrcat(inf,"fit");kstrcat(inf,"  + / - zoom, 0 fit, A apply wallpaper");T(cx+8,cy+ch-20,inf,C_SUBTEXT,C_BASE);
}
static void image_viewer_key(exp_win_t *w,int k){
    if(!w->image.rgba)return;
    if(k=='+'){w->image_zoom=w->image_zoom?w->image_zoom+25:125;if(w->image_zoom>400)w->image_zoom=400;}
    else if(k=='-'){w->image_zoom=w->image_zoom?w->image_zoom-25:75;if(w->image_zoom<25)w->image_zoom=25;}
    else if(k=='0'||k=='f'||k=='F')w->image_zoom=0;
    else if(k=='a'||k=='A'){
        if(exp_wallpaper_apply(w->image_path)){char msg[96];kstrcpy(msg,"Image wallpaper applied: ");kstrcat(msg,w->image_path);exp_notify(msg,C_GREEN);}else exp_notify("Wallpaper apply failed",C_RED);
    }
    exp_force_redraw=1;
}

/* ─── ArchiveEx: validated native TAR.ZST/ATPK frontend ─── */
static void archiveex_release(exp_win_t *w){if(w->archive_data){kfree(w->archive_data);w->archive_data=NULL;}w->archive_size=0;w->archive_valid=0;}
static void archiveex_copy(char *dst,uint32_t cap,const char *src){if(!cap)return;kstrncpy(dst,src?src:"",cap-1);dst[cap-1]=0;}
static void archiveex_load(exp_win_t *w,const char *path){
    archiveex_release(w);kmemset(w->archive_name,0,sizeof(w->archive_name));kmemset(w->archive_version,0,sizeof(w->archive_version));kmemset(w->archive_description,0,sizeof(w->archive_description));
    archiveex_copy(w->archive_path,sizeof(w->archive_path),path);if(!path||!path[0]){kstrcpy(w->archive_message,"Choose a .tar.zst or .atpk file in Files.");return;}
    int fd=vfs_open(path,O_RDONLY,0);vfs_stat_t st;if(fd<0||vfs_fstat(fd,&st)<0||!st.st_size||st.st_size>TZST_MAX_UNPACKED){if(fd>=0)vfs_close(fd);kstrcpy(w->archive_message,"Archive missing, empty or exceeds 256 KiB limit.");return;}
    w->archive_data=(uint8_t*)kmalloc((size_t)st.st_size);if(!w->archive_data){vfs_close(fd);kstrcpy(w->archive_message,"Insufficient memory for archive inspection.");return;}
    int64_t got=vfs_read(fd,w->archive_data,st.st_size);vfs_close(fd);if(got!=(int64_t)st.st_size){archiveex_release(w);kstrcpy(w->archive_message,"Archive read failed.");return;}
    w->archive_size=(uint32_t)st.st_size;tzst_pkg_t pkg;int rc=tzst_parse(&pkg,w->archive_data,w->archive_size);if(rc<0){archiveex_release(w);ksnprintf(w->archive_message,sizeof(w->archive_message),"Rejected by archive validator (rc=%d).",rc);return;}
    archiveex_copy(w->archive_name,sizeof(w->archive_name),pkg.package_name);archiveex_copy(w->archive_version,sizeof(w->archive_version),pkg.version);archiveex_copy(w->archive_description,sizeof(w->archive_description),pkg.description);w->archive_entries=pkg.atpk?pkg.payload_count:pkg.file_count;w->archive_atpk=pkg.atpk;w->archive_valid=1;
    kstrcpy(w->archive_message,pkg.atpk?"Validated ATPK; press I to install.":"Validated legacy archive; press I for compatibility install.");
}
static void archiveex_install(exp_win_t *w){
    if(!w->archive_valid||!w->archive_data){kstrcpy(w->archive_message,"No validated archive loaded.");return;}
    if(!user_is_admin()){kstrcpy(w->archive_message,"Installation requires an administrator session.");return;}
    tzst_pkg_t pkg;int rc=tzst_parse(&pkg,w->archive_data,w->archive_size);if(rc<0){ksnprintf(w->archive_message,sizeof(w->archive_message),"Archive changed/invalid (rc=%d).",rc);w->archive_valid=0;return;}
    rc=tzst_install(&pkg);if(rc<0)ksnprintf(w->archive_message,sizeof(w->archive_message),"Install rejected/rolled back (rc=%d).",rc);else kstrcpy(w->archive_message,"Install completed. Review package registry for status.");
}
static void draw_archiveex(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);R(cx,cy,cw2,ch,C_BASE);R(cx,cy,cw2,24,C_MANTLE);T(cx+8,cy+5,"ARCHIVEEX",C_TEXT,C_MANTLE);T(cx+110,cy+5,w->archive_atpk?"ATPK native package":"TAR.ZST compatibility archive",C_SUBTEXT,C_MANTLE);
    T(cx+12,cy+42,w->archive_path[0]?w->archive_path:"No archive selected",C_LAVENDER,C_BASE);if(!w->archive_valid){T(cx+12,cy+72,w->archive_message,C_RED,C_BASE);T(cx+12,cy+100,"Only raw/RLE Zstandard frames within 256 KiB are accepted.",C_SUBTEXT,C_BASE);return;}
    char line[160];ksnprintf(line,sizeof(line),"Name: %s",w->archive_name);T(cx+12,cy+76,line,C_TEXT,C_BASE);ksnprintf(line,sizeof(line),"Version: %s  |  Entries: %u",w->archive_version[0]?w->archive_version:"-",w->archive_entries);T(cx+12,cy+96,line,C_SUBTEXT,C_BASE);
    if(w->archive_description[0]){ksnprintf(line,sizeof(line),"Description: %s",w->archive_description);T(cx+12,cy+116,line,C_SUBTEXT,C_BASE);}R(cx+12,cy+ch-62,cw2-24,46,C_SURFACE0);T(cx+20,cy+ch-56,w->archive_message,C_TEXT,C_SURFACE0);T(cx+20,cy+ch-36,"I install (administrator only)  |  R reload archive  |  Esc closes",C_SUBTEXT,C_SURFACE0);
}
static void archiveex_key(exp_win_t *w,int k){if(k=='r'||k=='R')archiveex_load(w,w->archive_path);else if(k=='i'||k=='I')archiveex_install(w);exp_force_redraw=1;}

static void draw_sysmon(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);R(cx,cy,cw2,ch,C_BASE);sysmon_upd(w);
    sysmon_t *s=&w->sysmon;int last=(s->head+SYSMON_HISTORY-1)%SYSMON_HISTORY;
    int gh=(ch-102)/3;if(gh<28)gh=28;
    draw_graph(cx+4,cy+4,cw2-8,gh,s->cpu,s->head,C_BLUE,C_CRUST,"CPU",s->cpu[last]);
    draw_graph(cx+4,cy+gh+10,cw2-8,gh,s->mem,s->head,C_GREEN,C_CRUST,"RAM",s->mem[last]);
    draw_graph(cx+4,cy+2*gh+16,cw2-8,gh,s->io,s->head,C_PEACH,C_CRUST,"DISK I/O",s->io[last]);
    int ty=cy+3*gh+24;uint32_t up=sched_uptime_ticks()/100;char line[128],num[16];atm_memory_status_t mem;atminit_memory_status(&mem);
    color32_t lamp=mem.pressure==ATM_MEMORY_CRITICAL?C_RED:(mem.pressure==ATM_MEMORY_WARN?C_YELLOW:C_GREEN);
    R(cx+8,ty+2,9,9,lamp);BOX(cx+8,ty+2,9,9,C_SURFACE2);ksnprintf(line,sizeof(line),"HEAP LAMP %s  %u%% used  | resident %u B (%u task%s)",atminit_memory_pressure_name(mem.pressure),mem.heap_percent,(uint32_t)mem.task_resident,mem.task_count,mem.task_count==1?"":"s");T(cx+22,ty,line,C_SUBTEXT,C_BASE);ty+=14;
    kstrcpy(line,"Uptime ");kitoa(up/3600,num,10);kstrcat(line,num);kstrcat(line,":");kitoa((up/60)%60,num,10);kstrcat(line,num);kstrcat(line,":");kitoa(up%60,num,10);kstrcat(line,num);kstrcat(line,"  Tasks ");kitoa(sched_task_count(),num,10);kstrcat(line,num);kstrcat(line,"  Heap free ");kitoa(heap_free_bytes(),num,10);kstrcat(line,num);kstrcat(line," B");T(cx+8,ty,line,C_SUBTEXT,C_BASE);ty+=14;
    kstrcpy(line,"ATA ");kitoa(disk_count,num,10);kstrcat(line,num);kstrcat(line,"  reads ");kitoa(disk_read_ops,num,10);kstrcat(line,num);kstrcat(line,"  writes ");kitoa(disk_write_ops,num,10);kstrcat(line,num);kstrcat(line,"  errors ");kitoa(disk_io_errors,num,10);kstrcat(line,num);kstrcat(line,"  Net ");kstrcat(line,net.initialized?"UP":"--");T(cx+8,ty,line,C_SUBTEXT,C_BASE);ty+=14;
    for(int i=0;i<DISK_MAX_DRIVES&&ty<cy+ch-12;i++)if(disk_drives[i].present){char name[18];kstrncpy(name,disk_drives[i].model,16);name[16]=0;kstrcpy(line,"hd");line[2]=(char)('a'+i);line[3]=0;kstrcat(line,"  ");kstrcat(line,name[0]?name:"ATA Disk");kstrcat(line,"  ");kitoa(disk_capacity_mib(i),num,10);kstrcat(line,num);kstrcat(line," MiB  ");kstrcat(line,disk_drives[i].lba48?"LBA48":"LBA28");T(cx+8,ty,line,C_TEXT,C_BASE);ty+=14;}
}

/* ─── System Info and Events ─────────────────────────────── */
static void draw_sysinfo(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w),y=cy+12;char line[160];
    sdk_cpuid_t cpu;sdk_cpuid(&cpu);sdk_meminfo_t mi;sdk_meminfo(&mi);atm_memory_status_t ms;atminit_memory_status(&ms);
    R(cx,cy,cw2,ch,C_BASE);T(cx+12,y,"SYSTEM INFORMATION",C_LAVENDER,C_BASE);y+=22;
    ksnprintf(line,sizeof(line),"ATMKoala v0.5  |  x86-64  |  ABI v1.22  |  uptime %u s",sched_uptime_ticks()/100u);T(cx+12,y,line,C_TEXT,C_BASE);y+=20;
    R(cx+8,y-4,cw2-16,1,C_SURFACE2);y+=10;T(cx+12,y,"CPU (CPUID-detected)",C_BLUE,C_BASE);y+=16;
    ksnprintf(line,sizeof(line),"Vendor: %s   Family %u  Model %u  Stepping %u",cpu.vendor,cpu.family,cpu.model,cpu.stepping);T(cx+20,y,line,C_SUBTEXT,C_BASE);y+=15;
    ksnprintf(line,sizeof(line),"Brand: %s",cpu.brand[0]?cpu.brand:"unavailable");TC(cx+20,y,line,C_TEXT,C_BASE,cw2-32);y+=15;
    ksnprintf(line,sizeof(line),"FPU %s  MMX %s  SSE %s  SSE2 %s  APIC %s",cpu.has_fpu?"yes":"no",cpu.has_mmx?"yes":"no",cpu.has_sse?"yes":"no",cpu.has_sse2?"yes":"no",cpu.has_apic?"yes":"no");T(cx+20,y,line,C_SUBTEXT,C_BASE);y+=22;
    T(cx+12,y,"MEMORY / TASKS",C_GREEN,C_BASE);y+=16;
    ksnprintf(line,sizeof(line),"Physical discovery: %u MiB  |  Heap: %u / %u KiB (%u%%, %s)",mi.total_phys/(1024u*1024u),ms.heap_used/1024u,(ms.heap_used+ms.heap_free)/1024u,ms.heap_percent,atminit_memory_pressure_name(ms.pressure));T(cx+20,y,line,C_SUBTEXT,C_BASE);y+=15;
    ksnprintf(line,sizeof(line),"Tasks: %u  |  tracked task-resident: %u KiB (not process RSS)",ms.task_count,(uint32_t)(ms.task_resident/1024u));T(cx+20,y,line,C_SUBTEXT,C_BASE);y+=22;
    T(cx+12,y,"DEVICES / RUNTIME",C_PEACH,C_BASE);y+=16;
    ksnprintf(line,sizeof(line),"ATA disks: %u  reads %u  writes %u  I/O errors %u",disk_count,disk_read_ops,disk_write_ops,disk_io_errors);T(cx+20,y,line,C_SUBTEXT,C_BASE);y+=15;
    ksnprintf(line,sizeof(line),"Network stack: %s  |  Wi-Fi controller: %s  driver: %s  associated: %s",net.initialized?"initialized":"unavailable",g_radio.wifi_controller_present?"detected":"none",g_radio.wifi_driver_ready?"ready":"no",g_radio.wifi_connected?"yes":"no");TC(cx+20,y,line,C_SUBTEXT,C_BASE,cw2-32);y+=15;
    ksnprintf(line,sizeof(line),"Battery: %s  |  Bluetooth controller: %s  driver: %s",g_battery.valid?(g_battery.present?"present":"not present"):"unavailable",g_radio.bluetooth_controller_present?"detected":"none",g_radio.bluetooth_driver_ready?"ready":"no");T(cx+20,y,line,C_SUBTEXT,C_BASE);y+=20;
    T(cx+12,cy+ch-20,"Read-only snapshot. Detection does not imply a complete native driver or hardware acceleration.",C_OVERLAY0,C_BASE);
}
static void draw_eventlog(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w),rows=(ch-52)/15;if(rows<1)rows=1;char line[176];
    int count=w->log_mode?atminit_kernel_log_count():atminit_log_count();int max_scroll=count>rows?count-rows:0;if(w->log_scroll<0)w->log_scroll=0;if(w->log_scroll>max_scroll)w->log_scroll=max_scroll;
    int start=count-rows-w->log_scroll;if(start<0)start=0;
    R(cx,cy,cw2,ch,C_BASE);R(cx,cy,cw2,24,C_MANTLE);T(cx+10,cy+5,"EVENTS",C_TEXT,C_MANTLE);T(cx+82,cy+5,w->log_mode?"Kernel kprintf ring":"Init / service ring",C_LAVENDER,C_MANTLE);
    for(int row=0;row<rows&&start+row<count;row++){int i=start+row,y=cy+32+row*15;
        if(!w->log_mode){const atm_init_log_entry_t *e=atminit_log_at(i);if(!e)continue;ksnprintf(line,sizeof(line),"[%u.%02u] %s  %s  %s",e->tick/100u,e->tick%100u,e->event,e->service,e->detail);}
        else {const atm_kernel_log_entry_t *e=atminit_kernel_log_at(i);if(!e)continue;ksnprintf(line,sizeof(line),"[#%u] %s",e->sequence,e->text);}
        TC(cx+10,y,line,C_SUBTEXT,C_BASE,cw2-20);
    }
    ksnprintf(line,sizeof(line),"Tab switches ring  |  Up/Down scroll  |  %d events  |  volatile bounded memory",count);T(cx+10,cy+ch-18,line,C_OVERLAY0,C_BASE);
}
static int sysinfo_state_selftest(void){sdk_cpuid_t cpu;sdk_meminfo_t mi;sdk_cpuid(&cpu);sdk_meminfo(&mi);return cpu.vendor[0]&&cpu.vendor[12]==0?0:-1;}
static int events_state_selftest(void){return atminit_log_count()>=0&&atminit_kernel_log_count()>=0?0:-1;}

/* Screenshots are recorded as raw binary RGB PPM (P6). This format needs no
 * encoder, streams one row at a time and is intentionally distinct from the
 * decoder-only PNG/JPEG/BMP viewer support. */
static int screenshot_write_full(int fd,const uint8_t *p,uint32_t n){while(n){int64_t wrote=vfs_write(fd,p,n);if(wrote<=0)return -1;p+=(uint32_t)wrote;n-=(uint32_t)wrote;}return 0;}
static void screenshot_rgb_row(const uint8_t *src,uint8_t *dst,uint32_t width,uint32_t bpp){uint32_t step=bpp/8u;for(uint32_t x=0;x<width;x++){uint32_t s=x*step,o=x*3u;dst[o]=src[s+2];dst[o+1]=src[s+1];dst[o+2]=src[s];}}
static int screenshot_state_selftest(void){char header[32],path[80];uint8_t src24[6]={3,2,1,6,5,4},src32[8]={3,2,1,0xaa,6,5,4,0xbb},rgb[6];ksnprintf(header,sizeof(header),"P6\n%u %u\n255\n",2u,1u);ksnprintf(path,sizeof(path),"/data/uiu/screenshots/atmkoala-%02u.ppm",7u);if(kstrcmp(header,"P6\n2 1\n255\n")||kstrcmp(path,"/data/uiu/screenshots/atmkoala-07.ppm"))return -1;screenshot_rgb_row(src24,rgb,2,24);if(rgb[0]!=1||rgb[1]!=2||rgb[2]!=3||rgb[3]!=4||rgb[4]!=5||rgb[5]!=6)return -1;screenshot_rgb_row(src32,rgb,2,32);return rgb[0]==1&&rgb[1]==2&&rgb[2]==3&&rgb[3]==4&&rgb[4]==5&&rgb[5]==6?0:-1;}
static void screenshot_capture(exp_win_t *w){
    if(!catfs_vfs_is_mounted()){kstrcpy(w->screenshot_status,"CatFS /data is not mounted; capture was not saved.");return;}
    if(!vbe.active||!vbe.fb||(vbe.bpp!=24&&vbe.bpp!=32)||!vbe.width||!vbe.height||vbe.width>2048u||vbe.pitch<vbe.width*(vbe.bpp/8u)){kstrcpy(w->screenshot_status,"Framebuffer format is unavailable for RGB PPM capture.");return;}
    (void)vfs_mkdir("/data/uiu",0755);(void)vfs_mkdir("/data/uiu/screenshots",0755);
    char path[96],header[48];int fd=-1,number=0;
    for(int n=1;n<=99;n++){ksnprintf(path,sizeof(path),"/data/uiu/screenshots/atmkoala-%02u.ppm",(uint32_t)n);fd=vfs_open(path,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd>=0){number=n;break;}}
    if(fd<0){kstrcpy(w->screenshot_status,"No free screenshot slot (atmkoala-01..99.ppm).");return;}
    uint32_t row_bytes=vbe.width*3u;uint8_t *row=(uint8_t*)kmalloc(row_bytes);int failed=!row;
    ksnprintf(header,sizeof(header),"P6\n%u %u\n255\n",vbe.width,vbe.height);
    if(!failed&&screenshot_write_full(fd,(const uint8_t*)header,(uint32_t)kstrlen(header))<0)failed=1;
    /* Commit Exp's private backbuffer before reading the VBE-visible surface. */
    vbe_present();
    uint8_t *front=(uint8_t*)vbe.fb;
    for(uint32_t y=0;!failed&&y<vbe.height;y++){uint8_t *src=front+y*vbe.pitch;screenshot_rgb_row(src,row,vbe.width,vbe.bpp);if(screenshot_write_full(fd,row,row_bytes)<0)failed=1;}
    if(row)kfree(row);vfs_close(fd);
    if(failed){(void)vfs_unlink(path);kstrcpy(w->screenshot_status,"Capture failed; incomplete PPM was removed.");return;}
    w->screenshot_count=number;ksnprintf(w->screenshot_status,sizeof(w->screenshot_status),"Saved %s (%u x %u RGB PPM).",path,vbe.width,vbe.height);
}
static void draw_screenshots(exp_win_t *w){int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);char line[160];R(cx,cy,cw2,ch,C_BASE);T(cx+12,cy+12,"SCREENSHOTS",C_LAVENDER,C_BASE);T(cx+12,cy+34,"Capture the current presented framebuffer as a local RGB PPM image.",C_TEXT,C_BASE);T(cx+12,cy+52,"S / Enter or click Capture. Saved only when CatFS is mounted at /data.",C_SUBTEXT,C_BASE);R(cx+14,cy+80,164,32,C_SURFACE1);BOX(cx+14,cy+80,164,32,C_BLUE);T(cx+30,cy+89,"Capture",C_TEXT,C_SURFACE1);ksnprintf(line,sizeof(line),"Framebuffer: %u x %u  %u-bpp",vbe.width,vbe.height,vbe.bpp);T(cx+14,cy+132,line,C_SUBTEXT,C_BASE);T(cx+14,cy+154,"Path: /data/uiu/screenshots/atmkoala-01..99.ppm",C_SUBTEXT,C_BASE);TC(cx+14,cy+ch-42,w->screenshot_status[0]?w->screenshot_status:"No screenshot saved in this window yet.",C_TEXT,C_BASE,cw2-28);T(cx+14,cy+ch-20,"PPM only: no PNG/JPEG encoder, preview gallery or sharing service.",C_OVERLAY0,C_BASE);}
static void screenshots_key(exp_win_t*w,int k){if(k=='s'||k=='S'||k=='\n'||k=='\r')screenshot_capture(w);}
static void eventlog_key(exp_win_t *w,int k){
    if(k=='\t'||k=='m'||k=='M'){w->log_mode=!w->log_mode;w->log_scroll=0;return;}
    if(k==KEY_UP&&w->log_scroll<1024)w->log_scroll++;else if(k==KEY_DOWN&&w->log_scroll>0)w->log_scroll--;else if(k==KEY_PGUP)w->log_scroll+=8;else if(k==KEY_PGDN){w->log_scroll-=8;if(w->log_scroll<0)w->log_scroll=0;}
}

/* ─── Power menu ─────────────────────────────────────────── */
static void exp_power_commit(int action){
    atminit_shutdown();catfs_sync();
    if(action==2){outb(0x64,(uint8_t)0xFE);while(1)cpu_hlt();}
    cpu_cli();while(1)cpu_hlt();
}
static void draw_power_app(exp_win_t*w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);R(cx,cy,cw2,ch,C_BASE);T(cx+14,cy+12,"POWER",C_RED,C_BASE);
    T(cx+14,cy+32,"Choose an action. Halt and restart require an explicit Enter confirmation.",C_SUBTEXT,C_BASE);
    int bx=cx+16,by=cy+62,bw=(cw2-48)/3;const char*lbl[]={"HALT","RESTART","SLEEP"};const char*desc[]={"Stops CPU after CatFS sync","Keyboard-controller reset","Unavailable: ACPI S3 absent"};color32_t col[]={C_RED,C_PEACH,C_OVERLAY0};
    for(int i=0;i<3;i++){int x=bx+i*(bw+8);R(x,by,bw,54,C_SURFACE0);BOX(x,by,bw,54,(w->power_action==i+1)?C_YELLOW:col[i]);T(x+12,by+10,lbl[i],col[i],C_SURFACE0);T(x+12,by+30,desc[i],C_SUBTEXT,C_SURFACE0);}
    if(w->power_action){const char*what=w->power_action==1?"HALT":"RESTART";char line[96];ksnprintf(line,sizeof(line),"Confirm %s: Enter executes, Esc cancels.",what);T(cx+14,cy+ch-42,line,C_YELLOW,C_BASE);}else T(cx+14,cy+ch-42,"H halt  |  R restart  |  S sleep status  |  Esc cancel",C_OVERLAY0,C_BASE);
    T(cx+14,cy+ch-22,"Sleep is deliberately unavailable: no ACPI S3 suspend/resume driver is implemented.",C_OVERLAY0,C_BASE);
}
static void power_key(exp_win_t*w,int k){
    if(k==KEY_ESC){w->power_action=0;return;}
    if(w->power_action){if(k=='\n'||k=='\r')exp_power_commit(w->power_action);return;}
    if(k=='h'||k=='H')w->power_action=1;else if(k=='r'||k=='R')w->power_action=2;else if(k=='s'||k=='S')exp_notify("Sleep unavailable: ACPI S3 is not implemented",C_YELLOW);
}
static int power_state_selftest(void){exp_win_t w;kmemset(&w,0,sizeof(w));w.power_action=1;if(w.power_action!=1)return -1;w.power_action=0;return w.power_action==0?0:-1;}

/* ─── Calculator / Tasks ─────────────────────────────────── */
static const char *calc_p;
static int calc_expr_parse(int *out);
static void calc_ws(void){while(*calc_p==' ')calc_p++;}
static int calc_factor(int *out){
    calc_ws();int sign=1;if(*calc_p=='+'||*calc_p=='-'){if(*calc_p=='-')sign=-1;calc_p++;}
    calc_ws();if(*calc_p=='('){calc_p++;if(calc_expr_parse(out)<0||*calc_p!=')')return -1;calc_p++;*out*=sign;return 0;}
    if(*calc_p<'0'||*calc_p>'9')return -1;int v=0;while(*calc_p>='0'&&*calc_p<='9'){v=v*10+(*calc_p-'0');calc_p++;}*out=v*sign;return 0;
}
static int calc_term_parse(int *out){int v;if(calc_factor(&v)<0)return -1;for(;;){calc_ws();char op=*calc_p;if(op!='*'&&op!='/'&&op!='%')break;calc_p++;int rhs;if(calc_factor(&rhs)<0||(op=='/'||op=='%')&&rhs==0)return -1;if(op=='*')v*=rhs;else if(op=='/')v/=rhs;else v%=rhs;}*out=v;return 0;}
static int calc_expr_parse(int *out){int v;if(calc_term_parse(&v)<0)return -1;for(;;){calc_ws();char op=*calc_p;if(op!='+'&&op!='-')break;calc_p++;int rhs;if(calc_term_parse(&rhs)<0)return -1;if(op=='+')v+=rhs;else v-=rhs;}*out=v;return 0;}
static void calc_eval(exp_win_t *w){calc_p=w->calc_expr;int v;if(calc_expr_parse(&v)<0){w->calc_valid=0;return;}calc_ws();if(*calc_p){w->calc_valid=0;return;}w->calc_result=v;w->calc_valid=1;}
static void draw_calculator(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    R(cx,cy,cw2,ch,C_BASE);
    T(cx+10,cy+10,"CALCULATOR",C_SUBTEXT,C_BASE);
    R(cx+10,cy+34,cw2-20,44,C_CRUST); BOX(cx+10,cy+34,cw2-20,44,C_SURFACE2);
    T(cx+18,cy+42,w->calc_expr[0]?w->calc_expr:"0",C_TEXT,C_CRUST);
    char out[32]; if(w->calc_valid)kitoa(w->calc_result,out,10);else kstrcpy(out,"_");
    T(cx+18,cy+62,out,C_SUBTEXT,C_CRUST);
    static const char *keys[]={"7","8","9","/","4","5","6","*","1","2","3","-","0","C","=","+"};
    int bw=(cw2-44)/4, by=cy+92;
    for(int i=0;i<16;i++){
        int x=cx+10+(i%4)*(bw+8), y=by+(i/4)*34;
        R(x,y,bw,26,C_MANTLE); BOX(x,y,bw,26,C_SURFACE1);
        T(x+bw/2-4,y+5,keys[i],C_TEXT,C_MANTLE);
    }
    T(cx+10,cy+ch-20,"Integer: + - * / % ( ) | precedence | Enter evaluates | C clears",C_SUBTEXT,C_BASE);
}
static void calc_key(exp_win_t *w,int k){
    if(k=='\n'||k=='\r'){calc_eval(w);return;}
    if(k=='\b'||k==127){if(w->calc_len>0)w->calc_expr[--w->calc_len]=0;return;}
    if(k=='c'||k=='C'){w->calc_len=0;w->calc_expr[0]=0;w->calc_valid=0;return;}
    if((k>='0'&&k<='9')||k=='+'||k=='-'||k=='*'||k=='/'||k=='%'||k=='('||k==')'||k==' ')
        if(w->calc_len<(int)sizeof(w->calc_expr)-1){w->calc_expr[w->calc_len++]=(char)k;w->calc_expr[w->calc_len]=0;w->calc_valid=0;}
}
static void tasks_init(exp_win_t *w){
    static const char *const seed[]={"Review system status","Write a note","Back up CatFS data","Check open services","Plan next POSIX step"};
    w->todo_count=5;w->todo_sel=0;w->todo_edit=0;w->todo_input_len=0;w->todo_input[0]=0;kmemset(w->todo_done,0,sizeof(w->todo_done));
    for(int i=0;i<w->todo_count;i++)kstrncpy(w->todo_text[i],seed[i],TASK_TEXT_LEN-1);
}
static void draw_tasks(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);R(cx,cy,cw2,ch,C_BASE);T(cx+10,cy+10,"TASKS",C_SUBTEXT,C_BASE);T(cx+10,cy+30,"Bounded local checklist",C_OVERLAY0,C_BASE);
    int rows=w->todo_count;if(rows>TASK_MAX)rows=TASK_MAX;
    for(int i=0;i<rows;i++){int y=cy+52+i*28;int sel=i==w->todo_sel;color32_t bg=sel?C_SURFACE1:C_MANTLE;R(cx+10,y,cw2-20,24,bg);if(sel)BOX(cx+10,y,cw2-20,24,C_TEXT);BOX(cx+18,y+6,12,12,w->todo_done[i]?C_TEXT:C_SUBTEXT);if(w->todo_done[i])R(cx+21,y+9,6,6,C_TEXT);T(cx+42,y+4,w->todo_text[i],w->todo_done[i]?C_SUBTEXT:C_TEXT,bg);}
    int iy=cy+52+rows*28+6;R(cx+10,iy,cw2-20,26,w->todo_edit?C_SURFACE1:C_MANTLE);if(w->todo_edit)BOX(cx+10,iy,cw2-20,26,C_BLUE);T(cx+18,iy+5,w->todo_edit?(w->todo_input[0]?w->todo_input:"Type task; Enter adds; Esc cancels"):"A: add task  D: remove selected",w->todo_edit?C_TEXT:C_SUBTEXT,w->todo_edit?C_SURFACE1:C_MANTLE);
    T(cx+10,cy+ch-20,"Up/Down select  Space toggles  A add  D remove",C_SUBTEXT,C_BASE);
}
static void tasks_key(exp_win_t *w,int k){
    if(w->todo_edit){if(k==KEY_ESC){w->todo_edit=0;w->todo_input_len=0;w->todo_input[0]=0;}else if((k=='\n'||k=='\r')&&w->todo_input_len>0&&w->todo_count<TASK_MAX){kstrcpy(w->todo_text[w->todo_count],w->todo_input);w->todo_done[w->todo_count]=0;w->todo_sel=w->todo_count++;w->todo_edit=0;w->todo_input_len=0;w->todo_input[0]=0;}else if((k=='\b'||k==127)&&w->todo_input_len>0){w->todo_input[--w->todo_input_len]=0;}else if(k>=0x20&&k<=0x7e&&w->todo_input_len<TASK_TEXT_LEN-1){w->todo_input[w->todo_input_len++]=(char)k;w->todo_input[w->todo_input_len]=0;}return;}
    if((k=='a'||k=='A')&&w->todo_count<TASK_MAX){w->todo_edit=1;w->todo_input_len=0;w->todo_input[0]=0;}
    else if((k=='d'||k=='D')&&w->todo_count>0){for(int i=w->todo_sel;i+1<w->todo_count;i++){kstrcpy(w->todo_text[i],w->todo_text[i+1]);w->todo_done[i]=w->todo_done[i+1];}w->todo_count--;if(w->todo_sel>=w->todo_count&&w->todo_count>0)w->todo_sel=w->todo_count-1;}
    else if(k==KEY_UP&&w->todo_sel>0)w->todo_sel--;
    else if(k==KEY_DOWN&&w->todo_sel<w->todo_count-1)w->todo_sel++;
    else if((k==' '||k=='\n'||k=='\r')&&w->todo_count>0)w->todo_done[w->todo_sel]^=1;
}
static void draw_tinygl_app(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w),angle=(int)(pit_get_ticks()*7);
    const char *name=w->tinygl_scene?"Gears":"Cube";
    T(cx+10,cy+8,"TinyGL-Lite Software Scene",C_LAVENDER,C_BASE);
    TF(cx+10,cy+24,C_SUBTEXT,C_BASE,"%s | C cube | G gears | no GLX / Mesa API",name);
    int vh=ch-52;if(vh<64)vh=64;
    if(w->tinygl_scene)tgl_draw_gears(&tinygl_ctx,cx+10,cy+42,cw2-20,vh,angle);
    else tgl_draw_cube(&tinygl_ctx,cx+10,cy+42,cw2-20,vh,angle);
    T(cx+12,cy+46,w->tinygl_scene?"software gears scene":"software rotating cube",C_TEXT,RGB(0x10,0x10,0x10));
}
static void tinygl_key(exp_win_t *w,int k){if(k=='c'||k=='C'){w->tinygl_scene=0;kstrcpy(w->title,"TinyGL Cube (software)");}else if(k=='g'||k=='G'){w->tinygl_scene=1;kstrcpy(w->title,"TinyGL Gears (software)");}}
/* ─── Native GUI games ───────────────────────────────────── */
static uint32_t gui_rng_state=0x6D2B79F5u;
static uint32_t gui_rand(void){gui_rng_state^=gui_rng_state<<13;gui_rng_state^=gui_rng_state>>17;gui_rng_state^=gui_rng_state<<5;return gui_rng_state;}
static int mines_count(exp_mines_t *m,int x,int y){int n=0;for(int yy=y-1;yy<=y+1;yy++)for(int xx=x-1;xx<=x+1;xx++)if(xx>=0&&yy>=0&&xx<10&&yy<10&&m->mine[yy][xx])n++;return n;}
static void mines_reveal(exp_mines_t *m,int x,int y){if(x<0||y<0||x>=10||y>=10||m->open[y][x]||m->flag[y][x]||m->state)return;if(m->mine[y][x]){m->state=2;return;}m->open[y][x]=1;m->opened++;if(mines_count(m,x,y)==0)for(int yy=y-1;yy<=y+1;yy++)for(int xx=x-1;xx<=x+1;xx++)if(xx!=x||yy!=y)mines_reveal(m,xx,yy);if(m->opened>=100-m->mines)m->state=1;}
static void mines_init(exp_win_t *w){exp_mines_t*m=&w->mines;kmemset(m,0,sizeof(*m));m->mines=14;m->cursor_x=0;m->cursor_y=0;gui_rng_state^=pit_get_ticks()+0x9E3779B9u;int placed=0;while(placed<m->mines){int x=(int)(gui_rand()%10u),y=(int)(gui_rand()%10u);if(!m->mine[y][x]&&(x>1||y>1)){m->mine[y][x]=1;placed++;}}}
static void draw_mines_app(exp_win_t *w){exp_mines_t*m=&w->mines;int cx=CX(w),cy=CY(w),cell=24,bx=cx+16,by=cy+48;R(cx,cy,CW(w),CH(w),C_BASE);T(cx+14,cy+10,"MINESWEEPER",C_LAVENDER,C_BASE);char st[64];ksnprintf(st,sizeof(st),"14 mines  |  opened %d/86  |  arrows + Enter reveal, F flag",m->opened);T(cx+14,cy+26,st,C_SUBTEXT,C_BASE);
 for(int y=0;y<10;y++)for(int x=0;x<10;x++){int px=bx+x*cell,py=by+y*cell;int sel=x==m->cursor_x&&y==m->cursor_y;color32_t bg=m->open[y][x]?C_SURFACE0:C_SURFACE1;R(px,py,cell-2,cell-2,bg);BOX(px,py,cell-2,cell-2,sel?C_TEXT:C_SURFACE2);if(m->open[y][x]){if(m->mine[y][x])T(px+7,py+4,"*",C_RED,bg);else{int c=mines_count(m,x,y);if(c){char ns[2]={(char)('0'+c),0};T(px+7,py+4,ns,C_TEXT,bg);}}}else if(m->flag[y][x])T(px+7,py+4,"F",C_PEACH,bg);}
 const char*msg=m->state==1?"Board cleared — press R for a new game":(m->state==2?"Mine hit — press R for a new game":"Click a cell or use keyboard controls");T(cx+14,by+10*cell+10,msg,m->state?C_YELLOW:C_SUBTEXT,C_BASE);}
static void mines_key(exp_win_t*w,int k){exp_mines_t*m=&w->mines;if(k=='r'||k=='R'){mines_init(w);return;}if(k==KEY_LEFT&&m->cursor_x>0)m->cursor_x--;else if(k==KEY_RIGHT&&m->cursor_x<9)m->cursor_x++;else if(k==KEY_UP&&m->cursor_y>0)m->cursor_y--;else if(k==KEY_DOWN&&m->cursor_y<9)m->cursor_y++;else if(k=='f'||k=='F')m->flag[m->cursor_y][m->cursor_x]^=1;else if(k==' '||k=='\n'||k=='\r')mines_reveal(m,m->cursor_x,m->cursor_y);}
static void snake_init(exp_win_t*w){exp_snake_t*s=&w->snake;kmemset(s,0,sizeof(*s));s->length=4;s->dx=1;s->dy=0;s->body[0].x=9;s->body[0].y=7;for(int i=1;i<s->length;i++){s->body[i].x=9-i;s->body[i].y=7;}s->food.x=15;s->food.y=7;s->last_tick=pit_get_ticks();}
static int snake_has(exp_snake_t*s,int x,int y){for(int i=0;i<s->length;i++)if(s->body[i].x==x&&s->body[i].y==y)return 1;return 0;}
static void snake_food(exp_snake_t*s){for(int n=0;n<256;n++){int x=(int)(gui_rand()%20u),y=(int)(gui_rand()%14u);if(!snake_has(s,x,y)){s->food.x=x;s->food.y=y;return;}}}
/* A frame can be delayed by software VBE work on physical hardware.  Advance
 * a bounded number of fixed-tick steps rather than resetting the deadline,
 * otherwise Snake visually appears to stop whenever Exp misses a frame. */
static void snake_tick(exp_win_t*w){
    exp_snake_t*s=&w->snake;uint32_t now=pit_get_ticks();
    if(s->state||now-s->last_tick<12)return;
    uint32_t steps=(now-s->last_tick)/12;if(steps>4)steps=4;
    s->last_tick+=steps*12;
    while(steps--&&!s->state){
        int nx=s->body[0].x+s->dx,ny=s->body[0].y+s->dy;
        int eat=nx==s->food.x&&ny==s->food.y;
        int tail=(s->length>0&&s->body[s->length-1].x==nx&&s->body[s->length-1].y==ny);
        if(nx<0||ny<0||nx>=20||ny>=14||(snake_has(s,nx,ny)&&(eat||!tail))){s->state=1;break;}
        int grow=eat&&s->length<96;if(grow)s->length++;
        for(int i=s->length-1;i>0;i--)s->body[i]=s->body[i-1];
        s->body[0].x=nx;s->body[0].y=ny;
        if(eat){s->score+=10;snake_food(s);}
    }
}
static void draw_snake_app(exp_win_t*w){exp_snake_t*s=&w->snake;int cx=CX(w),cy=CY(w),cell=16,bx=cx+14,by=cy+44;R(cx,cy,CW(w),CH(w),C_BASE);T(cx+14,cy+10,"SNAKE",C_GREEN,C_BASE);char score[64];ksnprintf(score,sizeof(score),"Score %d  | arrows steer  | R restart",s->score);T(cx+14,cy+26,score,C_SUBTEXT,C_BASE);R(bx-2,by-2,20*cell+4,14*cell+4,C_CRUST);BOX(bx-2,by-2,20*cell+4,14*cell+4,C_SURFACE2);R(bx+s->food.x*cell+3,by+s->food.y*cell+3,cell-6,cell-6,C_PEACH);for(int i=s->length-1;i>=0;i--){color32_t c=i?C_GREEN:C_TEXT;R(bx+s->body[i].x*cell+2,by+s->body[i].y*cell+2,cell-4,cell-4,c);}if(s->state)T(bx+86,by+104,"GAME OVER",C_YELLOW,C_CRUST);int cb=by+14*cell+12;static const char*ctl[]={"Left","Up","Down","Right"};for(int i=0;i<4;i++){int bw=i==3?74:54,bx2=cx+14+(i==0?0:(i==1?58:(i==2?116:174)));R(bx2,cb,bw,24,C_MANTLE);BOX(bx2,cb,bw,24,C_SURFACE2);T(bx2+6,cb+5,ctl[i],C_SUBTEXT,C_MANTLE);}}
static void snake_key(exp_win_t*w,int k){exp_snake_t*s=&w->snake;if(k=='r'||k=='R'){snake_init(w);return;}if(k==KEY_LEFT&&s->dx!=1){s->dx=-1;s->dy=0;}else if(k==KEY_RIGHT&&s->dx!=-1){s->dx=1;s->dy=0;}else if(k==KEY_UP&&s->dy!=1){s->dx=0;s->dy=-1;}else if(k==KEY_DOWN&&s->dy!=-1){s->dx=0;s->dy=1;}}

#define FLAPPY_COLS 24
#define FLAPPY_ROWS 14
#define FLAPPY_GAP_ROWS 5
static int flappy_collides(const exp_flappy_t *f){
    int bird_row=f->bird_y/4;
    if(bird_row<0||bird_row>=FLAPPY_ROWS)return 1;
    if(3>=f->pipe_x&&3<f->pipe_x+2&&
       (bird_row<f->gap_y||bird_row>=f->gap_y+FLAPPY_GAP_ROWS))return 1;
    return 0;
}
static int flappy_state_selftest(void){
    exp_flappy_t f;kmemset(&f,0,sizeof(f));f.bird_y=7*4;f.pipe_x=3;f.gap_y=5;
    if(flappy_collides(&f))return -1;f.bird_y=1*4;
    if(!flappy_collides(&f))return -1;f.bird_y=7*4;f.pipe_x=24;
    return flappy_collides(&f)?-1:0;
}
static void flappy_init(exp_win_t *w){
    exp_flappy_t *f=&w->flappy;kmemset(f,0,sizeof(*f));
    f->bird_y=7*4;f->pipe_x=FLAPPY_COLS;f->gap_y=4;f->last_tick=pit_get_ticks();
}
static void flappy_tick(exp_win_t *w){
    exp_flappy_t *f=&w->flappy;uint32_t now=pit_get_ticks();
    if(f->state||now-f->last_tick<5)return;
    uint32_t steps=(now-f->last_tick)/5;if(steps>4)steps=4;f->last_tick+=steps*5;
    while(steps--&&!f->state){
        f->velocity+=1;if(f->velocity>4)f->velocity=4;f->bird_y+=f->velocity;
        f->pipe_x--;if(f->pipe_x==3)f->score++;
        if(f->pipe_x<-2){f->pipe_x=FLAPPY_COLS;f->gap_y=2+(int)(gui_rand()%6u);}
        if(flappy_collides(f))f->state=1;
    }
}
static void draw_flappy_app(exp_win_t *w){
    exp_flappy_t *f=&w->flappy;int cx=CX(w),cy=CY(w),cell=14,bx=cx+14,by=cy+48;
    R(cx,cy,CW(w),CH(w),C_BASE);T(cx+14,cy+10,"FLAPPY BIRD",C_YELLOW,C_BASE);
    char info[96];ksnprintf(info,sizeof(info),"Score %d  |  Space/Up/click flap  |  R restart",f->score);T(cx+14,cy+26,info,C_SUBTEXT,C_BASE);
    R(bx-2,by-2,FLAPPY_COLS*cell+4,FLAPPY_ROWS*cell+4,C_CRUST);BOX(bx-2,by-2,FLAPPY_COLS*cell+4,FLAPPY_ROWS*cell+4,C_SURFACE2);
    for(int x=f->pipe_x;x<f->pipe_x+2;x++)if(x>=0&&x<FLAPPY_COLS)for(int y=0;y<FLAPPY_ROWS;y++)if(y<f->gap_y||y>=f->gap_y+FLAPPY_GAP_ROWS){int px=bx+x*cell,py=by+y*cell;R(px+1,py+1,cell-2,cell-2,C_GREEN);}
    int bird_row=f->bird_y/4;if(bird_row>=0&&bird_row<FLAPPY_ROWS){int px=bx+3*cell,py=by+bird_row*cell;R(px+2,py+3,cell-4,cell-6,C_YELLOW);R(px+cell-5,py+5,2,2,C_CRUST);}
    if(f->state)T(bx+102,by+88,"GAME OVER",C_RED,C_CRUST);
    T(cx+14,by+FLAPPY_ROWS*cell+11,f->state?"Press R, Space or click to restart.":"Keep the bird inside the gap.",C_OVERLAY0,C_BASE);
}
static void flappy_key(exp_win_t *w,int k){
    exp_flappy_t *f=&w->flappy;
    if(k=='r'||k=='R'||(f->state&&(k==' '||k=='\n'||k=='\r'))){flappy_init(w);return;}
    if(!f->state&&(k==' '||k=='\n'||k=='\r'||k==KEY_UP))f->velocity=-4;
}

static color32_t paint_color(int c);
/* Fixed 4x4 tetromino cells, low bit is top-left. The sequence is local and
 * deterministic after window creation; this is a bounded arcade game, not a
 * networked/randomized competitive implementation. */
static const uint16_t tet_shape[7][4]={
    {0x000F,0x2222,0x000F,0x2222}, {0x0066,0x0066,0x0066,0x0066},
    {0x0027,0x0262,0x0072,0x0232}, {0x0036,0x0231,0x0036,0x0231},
    {0x0063,0x0132,0x0063,0x0132}, {0x0071,0x0223,0x0047,0x0622},
    {0x0074,0x0226,0x0017,0x0322}
};
static int tet_cell(int piece,int rot,int x,int y){return (tet_shape[piece%7][rot&3]&(1u<<(y*4+x)))!=0;}
static uint32_t tet_rand(exp_tetris_t*t){t->seed=t->seed*1664525u+1013904223u;return t->seed;}
static int tet_collides(const exp_tetris_t*t,int piece,int rot,int ox,int oy){
    for(int y=0;y<4;y++)for(int x=0;x<4;x++)if(tet_cell(piece,rot,x,y)){int bx=ox+x,by=oy+y;if(bx<0||bx>=EXP_TETRIS_W||by>=EXP_TETRIS_H)return 1;if(by>=0&&t->board[by][bx])return 1;}return 0;
}
static void tet_spawn(exp_tetris_t*t){t->piece=t->next;t->next=(int)(tet_rand(t)%7u);t->rotation=0;t->x=3;t->y=0;if(tet_collides(t,t->piece,t->rotation,t->x,t->y))t->state=1;}
static void tet_clear_lines(exp_tetris_t*t){for(int y=EXP_TETRIS_H-1;y>=0;y--){int full=1;for(int x=0;x<EXP_TETRIS_W;x++)if(!t->board[y][x]){full=0;break;}if(full){for(int yy=y;yy>0;yy--)for(int x=0;x<EXP_TETRIS_W;x++)t->board[yy][x]=t->board[yy-1][x];for(int x=0;x<EXP_TETRIS_W;x++)t->board[0][x]=0;t->lines++;t->score+=100;y++;}}}
static void tet_lock(exp_tetris_t*t){for(int y=0;y<4;y++)for(int x=0;x<4;x++)if(tet_cell(t->piece,t->rotation,x,y)){int bx=t->x+x,by=t->y+y;if(by>=0&&by<EXP_TETRIS_H&&bx>=0&&bx<EXP_TETRIS_W)t->board[by][bx]=(uint8_t)(t->piece+1);}tet_clear_lines(t);tet_spawn(t);}
static void tetris_init(exp_win_t*w){exp_tetris_t*t=&w->tetris;kmemset(t,0,sizeof(*t));t->seed=0xA7F05EEDu^(uint32_t)w->id;t->next=(int)(tet_rand(t)%7u);t->last_tick=pit_get_ticks();tet_spawn(t);}
static void tetris_try_move(exp_tetris_t*t,int dx,int dy){if(!t->state&&!tet_collides(t,t->piece,t->rotation,t->x+dx,t->y+dy)){t->x+=dx;t->y+=dy;}}
static void tetris_rotate(exp_tetris_t*t){int r=(t->rotation+1)&3;if(!t->state&&!tet_collides(t,t->piece,r,t->x,t->y))t->rotation=r;}
static void tetris_tick(exp_win_t*w){exp_tetris_t*t=&w->tetris;uint32_t now=pit_get_ticks();if(t->state||now-t->last_tick<40u)return;uint32_t steps=(now-t->last_tick)/40u;if(steps>3)steps=3;t->last_tick+=steps*40u;while(steps--&&!t->state){if(tet_collides(t,t->piece,t->rotation,t->x,t->y+1))tet_lock(t);else t->y++;}}
static void draw_tetris_app(exp_win_t*w){
    exp_tetris_t*t=&w->tetris;int cx=CX(w),cy=CY(w),cell=13,bx=cx+14,by=cy+44;R(cx,cy,CW(w),CH(w),C_BASE);T(cx+14,cy+10,"TETRIS",C_MAUVE,C_BASE);char line[96];ksnprintf(line,sizeof(line),"Score %d  |  Lines %d  |  arrows move/rotate  Space drop  R restart",t->score,t->lines);T(cx+14,cy+26,line,C_SUBTEXT,C_BASE);
    R(bx-2,by-2,EXP_TETRIS_W*cell+4,EXP_TETRIS_H*cell+4,C_CRUST);BOX(bx-2,by-2,EXP_TETRIS_W*cell+4,EXP_TETRIS_H*cell+4,C_SURFACE2);
    for(int y=0;y<EXP_TETRIS_H;y++)for(int x=0;x<EXP_TETRIS_W;x++){uint8_t v=t->board[y][x];color32_t c=v?paint_color(v):C_SURFACE0;R(bx+x*cell+1,by+y*cell+1,cell-2,cell-2,c);}
    for(int y=0;y<4;y++)for(int x=0;x<4;x++)if(tet_cell(t->piece,t->rotation,x,y)){int px=t->x+x,py=t->y+y;if(px>=0&&px<EXP_TETRIS_W&&py>=0&&py<EXP_TETRIS_H)R(bx+px*cell+1,by+py*cell+1,cell-2,cell-2,paint_color(t->piece+1));}
    int nx=bx+EXP_TETRIS_W*cell+24;T(nx,by,"NEXT",C_SUBTEXT,C_BASE);for(int y=0;y<4;y++)for(int x=0;x<4;x++)if(tet_cell(t->next,0,x,y))R(nx+x*cell,by+20+y*cell,cell-2,cell-2,paint_color(t->next+1));
    if(t->state)T(bx+28,by+118,"GAME OVER",C_RED,C_CRUST);T(nx,by+104,"Down: soft drop",C_SUBTEXT,C_BASE);T(nx,by+122,"Up: rotate",C_SUBTEXT,C_BASE);T(nx,by+140,"R: new game",C_SUBTEXT,C_BASE);
}
static void tetris_key(exp_win_t*w,int k){exp_tetris_t*t=&w->tetris;if(k=='r'||k=='R'){tetris_init(w);return;}if(t->state)return;if(k==KEY_LEFT)tetris_try_move(t,-1,0);else if(k==KEY_RIGHT)tetris_try_move(t,1,0);else if(k==KEY_DOWN){if(tet_collides(t,t->piece,t->rotation,t->x,t->y+1))tet_lock(t);else t->y++;}else if(k==KEY_UP)tetris_rotate(t);else if(k==' '||k=='\n'||k=='\r'){while(!tet_collides(t,t->piece,t->rotation,t->x,t->y+1))t->y++;tet_lock(t);}}
static int tetris_state_selftest(void){exp_tetris_t t;kmemset(&t,0,sizeof(t));t.seed=1;t.next=0;tet_spawn(&t);if(t.state||tet_collides(&t,t.piece,t.rotation,t.x,t.y))return -1;for(int x=0;x<EXP_TETRIS_W;x++)t.board[EXP_TETRIS_H-1][x]=1;tet_clear_lines(&t);return t.lines==1&&t.score==100&&t.board[EXP_TETRIS_H-1][0]==0?0:-1;}

static color32_t paint_color(int c){switch(c&7){case 0:return C_BASE;case 1:return C_TEXT;case 2:return C_RED;case 3:return C_PEACH;case 4:return C_YELLOW;case 5:return C_GREEN;case 6:return C_TEAL;default:return C_MAUVE;}}
static uint16_t paint_fill_queue[EXP_PAINT_W*EXP_PAINT_H];
static void paint_init(exp_win_t*w){kmemset(&w->paint,0,sizeof(w->paint));w->paint.color=1;}
static void paint_snapshot(exp_paint_t*p){kmemcpy(p->undo,p->pixels,sizeof(p->pixels));p->undo_valid=1;}
static void paint_put(exp_paint_t*p,int x,int y){if(x>=0&&y>=0&&x<EXP_PAINT_W&&y<EXP_PAINT_H)p->pixels[y][x]=(uint8_t)p->color;}
static void paint_dot(exp_paint_t*p,int x,int y){if(x<0||y<0||x>=EXP_PAINT_W||y>=EXP_PAINT_H||p->pixels[y][x]==(uint8_t)p->color)return;paint_snapshot(p);paint_put(p,x,y);}
static void paint_mark(exp_paint_t*p){p->anchor_x=p->cursor_x;p->anchor_y=p->cursor_y;p->anchor_valid=1;}
static void paint_line(exp_paint_t*p){if(!p->anchor_valid)return;int x0=p->anchor_x,y0=p->anchor_y,x1=p->cursor_x,y1=p->cursor_y;if(x0==x1&&y0==y1)return;int dx=x1-x0,dy=y1-y0,sx=dx<0?-1:1,sy=dy<0?-1:1,err=(dx<0?-dx:dx)-(dy<0?-dy:dy);paint_snapshot(p);for(;;){paint_put(p,x0,y0);if(x0==x1&&y0==y1)break;int e2=err*2;if(e2>-(dy<0?-dy:dy)){err-=(dy<0?-dy:dy);x0+=sx;}if(e2<(dx<0?-dx:dx)){err+=(dx<0?-dx:dx);y0+=sy;}}paint_mark(p);}
static void paint_box_outline(exp_paint_t*p){if(!p->anchor_valid)return;int x0=p->anchor_x,y0=p->anchor_y,x1=p->cursor_x,y1=p->cursor_y;if(x0>x1){int q=x0;x0=x1;x1=q;}if(y0>y1){int q=y0;y0=y1;y1=q;}if(x0==x1&&y0==y1)return;paint_snapshot(p);for(int x=x0;x<=x1;x++){paint_put(p,x,y0);paint_put(p,x,y1);}for(int y=y0;y<=y1;y++){paint_put(p,x0,y);paint_put(p,x1,y);}paint_mark(p);}
static void paint_fill(exp_paint_t*p){
    int start=p->cursor_y*EXP_PAINT_W+p->cursor_x,head=0,tail=0;uint8_t old=p->pixels[p->cursor_y][p->cursor_x],next=(uint8_t)p->color;
    if(old==next)return;paint_snapshot(p);paint_fill_queue[tail++]=(uint16_t)start;p->pixels[p->cursor_y][p->cursor_x]=next;
    while(head<tail){int pos=paint_fill_queue[head++],x=pos%EXP_PAINT_W,y=pos/EXP_PAINT_W;int n[4]={pos-1,pos+1,pos-EXP_PAINT_W,pos+EXP_PAINT_W};
        for(int i=0;i<4;i++){int q=n[i],qx=q%EXP_PAINT_W,qy=q/EXP_PAINT_W;if((i==0&&x==0)||(i==1&&x+1==EXP_PAINT_W)||(i==2&&y==0)||(i==3&&y+1==EXP_PAINT_H))continue;if(p->pixels[qy][qx]==old){p->pixels[qy][qx]=next;if(tail<EXP_PAINT_W*EXP_PAINT_H)paint_fill_queue[tail++]=(uint16_t)q;}}
    }
}
static void paint_clear(exp_paint_t*p){int any=0;for(int y=0;y<EXP_PAINT_H;y++)for(int x=0;x<EXP_PAINT_W;x++)if(p->pixels[y][x])any=1;if(any){paint_snapshot(p);kmemset(p->pixels,0,sizeof(p->pixels));}}
static void paint_ppm_rgb(uint8_t index,uint8_t out[3]){color32_t c=paint_color(index);out[0]=(uint8_t)(c>>16);out[1]=(uint8_t)(c>>8);out[2]=(uint8_t)c;}
static void paint_save_ppm(exp_win_t*w){
    exp_paint_t*p=&w->paint;char path[96],header[48];uint8_t row[EXP_PAINT_W*3];int fd=-1,number=0;
    if(!catfs_vfs_is_mounted()){kstrcpy(w->paint_status,"Save needs mounted CatFS /data.");return;}(void)vfs_mkdir("/data/uiu",0755);(void)vfs_mkdir("/data/uiu/paint",0755);
    for(int n=1;n<=99;n++){ksnprintf(path,sizeof(path),"/data/uiu/paint/paint-%02u.ppm",(uint32_t)n);fd=vfs_open(path,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd>=0){number=n;break;}}
    if(fd<0){kstrcpy(w->paint_status,"No free paint-01..99 PPM slot.");return;}ksnprintf(header,sizeof(header),"P6\n%u %u\n255\n",EXP_PAINT_W,EXP_PAINT_H);int failed=vfs_write(fd,header,kstrlen(header))!=(int64_t)kstrlen(header);
    for(int y=0;y<EXP_PAINT_H&&!failed;y++){for(int x=0;x<EXP_PAINT_W;x++)paint_ppm_rgb(p->pixels[y][x],row+x*3);if(vfs_write(fd,row,sizeof(row))!=(int64_t)sizeof(row))failed=1;}vfs_close(fd);
    if(failed){(void)vfs_unlink(path);kstrcpy(w->paint_status,"Save failed; incomplete PPM removed.");return;}w->paint_save_count=number;ksnprintf(w->paint_status,sizeof(w->paint_status),"Saved %s (%ux%u RGB PPM).",path,EXP_PAINT_W,EXP_PAINT_H);
}
static void paint_undo(exp_paint_t*p){if(!p->undo_valid)return;kmemcpy(p->pixels,p->undo,sizeof(p->pixels));p->undo_valid=0;}
static void draw_paint_app(exp_win_t*w){
    exp_paint_t*p=&w->paint;int cx=CX(w),cy=CY(w),cell=12,bx=cx+14,by=cy+60;
    R(cx,cy,CW(w),CH(w),C_BASE);T(cx+14,cy+10,"PAINT",C_LAVENDER,C_BASE);
    T(cx+14,cy+27,"Arrows/Space dot | M mark | L line | R box | F fill | U undo | S save | 1-8 color",C_SUBTEXT,C_BASE);
    for(int i=0;i<8;i++){int px=bx+i*27;R(px,cy+40,20,14,paint_color(i));BOX(px,cy+40,20,14,i==p->color?C_TEXT:C_SURFACE2);char n[2]={(char)('1'+i),0};T(px+6,cy+42,n,i==0?C_TEXT:C_BASE,paint_color(i));}
    R(bx-2,by-2,EXP_PAINT_W*cell+4,EXP_PAINT_H*cell+4,C_CRUST);BOX(bx-2,by-2,EXP_PAINT_W*cell+4,EXP_PAINT_H*cell+4,C_SURFACE2);
    for(int y=0;y<EXP_PAINT_H;y++)for(int x=0;x<EXP_PAINT_W;x++){int px=bx+x*cell,py=by+y*cell;R(px,py,cell-1,cell-1,paint_color(p->pixels[y][x]));if(p->anchor_valid&&x==p->anchor_x&&y==p->anchor_y)BOX(px+2,py+2,cell-5,cell-5,C_MAUVE);if(x==p->cursor_x&&y==p->cursor_y)BOX(px,py,cell-1,cell-1,C_YELLOW);}
    if(p->anchor_valid)TF(cx+14,by+EXP_PAINT_H*cell+10,C_OVERLAY0,C_BASE,"Mark %d,%d  |  %s  |  PPM only [S]",p->anchor_x,p->anchor_y,p->undo_valid?"one undo":"no undo");
    else TF(cx+14,by+EXP_PAINT_H*cell+10,C_OVERLAY0,C_BASE,"No mark  |  %s  |  PPM only [S]",p->undo_valid?"one undo":"no undo");
    if(w->paint_status[0])TC(cx+14,by+EXP_PAINT_H*cell+26,w->paint_status,C_SUBTEXT,C_BASE,CW(w)-28);
}
static void paint_key(exp_win_t*w,int k){exp_paint_t*p=&w->paint;if(k==KEY_LEFT&&p->cursor_x>0)p->cursor_x--;else if(k==KEY_RIGHT&&p->cursor_x+1<EXP_PAINT_W)p->cursor_x++;else if(k==KEY_UP&&p->cursor_y>0)p->cursor_y--;else if(k==KEY_DOWN&&p->cursor_y+1<EXP_PAINT_H)p->cursor_y++;else if(k==' '||k=='\n'||k=='\r')paint_dot(p,p->cursor_x,p->cursor_y);else if(k>='1'&&k<='8')p->color=k-'1';else if(k=='b'||k=='B')p->color=0;else if(k=='m'||k=='M')paint_mark(p);else if(k=='l'||k=='L')paint_line(p);else if(k=='r'||k=='R')paint_box_outline(p);else if(k=='f'||k=='F')paint_fill(p);else if(k=='u'||k=='U')paint_undo(p);else if(k=='s'||k=='S')paint_save_ppm(w);else if(k=='c'||k=='C')paint_clear(p);}
static int paint_state_selftest(void){uint8_t rgb[3];paint_ppm_rgb(1,rgb);if(rgb[0]!=(uint8_t)(C_TEXT>>16)||rgb[1]!=(uint8_t)(C_TEXT>>8)||rgb[2]!=(uint8_t)C_TEXT)return -1;exp_paint_t p;kmemset(&p,0,sizeof(p));p.color=2;paint_dot(&p,1,1);if(p.pixels[1][1]!=2||!p.undo_valid)return -1;paint_undo(&p);if(p.pixels[1][1]!=0)return -1;p.cursor_x=1;p.cursor_y=1;paint_mark(&p);p.cursor_x=4;p.cursor_y=3;paint_line(&p);if(p.pixels[1][1]!=2||p.pixels[3][4]!=2)return -1;paint_undo(&p);if(p.pixels[1][1]!=0)return -1;p.cursor_x=1;p.cursor_y=1;paint_mark(&p);p.cursor_x=4;p.cursor_y=3;paint_box_outline(&p);if(p.pixels[1][1]!=2||p.pixels[3][4]!=2||p.pixels[2][2]!=0)return -1;paint_undo(&p);p.color=3;paint_fill(&p);if(p.pixels[0][0]!=3||p.pixels[EXP_PAINT_H-1][EXP_PAINT_W-1]!=3)return -1;paint_undo(&p);return p.pixels[0][0]==0?0:-1;}

static int calendar_leap(int y){return (y%4==0&&y%100!=0)||y%400==0;}
static int calendar_days(int y,int m){static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31};return m==2?d[1]+calendar_leap(y):d[m-1];}
static int calendar_weekday(int y,int m,int d){static const uint8_t t[]={0,3,2,5,0,3,5,1,4,6,2,4};if(m<3)y--;return (y+y/4-y/100+y/400+t[m-1]+d)%7;}
static int calendar_rtc(rtc_datetime_t *t){const char *basis=sysconf_get("system","rtc_basis"),*zone=sysconf_get("system","timezone");if(basis&&!kstrcmp(basis,"local"))return rtc_read_datetime(t)==0;return atm_local_datetime(zone&&zone[0]?zone:"UTC",t,NULL,NULL)==0;}
/* Calendar data has day-level granularity. Probe the validated civil-time source
 * once per second, and request a full redraw only after a real local-day change. */
static int calendar_refresh(exp_win_t *w){
    rtc_datetime_t t;uint32_t now=pit_get_ticks();
    if(now-w->cal_last_probe_tick<100u)return 0;w->cal_last_probe_tick=now;
    if(!calendar_rtc(&t))return 0;
    int key=t.year*10000+t.month*100+t.day;
    if(key==w->cal_last_date_key)return 0;
    w->cal_last_date_key=key;
    if(w->cal_follow_today){w->cal_year=t.year;w->cal_month=t.month;}
    return 1;
}
static void draw_calendar_app(exp_win_t *w){
    static const char *const mon[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    static const char *const dow[]={"Su","Mo","Tu","We","Th","Fr","Sa"};
    rtc_datetime_t today;int have_today=calendar_rtc(&today);
    if(w->cal_year<1){w->cal_follow_today=1;if(have_today){w->cal_year=today.year;w->cal_month=today.month;}else{w->cal_year=2026;w->cal_month=1;}}
    if(w->cal_follow_today&&have_today){w->cal_year=today.year;w->cal_month=today.month;}
    if(w->cal_month<1||w->cal_month>12)w->cal_month=1;
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);R(cx,cy,cw2,ch,C_BASE);
    char title[64];ksnprintf(title,sizeof(title),"%s %d",mon[w->cal_month-1],w->cal_year);T(cx+14,cy+12,title,C_LAVENDER,C_BASE);
    char info[96];if(have_today)ksnprintf(info,sizeof(info),"Today: %04d-%02d-%02d | Left/Right month | Up/Down year | T today",today.year,today.month,today.day);else kstrcpy(info,"RTC date unavailable | Left/Right month | Up/Down year");T(cx+14,cy+28,info,C_SUBTEXT,C_BASE);
    int gx=cx+14,gy=cy+54,gw=cw2-28,cellw=gw/7,cellh=25;
    for(int i=0;i<7;i++)T(gx+i*cellw+6,gy,dow[i],i==0||i==6?C_PEACH:C_SUBTEXT,C_BASE);
    int first=calendar_weekday(w->cal_year,w->cal_month,1),days=calendar_days(w->cal_year,w->cal_month);
    for(int d=1;d<=days;d++){int slot=first+d-1,row=slot/7,col=slot%7,px=gx+col*cellw,py=gy+18+row*cellh;int is_today=have_today&&w->cal_year==today.year&&w->cal_month==today.month&&d==today.day;color32_t bg=is_today?C_GREEN:((col==0||col==6)?C_MANTLE:C_SURFACE0);R(px,py,cellw-2,cellh-2,bg);if(is_today)BOX(px,py,cellw-2,cellh-2,C_TEXT);char n[4];kitoa(d,n,10);T(px+7,py+4,n,is_today?C_BASE:(col==0||col==6?C_PEACH:C_TEXT),bg);}
    T(cx+14,gy+18+6*cellh+8,have_today?(w->cal_follow_today?"Following selected local date; month changes automatically at midnight.":"Manual month view; press T to return to today."):"No valid local RTC date; calendar remains manual.",C_OVERLAY0,C_BASE);
}
static void calendar_key(exp_win_t *w,int k){rtc_datetime_t t;if(k=='t'||k=='T'){if(calendar_rtc(&t)){w->cal_follow_today=1;w->cal_year=t.year;w->cal_month=t.month;}return;}if(k==KEY_LEFT){w->cal_follow_today=0;if(--w->cal_month<1){w->cal_month=12;w->cal_year--;}}else if(k==KEY_RIGHT){w->cal_follow_today=0;if(++w->cal_month>12){w->cal_month=1;w->cal_year++;}}else if(k==KEY_UP){w->cal_follow_today=0;w->cal_year++;}else if(k==KEY_DOWN&&w->cal_year>1){w->cal_follow_today=0;w->cal_year--;}}

static uint32_t stopwatch_ticks(const exp_win_t *w){return w->stopwatch_elapsed_ticks+(w->stopwatch_running?(sched_uptime_ticks()-w->stopwatch_started_tick):0);}
static void stopwatch_toggle(exp_win_t *w){if(w->stopwatch_running){w->stopwatch_elapsed_ticks=stopwatch_ticks(w);w->stopwatch_running=0;}else{w->stopwatch_started_tick=sched_uptime_ticks();w->stopwatch_running=1;}}
static void stopwatch_reset(exp_win_t *w){w->stopwatch_elapsed_ticks=0;w->stopwatch_started_tick=sched_uptime_ticks();}
static int clock_zone_index(const char *zone){uint32_t n=atm_timezone_count();for(uint32_t i=0;i<n;i++)if(!kstrcmp(zone&&zone[0]?zone:"UTC",atm_timezone_name(i)))return (int)i;return 0;}
static const char *clock_browse_zone(exp_win_t *w){uint32_t n=atm_timezone_count();if(!n)return "UTC";if(w->clock_zone_index<0||w->clock_zone_index>=(int)n)w->clock_zone_index=0;return atm_timezone_name((uint32_t)w->clock_zone_index);}
static void clock_zone_step_index(int *index,int delta){int n=(int)atm_timezone_count();if(!index||n<1)return;*index=(*index+delta)%n;if(*index<0)*index+=n;}
static void clock_zone_step(exp_win_t *w,int delta){clock_zone_step_index(&w->clock_zone_index,delta);}
static int clock_zone_state_selftest(void){int index=0,n=(int)atm_timezone_count();if(n<2||!atm_timezone_name(0))return -1;clock_zone_step_index(&index,-1);if(index!=n-1)return -1;clock_zone_step_index(&index,1);return index==0?0:-1;}
static void clock_zone_apply(exp_win_t *w){const char *zone=clock_browse_zone(w);if(sysconf_set("system","timezone",zone)<0){exp_notify("Timezone setting could not be saved",C_RED);return;}sysconf_save();char msg[80];kstrcpy(msg,"Timezone: ");kstrcat(msg,zone);exp_notify(msg,C_GREEN);}
static void clock_key(exp_win_t *w,int k){if(k==KEY_LEFT)clock_zone_step(w,-1);else if(k==KEY_RIGHT)clock_zone_step(w,1);else if(k=='a'||k=='A')clock_zone_apply(w);else if(k=='s'||k=='S'||k==' '||k=='\n'||k=='\r')stopwatch_toggle(w);else if(k=='r'||k=='R')stopwatch_reset(w);}
static void draw_clock_app(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);(void)ch;
    R(cx,cy,cw2,ch,C_BASE);
    const char *zone=sysconf_get("system","timezone"),*basis=sysconf_get("system","rtc_basis"),*view=clock_browse_zone(w);
    int rtc_is_local=basis&&!kstrcmp(basis,"local"),offset=0,dst=0,have_time=0;
    rtc_datetime_t local;
    if(rtc_is_local)have_time=rtc_read_datetime(&local)==0;
    else have_time=atm_local_datetime(zone&&zone[0]?zone:"UTC",&local,&offset,&dst)==0;
    T(cx+18,cy+10,"SYSTEM CLOCK",C_SUBTEXT,C_BASE);
    char zone_line[96];ksnprintf(zone_line,sizeof(zone_line),"Active: %s  |  RTC %s",zone&&zone[0]?zone:"UTC",rtc_is_local?"local":"UTC");TC(cx+18,cy+26,zone_line,C_LAVENDER,C_BASE,cw2-36);
    R(cx+18,cy+46,cw2-36,56,C_CRUST); BOX(cx+18,cy+46,cw2-36,56,C_SURFACE2);
    if(have_time){char clock[16],date[32],meta[48];ksnprintf(clock,sizeof(clock),"%02d:%02d:%02d",local.hour,local.minute,local.second);ksnprintf(date,sizeof(date),"%04d-%02d-%02d",local.year,local.month,local.day);if(rtc_is_local)kstrcpy(meta,"Firmware local clock; no conversion");else{int a=offset<0?-offset:offset;ksnprintf(meta,sizeof(meta),"UTC%c%02d:%02d%s",offset<0?'-':'+',a/60,a%60,dst?"  DST":"");}T(cx+34,cy+58,clock,C_TEXT,C_CRUST);T(cx+164,cy+58,date,C_SUBTEXT,C_CRUST);T(cx+34,cy+78,meta,C_OVERLAY0,C_CRUST);}else T(cx+34,cy+66,"CMOS RTC or selected timezone unavailable",C_YELLOW,C_CRUST);
    HL(cx+18,cy+116,cw2-36,C_SURFACE1);T(cx+18,cy+126,"TIMEZONE PREVIEW  [Left/Right browse, A save]",C_SUBTEXT,C_BASE);
    R(cx+18,cy+146,cw2-36,48,C_CRUST);BOX(cx+18,cy+146,cw2-36,48,C_SURFACE2);
    rtc_datetime_t preview;int poff=0,pdst=0,have_preview=!rtc_is_local&&atm_local_datetime(view,&preview,&poff,&pdst)==0;
    if(have_preview){char pv[96],meta[40];int a=poff<0?-poff:poff;ksnprintf(pv,sizeof(pv),"%s  %04d-%02d-%02d  %02d:%02d:%02d",view,preview.year,preview.month,preview.day,preview.hour,preview.minute,preview.second);ksnprintf(meta,sizeof(meta),"UTC%c%02d:%02d%s",poff<0?'-':'+',a/60,a%60,pdst?"  DST":"");TC(cx+30,cy+156,pv,C_TEXT,C_CRUST,cw2-60);T(cx+30,cy+174,meta,C_OVERLAY0,C_CRUST);}else TC(cx+30,cy+164,rtc_is_local?"Preview requires RTC basis UTC (timezone clock utc).":"CMOS RTC unavailable for timezone preview.",C_YELLOW,C_CRUST,cw2-60);
    int pby=cy+204;R(cx+18,pby,38,26,C_SURFACE1);BOX(cx+18,pby,38,26,C_SURFACE2);T(cx+31,pby+5,"<",C_TEXT,C_SURFACE1);R(cx+62,pby,38,26,C_SURFACE1);BOX(cx+62,pby,38,26,C_SURFACE2);T(cx+75,pby+5,">",C_TEXT,C_SURFACE1);R(cx+108,pby,cw2-126,26,C_GREEN);BOX(cx+108,pby,cw2-126,26,C_SURFACE2);TC(cx+116,pby+5,"Set active [A]",C_BASE,C_GREEN,cw2-142);
    HL(cx+18,cy+246,cw2-36,C_SURFACE1);T(cx+18,cy+256,"STOPWATCH",C_SUBTEXT,C_BASE);
    uint32_t ticks=stopwatch_ticks(w),centis=ticks%100u,total=ticks/100u;char watch[32];ksnprintf(watch,sizeof(watch),"%02u:%02u:%02u.%02u",total/3600u,(total/60u)%60u,total%60u,centis);
    R(cx+18,cy+276,cw2-36,44,C_CRUST);BOX(cx+18,cy+276,cw2-36,44,C_SURFACE2);T(cx+34,cy+288,watch,C_TEXT,C_CRUST);
    int bw=(cw2-44)/2,by=cy+334;R(cx+18,by,bw,28,w->stopwatch_running?C_YELLOW:C_SURFACE1);BOX(cx+18,by,bw,28,C_SURFACE2);T(cx+27,by+6,w->stopwatch_running?"Pause [S]":"Start [S]",w->stopwatch_running?C_BASE:C_TEXT,w->stopwatch_running?C_YELLOW:C_SURFACE1);R(cx+26+bw,by,bw,28,C_MANTLE);BOX(cx+26+bw,by,bw,28,C_SURFACE2);T(cx+35+bw,by+6,"Reset [R]",C_SUBTEXT,C_MANTLE);
    T(cx+18,cy+374,"Stopwatch uses monotonic PIT ticks; it is independent of CMOS time.",C_OVERLAY0,C_BASE);
}

/* ─── Editor /Viewer ────────────────────────────────────── */
static void ed_load(exp_win_t *w, const char *path){
    kstrncpy(w->ed_path,path,127);
    int fd=vfs_open(path,O_RDONLY, 0);
    if(fd<0){kstrcpy(w->ed_buf,"(not found)");w->ed_len=11;return;}
    w->ed_len=vfs_read(fd,(uint8_t*)w->ed_buf,sizeof(w->ed_buf)-1);
    vfs_close(fd);
    if(w->ed_len<0)w->ed_len=0;
    w->ed_buf[w->ed_len]=0;
}

static void notepad_load(exp_win_t *w){
    int fd=vfs_open(w->ed_path,O_RDONLY,0);
    if(fd<0){w->ed_len=0;w->ed_cursor=0;w->ed_buf[0]=0;return;}
    int n=vfs_read(fd,(uint8_t*)w->ed_buf,sizeof(w->ed_buf)-1);
    vfs_close(fd); if(n<0)n=0;
    w->ed_len=n; w->ed_cursor=n; w->ed_buf[n]=0;
}

static void notepad_save(exp_win_t *w){
    int fd=vfs_open(w->ed_path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0){exp_notify("Notepad: save denied",C_RED);return;}
    vfs_write(fd,(const uint8_t*)w->ed_buf,(uint32_t)w->ed_len); vfs_close(fd);
    w->ed_dirty=0; exp_notify("Notepad saved",C_GREEN);
}

static void notepad_insert(exp_win_t *w,const char *src,int n){
    if(n<1||w->ed_len+n>=(int)sizeof(w->ed_buf)-1)return;
    for(int i=w->ed_len+n;i>=w->ed_cursor+n;i--) w->ed_buf[i]=w->ed_buf[i-n];
    for(int i=0;i<n;i++)w->ed_buf[w->ed_cursor+i]=src[i];
    w->ed_cursor+=n; w->ed_len+=n; w->ed_buf[w->ed_len]=0; w->ed_dirty=1;
}

static void notepad_key(exp_win_t *w,int k,int ctrl){
    /* keyboard_poll() encodes Ctrl+S as ASCII DC3 (0x13). Keep the
     * printable form too for alternative input back ends. */
    if(k==0x13 || (ctrl && (k=='s'||k=='S'))){notepad_save(w);return;}
    if(k==KEY_LEFT){if(w->ed_cursor>0)w->ed_cursor--;return;}
    if(k==KEY_RIGHT){if(w->ed_cursor<w->ed_len)w->ed_cursor++;return;}
    if(k==KEY_HOME){while(w->ed_cursor>0&&w->ed_buf[w->ed_cursor-1]!='\n')w->ed_cursor--;return;}
    if(k==KEY_END){while(w->ed_cursor<w->ed_len&&w->ed_buf[w->ed_cursor]!='\n')w->ed_cursor++;return;}
    if(k=='\b'||k==127){
        if(w->ed_cursor>0){for(int i=w->ed_cursor-1;i<w->ed_len;i++)w->ed_buf[i]=w->ed_buf[i+1];w->ed_cursor--;w->ed_len--;w->ed_dirty=1;}
        return;
    }
    if(k==KEY_DEL){
        if(w->ed_cursor<w->ed_len){for(int i=w->ed_cursor;i<w->ed_len;i++)w->ed_buf[i]=w->ed_buf[i+1];w->ed_len--;w->ed_dirty=1;}
        return;
    }
    if(k=='\n'||k=='\r'){char c='\n';notepad_insert(w,&c,1);return;}
    if(k==0x200){extern char keyboard_utf8_buf[4];notepad_insert(w,keyboard_utf8_buf,(int)kstrlen(keyboard_utf8_buf));return;}
    if(k>=0x20&&k<=0x7E){char c=(char)k;notepad_insert(w,&c,1);}
}

static void draw_editor(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    R(cx,cy,cw2,ch,C_BASE);
    R(cx,cy,cw2,18,C_MANTLE);
    TF(cx+4,cy+1,C_LAVENDER,C_MANTLE," %s",w->ed_path);
    HL(cx,cy+18,cw2,C_SURFACE0);
    const char *p=w->ed_buf;
    int skip=w->ed_scroll, lnum=1;
    while(*p&&skip>0){while(*p&&*p!='\n')p++;if(*p=='\n')p++;skip--;lnum++;}
    int row=0, mr=(ch-22)/16; if(mr<1)mr=1;
    file_fmt_t fmt=fmt_detect((uint8_t*)w->ed_buf,(uint32_t)w->ed_len,w->ed_path);
    while(*p&&row<mr){
        char line[82]; int li=0, maxcols=(cw2-48)/8;
        if(maxcols<1)maxcols=1; if(maxcols>80)maxcols=80;
        while(*p&&*p!='\n'&&li<maxcols) line[li++]=*p++;
        line[li]=0; if(*p=='\n') p++;
        int ly=cy+20+row*16;
        char lnb[6]; kitoa(lnum,lnb,10);
        int lnl=(int)kstrlen(lnb);
        for(int qi=0;qi<4-lnl;qi++) T(cx+2+qi*8,ly,"0",C_SURFACE2,C_BASE);
        T(cx+2+(4-lnl)*8,ly,lnb,C_SURFACE2,C_BASE);
        VL(cx+36,ly,16,C_SURFACE1);
        color32_t lc=C_TEXT;
        if(fmt==FMT_INI){
            if(line[0]=='[') lc=C_YELLOW;
            else if(line[0]=='#') lc=C_OVERLAY0;
        } else if(fmt==FMT_C_SOURCE||fmt==FMT_SHELL){
            if(line[0]=='#'||kstrncmp(line,"//",2)==0) lc=C_OVERLAY0;
        }
        T(cx+40,ly,line,lc,C_BASE);
        row++; lnum++;
    }
    /* Scrollbar */
    int tot_lines=0; const char *pp=w->ed_buf;
    while(*pp){if(*pp=='\n')tot_lines++;pp++;} tot_lines++;
    if(tot_lines>mr){
        int sbh=ch-22; int th=sbh*mr/tot_lines; if(th<6)th=6;
        int ty=cy+20+(sbh-th)*w->ed_scroll/tot_lines;
        R(cx+cw2-5,cy+20,5,sbh,C_SURFACE0);
        R(cx+cw2-4,ty,3,th,C_SURFACE2);
    }
    HL(cx,cy+ch-16,cw2,C_SURFACE0);
    R(cx,cy+ch-15,cw2,15,C_MANTLE);
    if(w->app==APP_NOTEPAD||w->app==APP_JOURNAL)
        TF(cx+4,cy+ch-14,w->ed_dirty?C_PEACH:C_SUBTEXT,C_MANTLE,"%s  Ctrl+S save  |  arrows/Home/End  |  %d bytes",w->ed_dirty?"Modified":"Saved",w->ed_len);
    else
        TF(cx+4,cy+ch-14,C_SUBTEXT,C_MANTLE,"%s  line %d  PgUp/Dn/arrows",fmt_name(fmt),w->ed_scroll+1);
}

static void ed_key(exp_win_t *w, int k){
    if(k==KEY_PGUP){w->ed_scroll-=6;if(w->ed_scroll<0)w->ed_scroll=0;}
    if(k==KEY_PGDN){w->ed_scroll+=6;}
    if(k==KEY_UP&&w->ed_scroll>0) w->ed_scroll--;
    if(k==KEY_DOWN) w->ed_scroll++;
    if(k==KEY_HOME) w->ed_scroll=0;
}

/* ─── Settings ───────────────────────────────────────────── */
static void draw_settings(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    R(cx,cy,cw2,ch,C_BASE);
    const char *tabs[]={l10n_get(L10N_APPEARANCE), l10n_get(L10N_LANGUAGE),
                        l10n_get(L10N_SYSTEM), l10n_get(L10N_ABOUT)};
    int ntabs=4, tw=cw2/ntabs;
    for(int i=0;i<ntabs;i++){
        color32_t tb=(i==w->cfg_tab)?C_SURFACE1:C_MANTLE;
        color32_t tf=(i==w->cfg_tab)?C_TEXT:C_SUBTEXT;
        R(cx+i*tw,cy,tw,22,tb);
        HL(cx+i*tw,cy+22,tw,C_SURFACE1);
        int tl=exp_text_width(tabs[i]); if(tl>tw-8)tl=tw-8;
        TC(cx+i*tw+(tw-tl)/2,cy+3,tabs[i],tf,tb,tw-8);
    }
    HL(cx,cy+22,cw2,C_SURFACE0);
    int sy=cy+32;
    if(w->cfg_tab==0){
        static const color32_t wc[]={RGB(0x0B,0x0B,0x0B),RGB(0x17,0x17,0x17),RGB(0x0C,0x0C,0x0C),RGB(0x10,0x10,0x10),RGB(0x0A,0x32,0x78),RGB(0x0B,0x3A,0x86),RGB(0x0C,0x24,0x50),RGB(0x09,0x36,0x88),RGB(0x10,0x58,0x70),RGB(0x08,0x08,0x08),RGB(0xF0,0xF0,0xF0),RGB(0x20,0x20,0x20),RGB(0x2A,0x2A,0x2A),RGB(0x16,0x16,0x16)};
        T(cx+8,sy,"Desktop theme (D/W; saved automatically):",C_LAVENDER,C_BASE); sy+=22;
        int dark=exp_theme==EXP_THEME_DARK;
        R(cx+8,sy,148,30,dark?C_SURFACE1:C_MANTLE);BOX(cx+8,sy,148,30,dark?C_TEXT:C_SURFACE1);T(cx+16,sy+7,"D. Dark Mono",dark?C_TEXT:C_SUBTEXT,dark?C_SURFACE1:C_MANTLE);
        R(cx+168,sy,148,30,!dark?C_SURFACE1:C_MANTLE);BOX(cx+168,sy,148,30,!dark?C_TEXT:C_SURFACE1);T(cx+176,sy+7,"W. White Paper",!dark?C_TEXT:C_SUBTEXT,!dark?C_SURFACE1:C_MANTLE);sy+=42;
        T(cx+8,sy,"Interface scale: fixed at 100% for stable layout.",C_SUBTEXT,C_BASE);sy+=22;
        T(cx+8,sy,"Built-in wallpaper (keys 1-9; click all 14; clears image wallpaper):",C_LAVENDER,C_BASE); sy+=24;
        int cols=4,cellw=(cw2-40)/cols;
        for(int i=0;i<EXP_BUILTIN_WALLPAPERS;i++){
            int tx=cx+8+(i%cols)*(cellw+8),ty=sy+(i/cols)*52;
            R(tx,ty,cellw,44,C_MANTLE);BOX(tx,ty,cellw,44,i==wallpaper_id?C_TEXT:C_SURFACE1);
            R(tx+6,ty+6,cellw-12,16,wc[i]);
            if(i==2){for(int gx=tx+6;gx<tx+cellw-6;gx+=16)VL(gx,ty+6,16,C_SURFACE1);}
            if(i==3||i==5){HL(tx+6,ty+12,cellw-12,C_SURFACE1);}
            if(i==6){BOX(tx+cellw-38,ty+7,22,12,C_SURFACE1);}
            if(i==7){VL(tx+cellw/2,ty+6,16,C_SURFACE1);}
            char tag[20];kitoa(i+1,tag,10);kstrcat(tag,". ");kstrcat(tag,wallpaper_name(i));
            T(tx+8,ty+26,tag,i==wallpaper_id?C_TEXT:C_SUBTEXT,C_MANTLE);
        }
        sy+=218;
        TF(cx+8,sy,C_SUBTEXT,C_BASE,"Current: %s",wallpaper_label()); sy+=18;
        T(cx+8,sy,"Open PNG/JPEG/BMP in Viewer and press A to apply it as wallpaper.",C_SUBTEXT,C_BASE); sy+=18;
        T(cx+8,sy,"Resolution: choose 640x480, 800x600 or 1024x768 in GRUB",C_SUBTEXT,C_BASE);
    } else if(w->cfg_tab==1){
        /* Localized prose wraps inside the content pane.  This is especially
         * important for Cyrillic, whose UTF-8 byte length is not its glyph count. */
        int desc_rows=TW(cx+8,sy,l10n_get(L10N_LANGUAGE_DESC),C_LAVENDER,C_BASE,cw2-16,2);
        sy+=(desc_rows>1?40:24);
        TF(cx+8,sy,C_TEXT,C_BASE,"Current: %s [%s]",l10n_current_name(),l10n_current_code()); sy+=28;
        for(uint32_t i=0;i<l10n_available_count();i++){
            int ly=sy+(int)i*34;int selected=kstrcmp(l10n_current_code(),l10n_available_code(i))==0; color32_t bg=selected?C_SURFACE1:C_MANTLE;
            R(cx+8,ly,cw2-16,28,bg);BOX(cx+8,ly,cw2-16,28,selected?C_TEXT:C_SURFACE1);
            TF(cx+16,ly+5,selected?C_TEXT:C_SUBTEXT,bg,"%u. %s [%s]",i+1,l10n_available_name(i),l10n_available_code(i));
        }
        sy+=34*(int)l10n_available_count()+6;TW(cx+8,sy,l10n_get(L10N_ONLY_ENGLISH),C_SUBTEXT,C_BASE,cw2-16,2);
    } else if(w->cfg_tab==2){
        sdk_cpuid_t cpu; sdk_cpuid(&cpu);
        T(cx+8,sy,"System Info:",C_LAVENDER,C_BASE); sy+=20;
        TF(cx+8,sy,C_TEXT,C_BASE,"OS      atmkoala v0.5"); sy+=16;
        TF(cx+8,sy,C_SUBTEXT,C_BASE,"CPU     %s",cpu.brand[0]?cpu.brand:cpu.vendor); sy+=16;
        TF(cx+8,sy,C_TEXT,C_BASE,"RAM     %u KB free",heap_free_bytes()/1024); sy+=16;
        TF(cx+8,sy,C_TEXT,C_BASE,"VBE     %ux%u x32bpp",vbe.width,vbe.height); sy+=16;
        TF(cx+8,sy,C_TEXT,C_BASE,"Disk    %d  Net %s",disk_count,net.initialized?"UP":"--"); sy+=16;
        if(g_battery.valid&&g_battery.present) TF(cx+8,sy,C_TEXT,C_BASE,"Battery %u%%  %s",g_battery.capacity_pct,g_battery.charging?"charging":"discharging");
        else T(cx+8,sy,"Battery telemetry unavailable",C_SUBTEXT,C_BASE); sy+=16;
        if(g_radio.wifi_connected) TF(cx+8,sy,C_GREEN,C_BASE,"Wi-Fi connected  %u%% signal",g_radio.wifi_signal_pct);
        else if(g_radio.wifi_driver_ready) T(cx+8,sy,"Wi-Fi driver ready; not associated",C_YELLOW,C_BASE);
        else if(g_radio.wifi_controller_present) T(cx+8,sy,"Wi-Fi controller detected; driver unavailable",C_YELLOW,C_BASE);
        else T(cx+8,sy,"Wi-Fi unavailable",C_SUBTEXT,C_BASE); sy+=16;
        if(g_radio.bluetooth_connected) T(cx+8,sy,"Bluetooth connected",C_GREEN,C_BASE);
        else if(g_radio.bluetooth_driver_ready) T(cx+8,sy,g_radio.bluetooth_enabled?"Bluetooth enabled; no device connected":"Bluetooth driver ready; radio disabled",C_YELLOW,C_BASE);
        else if(g_radio.bluetooth_controller_present) T(cx+8,sy,"Bluetooth controller detected; HCI unavailable",C_YELLOW,C_BASE);
        else T(cx+8,sy,"Bluetooth HCI driver unavailable",C_SUBTEXT,C_BASE); sy+=16;
        uint32_t up=sched_uptime_ticks()/100;
        TF(cx+8,sy,C_TEXT,C_BASE,"Uptime  %uh %um %us",up/3600,(up/60)%60,up%60); sy+=18;
        const char *active_zone=sysconf_get("system","timezone");TF(cx+8,sy,C_TEXT,C_BASE,"Timezone %s  |  Clock: browse arrows, A saves",active_zone&&active_zone[0]?active_zone:"UTC");sy+=20;
        R(cx+8,sy,184,28,C_SURFACE1);BOX(cx+8,sy,184,28,C_BLUE);T(cx+18,sy+7,"P. Preview screensaver",C_TEXT,C_SURFACE1);
        T(cx+204,sy+7,"Auto-start: 5-10 min idle",C_SUBTEXT,C_BASE);
    } else {
        T(cx+8,sy,"atmkoala v0.5",C_LAVENDER,C_BASE); sy+=24;
        T(cx+8,sy,"x86-64 OS from scratch — C + ASM",C_TEXT,C_BASE); sy+=18;
        const char *h[]={"v1 VGA shell","v2 IDT/PIT/colors","v3 VBE/VFS/net",
            "v4 ATA/QewoxFS","v5 Scrollback/DE","v6 SDK/extensions",
            "v7 Snake/Tetris","v8 Cyrillic/FAT32","v9 RU/AI/ping",
            "v10 Exp: GUI ABI, fixed 100%, themes and Image Viewer",NULL};
        for(int i=0;h[i]&&sy<cy+ch-16;i++){
            T(cx+16,sy,h[i],i==9?C_YELLOW:C_SUBTEXT,C_BASE); sy+=14;
        }
    }
    (void)ch;
}
static void settings_wallpaper_set(int id){
    if(id<0||id>=EXP_BUILTIN_WALLPAPERS)return;wallpaper_file_clear(0);wallpaper_id=id;char value[4];ksnprintf(value,sizeof(value),"%d",id+1);
    sysconf_set("desktop","wallpaper",value);sysconf_set("desktop","wallpaper_file","");sysconf_save();
    char msg[48];kstrcpy(msg,"Wallpaper: ");kstrcat(msg,wallpaper_name(wallpaper_id));exp_notify(msg,C_TEXT);exp_force_redraw=1;
}
static void settings_theme_set(exp_theme_t id){
    exp_theme_set(id,1);char msg[48];kstrcpy(msg,"Exp theme: ");kstrcat(msg,exp_theme_name());exp_notify(msg,C_TEXT);exp_force_redraw=1;
}
static void settings_language_set(uint32_t id){
    if(id>=l10n_available_count()||l10n_set(l10n_available_code(id))<0)return;
    char msg[48];kstrcpy(msg,"Language: ");kstrcat(msg,l10n_current_name());exp_notify(msg,C_TEXT);exp_force_redraw=1;
}
static void settings_key(exp_win_t *w, int k){
    if(w->cfg_tab==0&&(k=='d'||k=='D'))settings_theme_set(EXP_THEME_DARK);
    else if(w->cfg_tab==0&&(k=='w'||k=='W'))settings_theme_set(EXP_THEME_WHITE);
    else if(k>='1'&&k<='9'&&w->cfg_tab==0)settings_wallpaper_set(k-'1');
    else if(k>='1'&&k<='3'&&w->cfg_tab==1)settings_language_set((uint32_t)(k-'1'));
    else if(w->cfg_tab==2&&(k=='p'||k=='P'))saver_begin();
    if(k==KEY_LEFT||k=='h'){w->cfg_tab--;if(w->cfg_tab<0)w->cfg_tab=3;}
    if(k==KEY_RIGHT||k=='l'){w->cfg_tab=(w->cfg_tab+1)%4;}
}

static void app_mouse_press(exp_win_t *w,int mx,int my){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    if(w->app==APP_FILES){
        int lw2=(cw2*3)/5,row=(my-(cy+22))/17;
        if(inside(mx,my,cx,cy,cw2,18)){fm_key(w,'\b');return;}
        if(inside(mx,my,cx,cy+22,lw2,ch-38)&&row>=0){int item=w->fm_scroll+row;if(item>=0&&item<w->fm_count){uint32_t now=pit_get_ticks();if(mouse_last_win==w->id&&mouse_last_item==item&&now-mouse_last_tick<50)fm_key(w,'\n');else{w->fm_sel=item;fm_preview(w);}mouse_last_win=w->id;mouse_last_item=item;mouse_last_tick=now;}return;}
    } else if(w->app==APP_SETTINGS){
        if(my>=cy&&my<cy+22){int tab=(mx-cx)/(cw2/4);if(tab>=0&&tab<4)w->cfg_tab=tab;return;}
        if(w->cfg_tab==0){int sy=cy+54;if(inside(mx,my,cx+8,sy,148,30)){settings_theme_set(EXP_THEME_DARK);return;}if(inside(mx,my,cx+168,sy,148,30)){settings_theme_set(EXP_THEME_WHITE);return;}sy=cy+142;int cellw=(cw2-40)/4;for(int i=0;i<EXP_BUILTIN_WALLPAPERS;i++){int tx=cx+8+(i%4)*(cellw+8),ty=sy+(i/4)*52;if(inside(mx,my,tx,ty,cellw,44)){settings_wallpaper_set(i);return;}}}
        else if(w->cfg_tab==1){int sy=cy+84;for(uint32_t i=0;i<l10n_available_count();i++)if(inside(mx,my,cx+8,sy+(int)i*34,cw2-16,28)){settings_language_set(i);return;}}
        else if(w->cfg_tab==2&&inside(mx,my,cx+8,cy+218,184,28)){saver_begin();return;}
    } else if(w->app==APP_EXTERNAL&&w->ext_slot>=0&&w->ext_slot<gui_app_count){
        exp_gui_context_t ctx;gui_context(&ctx,w);if(gui_apps[w->ext_slot].pointer)gui_apps[w->ext_slot].pointer(&ctx,mx-CX(w),my-CY(w),1);return;
    } else if(w->app==APP_CALCULATOR){
        int bw=(cw2-44)/4,by=cy+92;if(inside(mx,my,cx+10,by,cw2-20,4*34)){int col=(mx-(cx+10))/(bw+8),row=(my-by)/34;if(col>=0&&col<4&&row>=0&&row<4){static const char keys[]={'7','8','9','/','4','5','6','*','1','2','3','-','0','C','=','+'};int k=keys[row*4+col];calc_key(w,k=='='?'\n':k);return;}}
    } else if(w->app==APP_TASKS){
        for(int i=0;i<5;i++)if(inside(mx,my,cx+10,cy+58+i*34,cw2-20,28)){w->todo_sel=i;w->todo_done[i]^=1;return;}
    } else if(w->app==APP_CLOCK){
        int pby=cy+204;if(inside(mx,my,cx+18,pby,38,26))clock_zone_step(w,-1);else if(inside(mx,my,cx+62,pby,38,26))clock_zone_step(w,1);else if(inside(mx,my,cx+108,pby,cw2-126,26))clock_zone_apply(w);else{int bw=(cw2-44)/2,by=cy+334;if(inside(mx,my,cx+18,by,bw,28))stopwatch_toggle(w);else if(inside(mx,my,cx+26+bw,by,bw,28))stopwatch_reset(w);}return;
    } else if(w->app==APP_MINES){
        int bx=cx+16,by=cy+48,cell=24;if(inside(mx,my,bx,by,10*cell,10*cell)){int gx=(mx-bx)/cell,gy=(my-by)/cell;w->mines.cursor_x=gx;w->mines.cursor_y=gy;mines_reveal(&w->mines,gx,gy);return;}
    } else if(w->app==APP_SNAKE){
        /* Four compact click controls below the board: left, up, down, right/restart. */
        int by=cy+44+14*16+12;if(inside(mx,my,cx+14,by,54,24))snake_key(w,KEY_LEFT);else if(inside(mx,my,cx+72,by,54,24))snake_key(w,KEY_UP);else if(inside(mx,my,cx+130,by,54,24))snake_key(w,KEY_DOWN);else if(inside(mx,my,cx+188,by,74,24)){if(w->snake.state)snake_key(w,'r');else snake_key(w,KEY_RIGHT);}return;
    } else if(w->app==APP_FLAPPY){
        flappy_key(w,' ');return;
    } else if(w->app==APP_SCREENSHOTS){
        if(inside(mx,my,cx+14,cy+80,164,32))screenshot_capture(w);
        return;
    } else if(w->app==APP_POWER){
        int bx=cx+16,by=cy+62,bw=(cw2-48)/3;
        for(int i=0;i<3;i++)if(inside(mx,my,bx+i*(bw+8),by,bw,54)){if(i==0)w->power_action=1;else if(i==1)w->power_action=2;else exp_notify("Sleep unavailable: ACPI S3 is not implemented",C_YELLOW);return;}
        if(w->power_action&&inside(mx,my,cx+14,cy+ch-58,cw2-28,22)){exp_power_commit(w->power_action);return;}
    } else if(w->app==APP_PAINT){
        int bx=cx+14,by=cy+60,cell=12;if(inside(mx,my,bx,by,EXP_PAINT_W*cell,EXP_PAINT_H*cell)){w->paint.cursor_x=(mx-bx)/cell;w->paint.cursor_y=(my-by)/cell;paint_dot(&w->paint,w->paint.cursor_x,w->paint.cursor_y);return;}
        if(inside(mx,my,bx,cy+40,8*27,14)){w->paint.color=(mx-bx)/27;return;}
    } else if((w->app==APP_NOTEPAD||w->app==APP_JOURNAL)&&inside(mx,my,cx,cy+ch-16,cw2,16)){
        notepad_save(w);return;
    }
}

/* ─── About ──────────────────────────────────────────────── */
/* Compact Q-koala mark derived from the supplied ATMKoala reference logo.
 * It remains vector-like and crisp at every Exp scale, without needing a
 * PNG renderer during early desktop drawing. */
static void draw_atmkoala_mark(int x,int y,int u,color32_t fg,color32_t bg){
    if(u<1)u=1;int s=16*u;
    RR(x+3*u,y+u,10*u,10*u,fg);R(x+5*u,y+3*u,6*u,6*u,bg);
    R(x+10*u,y+10*u,5*u,2*u,fg);R(x+12*u,y+11*u,2*u,3*u,fg);
    RR(x+5*u,y+6*u,6*u,5*u,fg);R(x+6*u,y+7*u,4*u,3*u,bg);
    R(x+5*u,y+5*u,2*u,2*u,fg);R(x+9*u,y+5*u,2*u,2*u,fg);
    R(x+7*u,y+8*u,2*u,2*u,fg);
    (void)s;
}
static void draw_about(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    R(cx,cy,cw2,ch,C_BASE);
    /* Product card: compact enough for all supported UI scales. */
    R(cx+10,cy+12,cw2-20,54,C_MANTLE);BOX(cx+10,cy+12,cw2-20,54,C_SURFACE2);
    draw_atmkoala_mark(cx+18,cy+19,2,C_TEXT,C_MANTLE);
    T(cx+58,cy+20,"ATMKOALA  v0.5",C_TEXT,C_MANTLE);
    T(cx+58,cy+38,"Q-koala / Exp desktop / native x86-64",C_SUBTEXT,C_MANTLE);
    T(cx+58,cy+52,"C + ASM / freestanding / no libc",C_SUBTEXT,C_MANTLE);
    int ay=cy+78;
    T(cx+12,ay,"A focused desktop for the native system layer",C_LAVENDER,C_BASE);ay+=18;
    T(cx+12,ay,"VBE graphics + Multiboot2 boot.",C_SUBTEXT,C_BASE);ay+=14;
    T(cx+12,ay,"VGA Caramel console stays separate.",C_SUBTEXT,C_BASE);ay+=16;
    HL(cx+12,ay,cw2-24,C_SURFACE1);ay+=10;
    const char *fl[]={
        "Themes: Dark Mono / White Paper; fixed 100% layout",
        "GUI ABI v1: external apps, keyboard and pointer",
        "Files opens the bounded PNG and JPEG viewer",
        "Taskbar: locale, telemetry and notifications",
        "Settings owns appearance; no terminal themes",
        "POSIX: cwd, dirs, access, umask and isatty",
        NULL
    };
    for(int i=0;fl[i]&&ay<cy+ch-18;i++){
        R(cx+12,ay+5,4,4,i==2?C_PEACH:C_SURFACE2);
        T(cx+24,ay,fl[i],i==2?C_TEXT:C_SUBTEXT,C_BASE);ay+=16;
    }
}

/* ─── Launcher ───────────────────────────────────────────── */
static const struct{const char *name,*desc;app_id_t app;color32_t ic;}
LA[23]={
    {"Terminal","Command workspace",APP_TERMINAL,RGB(0x48,0x6E,0x85)},
    {"Files",   "Local file browser",APP_FILES,RGB(0x8F,0x7A,0x3A)},
    {"Notepad", "Plain text editor",APP_NOTEPAD,RGB(0x44,0x76,0x4B)},
    {"Monitor", "CPU and memory",APP_SYSMON,RGB(0x75,0x5B,0x75)},
    {"System Info", "Hardware snapshot",APP_SYSINFO,RGB(0x5A,0x70,0x7A)},
    {"Power", "Halt, restart, sleep status",APP_POWER,RGB(0x9A,0x4D,0x4D)},
    {"Events", "Init and kernel rings",APP_EVENTLOG,RGB(0x70,0x70,0x68)},
    {"Screenshots", "Capture RGB PPM",APP_SCREENSHOTS,RGB(0x5A,0x70,0x7A)},
    {"Settings","Desktop preferences",APP_SETTINGS,RGB(0x98,0x5F,0x39)},
    {"Calculator","Integer expressions",APP_CALCULATOR,RGB(0x52,0x52,0x4B)},
    {"Tasks",   "Local checklist",APP_TASKS,RGB(0x43,0x72,0x69)},
    {"Journal", "Persistent daily notes",APP_JOURNAL,RGB(0x70,0x70,0x68)},
    {"Clock",   "Uptime and time",APP_CLOCK,RGB(0x38,0x38,0x34)},
    {"Calendar","RTC-backed month view",APP_CALENDAR,RGB(0x38,0x38,0x34)},
    {"About",   "System overview",APP_ABOUT,RGB(0x77,0x77,0x70)},
    {"TinyGL",  "Software 3D demo",APP_TINYGL,RGB(0x46,0x70,0x82)},
    {"Mines",   "GUI minesweeper",APP_MINES,RGB(0x52,0x52,0x4B)},
    {"Snake",   "GUI arcade snake",APP_SNAKE,RGB(0x44,0x76,0x4B)},
    {"Flappy Bird", "GUI arcade flight",APP_FLAPPY,RGB(0xA0,0x87,0x32)},
    {"Tetris", "GUI falling blocks",APP_TETRIS,RGB(0x88,0x62,0x91)},
    {"Paint",   "Local pixel canvas",APP_PAINT,RGB(0x9A,0x58,0x50)},
    {"Viewer",  "Text and image viewer",APP_VIEWER,RGB(0x60,0x75,0x8B)},
    {"ArchiveEx", "Validated TAR.ZST/ATPK installer",APP_ARCHIVEEX,RGB(0x7B,0x66,0x28)},
};
#define NLA 23
#define LA_COLS 3
#define LA_ROWS ((NLA+LA_COLS-1)/LA_COLS)

static void launcher_rect(int *ox,int *oy,int *ow,int *oh){
    int w=522,h=LA_ROWS*46+104;
    *ox=6;*oy=DE_SCR_H-DE_TASKBAR_H-h-4;*ow=w;*oh=h;
}
static void draw_launcher(void){
    int lx,ly,lw,lh;launcher_rect(&lx,&ly,&lw,&lh);
    /* XP interaction model, rendered in Exp's neutral monochrome palette. */
    R(lx+3,ly+3,lw,lh,C_CRUST); RR(lx,ly,lw,lh,C_MANTLE); BOX(lx,ly,lw,lh,C_BLUE);
    R(lx+2,ly+2,lw-4,44,C_SURFACE1);
    draw_app_icon(APP_ABOUT,lx+12,ly+9,28,C_LAVENDER,C_SURFACE1);
    T(lx+50,ly+10,"ATMKoala",C_TEXT,C_SURFACE1);T(lx+50,ly+25,"native desktop session",C_SUBTEXT,C_SURFACE1);
    T(lx+lw-70,ly+16,"Esc close",C_SUBTEXT,C_SURFACE1);HL(lx+2,ly+46,lw-4,C_SURFACE2);
    int cellw=(lw-28)/LA_COLS;
    for(int i=0;i<NLA;i++){
        int col=i/LA_ROWS,row=i%LA_ROWS;
        int ix=lx+12+col*cellw,iy=ly+54+row*46;
        int sel=(i==DE.launcher_sel);color32_t bg=sel?C_SURFACE1:C_MANTLE,fg=sel?C_TEXT:C_SUBTEXT;
        R(ix,iy,cellw-8,42,bg);if(sel)BOX(ix,iy,cellw-8,42,C_BLUE);
        draw_app_icon(LA[i].app,ix+5,iy+6,30,LA[i].ic,bg);
        T(ix+43,iy+7,LA[i].name,fg,bg);T(ix+43,iy+22,LA[i].desc,C_SUBTEXT,bg);
    }
    VL(lx+lw/2,ly+54,LA_ROWS*46,C_SURFACE2);
    R(lx+2,ly+lh-32,lw-4,30,C_SURFACE0);T(lx+12,ly+lh-21,"All native applications",C_SUBTEXT,C_SURFACE0);
    T(lx+lw-156,ly+lh-21,"Enter open  arrows browse",C_SUBTEXT,C_SURFACE0);
}

/* ─── Alt+Tab ────────────────────────────────────────────── */
static void draw_alttab(void){
    if(!DE.alttab_open||DE.win_count==0) return;
    int n=DE.win_count, bw=96, bh=64, gap=6;
    int total=n*(bw+gap)-gap;
    int sx=(DE_SCR_W-total)/2, sy=(DE_SCR_H-bh)/2-16;
    R(sx-14,sy-14,total+28,bh+44,C_CRUST);
    BOX(sx-14,sy-14,total+28,bh+44,C_SURFACE0);
    T(sx,sy-10,"Alt+Tab — Switch Window",C_SUBTEXT,C_CRUST);
    color32_t ac[]={0,C_SKY,C_YELLOW,C_GREEN,C_MAUVE,C_PEACH,C_LAVENDER,C_PINK,C_OVERLAY0,C_TEXT,C_SURFACE2,C_SUBTEXT,C_SURFACE1};
    for(int i=0;i<n;i++){
        exp_win_t *w=&DE.wins[i];
        int ox=sx+i*(bw+gap);
        int sel=(i==DE.alttab_sel);
        color32_t bg=sel?C_SURFACE2:C_SURFACE0;
        RR(ox,sy,bw,bh,bg);
        if(sel) BOX(ox,sy,bw,bh,C_BLUE);
        color32_t acolor=(w->app<17)?ac[w->app<13?w->app:12]:C_SURFACE1;
        draw_app_icon(w->app,ox+32,sy+5,36,acolor,bg);
        char ttl2[10]; kstrncpy(ttl2,w->title,8); ttl2[8]=0;
        T(ox+4,sy+bh-16,ttl2,sel?C_TEXT:C_SUBTEXT,bg);
    }
}

/* ─── open_app ───────────────────────────────────────────── */
int exp_open_app(app_id_t app, const char *path){
    if(DE.win_count>=DE_MAX_WIN) return -1;
    exp_win_t *w=&DE.wins[DE.win_count];
    kmemset(w,0,sizeof(exp_win_t));
    w->id=next_id++; w->app=app; w->ext_slot=-1;
    int n2=DE.win_count;
    w->w=560; w->h=380;
    w->x=60+n2*22; w->y=32+n2*18;
    if(w->x+w->w>DE_SCR_W) w->x=60;
    if(w->y+w->h>DE_SCR_H-DE_TASKBAR_H) w->y=32;
    w->px=w->x; w->py=w->y; w->pw=w->w; w->ph=w->h;
    switch(app){
    case APP_TERMINAL:
        kstrcpy(w->title,"Terminal");
        term_addl(w,"atmkoala v0.5 — Exp terminal",C_GREEN,0);
        term_addl(w,"Type 'help'. 'exit' closes Exp.",C_SUBTEXT,0);
        w->thist_idx=0; break;
    case APP_FILES:
        kstrcpy(w->title,"Files");
        kstrcpy(w->fm_path,(path&&path[0])?path:"/");
        fm_load(w); fm_preview(w); break;
    case APP_EDITOR:
        kstrcpy(w->title,"Viewer");
        if(path&&path[0]){kstrcpy(w->title,path);ed_load(w,path);}
        w->ed_readonly=1;
        break;
    case APP_NOTEPAD:
        kstrcpy(w->title,"Notepad");
        kstrcpy(w->ed_path,(path&&path[0])?path:"/data/notepad.txt");
        w->ed_dirty=0; notepad_load(w);
        break;
    case APP_VIEWER:
        kstrcpy(w->title,(path&&path[0])?path:"Viewer");
        if(path&&path[0]) {
            file_fmt_t ff=fmt_detect(NULL,0,path);
            if(ff==FMT_PNG || ff==FMT_JPEG || ff==FMT_BMP) image_viewer_load(w,path);
            else { ed_load(w,path); w->ed_readonly=1; }
        }
        break;
    case APP_ARCHIVEEX:
        kstrcpy(w->title,(path&&path[0])?path:"ArchiveEx");
        w->w=620;w->h=330;archiveex_load(w,path);break;
    case APP_IMAGE_VIEWER:
        kstrcpy(w->title,(path&&path[0])?path:"Image Viewer");
        w->w=640;w->h=460;if(path&&path[0])image_viewer_load(w,path);else kstrcpy(w->image.error,"Open a PNG or JPEG from Files.");
        break;
    case APP_SYSMON:
        kstrcpy(w->title,"System Monitor");
        w->sysmon.last_tick=sched_uptime_ticks();w->sysmon.last_idle_tick=sched_idle_ticks();w->sysmon.last_read_ops=disk_read_ops;w->sysmon.last_write_ops=disk_write_ops; break;
    case APP_SYSINFO:
        kstrcpy(w->title,"System Information");
        w->w=650;w->h=410;break;
    case APP_EVENTLOG:
        kstrcpy(w->title,"Events");
        w->w=650;w->h=410;w->log_mode=0;w->log_scroll=0;break;
    case APP_POWER:
        kstrcpy(w->title,"Power");
        w->w=510;w->h=230;w->power_action=0;break;
    case APP_SCREENSHOTS:
        kstrcpy(w->title,"Screenshots");
        w->w=560;w->h=260;w->screenshot_count=0;w->screenshot_status[0]=0;break;
    case APP_SETTINGS:
        kstrcpy(w->title,"Settings");
        w->w=580;w->h=440;w->cfg_tab=0; break;
    case APP_CALCULATOR:
        kstrcpy(w->title,"Calculator");
        w->w=360; w->h=330; w->calc_expr[0]=0; w->calc_valid=0; break;
    case APP_TASKS:
        kstrcpy(w->title,"Tasks");
        w->w=460; w->h=420; tasks_init(w); break;
    case APP_JOURNAL:
        kstrcpy(w->title,"Journal");
        kstrcpy(w->ed_path,"/data/journal.txt");
        w->ed_dirty=0; notepad_load(w); break;
    case APP_CLOCK:
        kstrcpy(w->title,"Clock");
        w->w=480;w->h=420;w->stopwatch_started_tick=sched_uptime_ticks();w->clock_zone_index=clock_zone_index(sysconf_get("system","timezone"));break;
    case APP_CALENDAR:
        kstrcpy(w->title,"Calendar");
        w->cal_year=0;w->cal_month=0;w->cal_follow_today=1;w->cal_last_date_key=0;w->cal_last_probe_tick=0;w->w=390;w->h=300;break;
    case APP_TINYGL:
        kstrcpy(w->title,"TinyGL Cube (software)");
        w->tinygl_scene=0;w->w=520;w->h=400;break;
    case APP_MINES:
        kstrcpy(w->title,"Minesweeper");
        w->w=360; w->h=390; mines_init(w); break;
    case APP_SNAKE:
        kstrcpy(w->title,"Snake");
        w->w=380; w->h=330; snake_init(w); break;
    case APP_FLAPPY:
        kstrcpy(w->title,"Flappy Bird");
        w->w=390; w->h=330; flappy_init(w); break;
    case APP_TETRIS:
        kstrcpy(w->title,"Tetris");
        w->w=390; w->h=365; tetris_init(w); break;
    case APP_PAINT:
        kstrcpy(w->title,"Paint");
        w->w=430; w->h=355; paint_init(w); break;
    case APP_ABOUT:
        kstrcpy(w->title,"About");
        w->w=440; w->h=340; break;
    case APP_EXTERNAL:
        kstrcpy(w->title,"External GUI");
        w->w=440; w->h=300; break;
    default:
        kstrcpy(w->title,"Window"); break;
    }
    if(w->w>DE_SCR_W)w->w=DE_SCR_W;
    if(w->h>DE_SCR_H-DE_TASKBAR_H)w->h=DE_SCR_H-DE_TASKBAR_H;
    if(w->x+w->w>DE_SCR_W)w->x=0;
    if(w->y+w->h>DE_SCR_H-DE_TASKBAR_H)w->y=0;
    w->px=w->x;w->py=w->y;w->pw=w->w;w->ph=w->h;
    DE.active=DE.win_count;
    DE.win_count++;
    atminit_note_app_launch(w->title,"GUI window created");
    exp_notify(w->title,C_BLUE);
    return w->id;
}
int exp_open_tinygl_scene(int scene){
    int id=exp_open_app(APP_TINYGL,NULL);if(id<0)return -1;
    exp_win_t *w=&DE.wins[DE.active];w->tinygl_scene=scene?1:0;kstrcpy(w->title,w->tinygl_scene?"TinyGL Gears (software)":"TinyGL Cube (software)");return id;
}

int exp_gui_open(const char *id){
    if(!id)return -1;int slot=-1;for(int i=0;i<gui_app_count;i++)if(!kstrcmp(gui_apps[i].id,id)){slot=i;break;}
    if(slot<0)return -1;int wid=exp_open_app(APP_EXTERNAL,NULL);if(wid<0)return -1;
    exp_win_t *w=&DE.wins[DE.win_count-1];const exp_gui_app_t *a=&gui_apps[slot];w->ext_slot=slot;kstrncpy(w->title,a->title,47);
    if(a->default_w>120)w->w=a->default_w;if(a->default_h>100)w->h=a->default_h;
    if(w->w>DE_SCR_W)w->w=DE_SCR_W;if(w->h>DE_SCR_H-DE_TASKBAR_H)w->h=DE_SCR_H-DE_TASKBAR_H;
    w->pw=w->w;w->ph=w->h;exp_gui_context_t ctx;gui_context(&ctx,w);if(a->open)a->open(&ctx);return wid;
}
static void win_close(int idx){
    if(idx<0||idx>=DE.win_count) return;
    exp_win_t *closing=&DE.wins[idx];if(closing->app==APP_EXTERNAL&&closing->ext_slot>=0&&closing->ext_slot<gui_app_count){exp_gui_context_t ctx;gui_context(&ctx,closing);if(gui_apps[closing->ext_slot].close)gui_apps[closing->ext_slot].close(&ctx);}if(closing->app==APP_IMAGE_VIEWER||closing->app==APP_VIEWER)atm_image_release(&closing->image);if(closing->app==APP_ARCHIVEEX)archiveex_release(closing);
    for(int i=idx;i<DE.win_count-1;i++) DE.wins[i]=DE.wins[i+1];
    DE.win_count--;
    if(DE.active>=DE.win_count) DE.active=DE.win_count-1;
}

static void win_max(exp_win_t *w){
    if(w->maximized){
        w->x=w->px;w->y=w->py;w->w=w->pw;w->h=w->ph;w->maximized=0;
    } else {
        w->px=w->x;w->py=w->y;w->pw=w->w;w->ph=w->h;
        w->x=0;w->y=0;w->w=DE_SCR_W;w->h=DE_SCR_H-DE_TASKBAR_H;
        w->maximized=1;
    }
}

static int inside(int px,int py,int x,int y,int w,int h){return px>=x&&py>=y&&px<x+w&&py<y+h;}
/* Render order is z-order: index 0 is back, final index is front. */
static int win_to_front(int idx){
    if(idx<0||idx>=DE.win_count)return -1;
    int front=DE.win_count-1;
    if(idx!=front){exp_win_t moved=DE.wins[idx];for(int i=idx;i<front;i++)DE.wins[i]=DE.wins[i+1];DE.wins[front]=moved;}
    DE.active=front;return front;
}

static void exp_mouse_press(int mx,int my){
    /* Taskbar launcher and task buttons. */
    int panel_y=DE_SCR_H-DE_TASKBAR_H;
    if(my>=panel_y){
        if(mx<80){DE.launcher_open=!DE.launcher_open;DE.launcher_sel=0;return;}
        int wx=84;
        for(int i=0;i<DE.win_count;i++,wx+=110) if(inside(mx,my,wx,panel_y+4,104,DE_TASKBAR_H-8)){
            if(DE.wins[i].minimized){DE.wins[i].minimized=0;win_to_front(i);}
            else if(DE.active==i)DE.wins[i].minimized=1;
            else win_to_front(i);
            return;
        }
        return;
    }
    /* XP-style Start menu cells. */
    if(DE.launcher_open){
        int lx,ly,lw,lh;launcher_rect(&lx,&ly,&lw,&lh);int cellw=(lw-28)/LA_COLS;
        for(int i=0;i<NLA;i++){int col=i/LA_ROWS,row=i%LA_ROWS;int ix=lx+12+col*cellw,iy=ly+54+row*46;
            if(inside(mx,my,ix,iy,cellw-8,42)){DE.launcher_sel=i;DE.launcher_open=0;exp_open_app(LA[i].app,NULL);return;}}
        return;
    }
    /* Topmost window first. */
    for(int i=DE.win_count-1;i>=0;i--){
        exp_win_t *w=&DE.wins[i]; if(w->minimized||!inside(mx,my,w->x,w->y,w->w,w->h))continue;
        i=win_to_front(i);w=&DE.wins[i];
        int bx=w->x+w->w-20;
        if(inside(mx,my,bx,w->y+3,16,15)){win_close(i);return;}
        if(inside(mx,my,bx-20,w->y+3,16,15)){win_max(w);return;}
        if(inside(mx,my,bx-40,w->y+3,16,15)){w->minimized=1;return;}
        if(!w->maximized&&inside(mx,my,w->x+w->w-16,w->y+w->h-16,16,16)){mouse_resize_win=i;return;}
        if(my<w->y+DE_TITLEBAR_H&&!w->maximized){mouse_drag_win=i;mouse_drag_dx=mx-w->x;mouse_drag_dy=my-w->y;return;}
        app_mouse_press(w,mx,my);return;
    }
    /* Desktop icons. */
    for(int i=0;i<NICONS;i++)if(inside(mx,my,ICONS[i].x,ICONS[i].y,56,56)){
        icon_sel=i;exp_open_app(ICONS[i].app,NULL);return;
    }
    DE.active=-1;
}

static void exp_mouse_update(const mouse_state_t *mouse,uint8_t previous_buttons){
    if(!mouse||!mouse->available)return;
    if((mouse->buttons&1)&&!(previous_buttons&1))exp_mouse_press(mouse->x,mouse->y);
    if((mouse->buttons&1)&&mouse_drag_win>=0&&mouse_drag_win<DE.win_count){
        exp_win_t *w=&DE.wins[mouse_drag_win];
        w->x=mouse->x-mouse_drag_dx; w->y=mouse->y-mouse_drag_dy;
        if(w->x<0)w->x=0; if(w->y<0)w->y=0;
        if(w->x+w->w>DE_SCR_W)w->x=DE_SCR_W-w->w;
        if(w->y+w->h>DE_SCR_H)w->y=DE_SCR_H-w->h;
    }
    if((mouse->buttons&1)&&mouse_resize_win>=0&&mouse_resize_win<DE.win_count){
        exp_win_t *w=&DE.wins[mouse_resize_win];int nw=mouse->x-w->x+1,nh=mouse->y-w->y+1;
        if(nw<220)nw=220;if(nh<140)nh=140;if(nw>DE_SCR_W-w->x)nw=DE_SCR_W-w->x;if(nh>DE_SCR_H-DE_TASKBAR_H-w->y)nh=DE_SCR_H-DE_TASKBAR_H-w->y;w->w=nw;w->h=nh;
    }
    if(!(mouse->buttons&1)){mouse_drag_win=-1;mouse_resize_win=-1;}
}

static void draw_win_content(exp_win_t *w){
    int cx=CX(w),cy=CY(w),cw2=CW(w),ch=CH(w);
    R(cx,cy,cw2,ch,C_BASE);
    switch(w->app){
    case APP_TERMINAL: draw_terminal(w); break;
    case APP_FILES:    draw_files(w);    break;
    case APP_EDITOR:
    case APP_NOTEPAD:  draw_editor(w);   break;
    case APP_SYSMON:   draw_sysmon(w);   break;
    case APP_SYSINFO:  draw_sysinfo(w);  break;
    case APP_EVENTLOG: draw_eventlog(w); break;
    case APP_POWER:    draw_power_app(w); break;
    case APP_SCREENSHOTS: draw_screenshots(w); break;
    case APP_SETTINGS: draw_settings(w); break;
    case APP_CALCULATOR: draw_calculator(w); break;
    case APP_TASKS:    draw_tasks(w);    break;
    case APP_JOURNAL:  draw_editor(w);   break;
    case APP_CLOCK:    draw_clock_app(w);break;
    case APP_CALENDAR: draw_calendar_app(w);break;
    case APP_TINYGL:   draw_tinygl_app(w);break;
    case APP_MINES:    draw_mines_app(w);break;
    case APP_SNAKE:    draw_snake_app(w);break;
    case APP_FLAPPY:   draw_flappy_app(w);break;
    case APP_TETRIS:   draw_tetris_app(w);break;
    case APP_PAINT:    draw_paint_app(w);break;
    case APP_IMAGE_VIEWER: draw_image_viewer(w);break;
    case APP_VIEWER: if(w->image_path[0]) draw_image_viewer(w); else draw_editor(w); break;
    case APP_ARCHIVEEX: draw_archiveex(w); break;
    case APP_ABOUT:    draw_about(w);    break;
    case APP_EXTERNAL:
        if(w->ext_slot>=0&&w->ext_slot<gui_app_count){exp_gui_context_t ctx;gui_context(&ctx,w);gui_apps[w->ext_slot].draw(&ctx);}else T(cx+8,cy+8,"External app unavailable",C_RED,C_BASE);break;
    default:
        T(cx+8,cy+8,"(empty)",C_SUBTEXT,C_BASE); break;
    }
}

static void draw_mouse_cursor(void){
    mouse_state_t sample;const mouse_state_t *m=mouse_snapshot(&sample)?&sample:NULL;
    if(!m || !m->available) return;
    /* White arrow with a dark one-pixel outline.  It is drawn last, so a
     * frame redraw restores the pixels previously covered by the cursor. */
    static const uint16_t shape[12]={0x800,0xC00,0xE00,0xF00,0xF80,0xFC0,
                                     0xFE0,0xFF0,0xF80,0xD80,0x0C0,0x060};
    for(int py=0;py<12;py++) for(int px=0;px<12;px++){
        if(!(shape[py]&(uint16_t)(0x800>>px))) continue;
        int x=m->x+px, y=m->y+py;
        vbe_putpixel(x+1,y+1,C_CRUST);
        vbe_putpixel(x,y,C_TEXT);
    }
}

/* ─── Idle screensaver ───────────────────────────────────── */
static uint32_t saver_timeout_ticks(void){const char *v=sysconf_get("desktop","screensaver_minutes");int minutes=v&&v[0]?kstrtoi(v):5;if(minutes<5||minutes>10)minutes=5;return (uint32_t)minutes*60u*100u;}
/* A noisy physical PS/2 device must not prevent the idle timeout forever.
 * Three physical pixels remain below normal intentional pointer motion while
 * preserving button changes as immediate activity. */
static int saver_mouse_activity(int x,int y,uint8_t buttons,int old_x,int old_y,uint8_t old_buttons){int dx=x-old_x,dy=y-old_y;if(dx<0)dx=-dx;if(dy<0)dy=-dy;return buttons!=old_buttons||dx>2||dy>2;}
static void saver_begin(void){DE.saver_active=1;DE.saver_x=DE_SCR_W/3;DE.saver_y=DE_SCR_H/3;DE.saver_dx=3;DE.saver_dy=2;DE.saver_color=0;DE.saver_last_tick=pit_get_ticks();}
static int saver_state_selftest(void){uint32_t t=saver_timeout_ticks();return t>=30000u&&t<=60000u&&!saver_mouse_activity(101,99,0,100,100,0)&&saver_mouse_activity(103,100,0,100,100,0)&&saver_mouse_activity(100,100,1,100,100,0)?0:-1;}
static void saver_note_activity(void){DE.last_activity_tick=pit_get_ticks();DE.saver_active=0;}
static void saver_tick(void){
    static const color32_t colors[]={RGB(0xD0,0x55,0x55),RGB(0xCF,0xB1,0x50),RGB(0x65,0xB5,0x79),RGB(0x6F,0xA2,0xC7),RGB(0xB5,0x82,0xB4)};
    uint32_t now=pit_get_ticks();if(now-DE.saver_last_tick<3u)return;uint32_t steps=(now-DE.saver_last_tick)/3u;if(steps>8)steps=8;DE.saver_last_tick+=steps*3u;
    while(steps--){DE.saver_x+=DE.saver_dx;DE.saver_y+=DE.saver_dy;int hit=0;if(DE.saver_x<8){DE.saver_x=8;DE.saver_dx=-DE.saver_dx;hit=1;}if(DE.saver_y<8){DE.saver_y=8;DE.saver_dy=-DE.saver_dy;hit=1;}if(DE.saver_x+132>DE_SCR_W){DE.saver_x=DE_SCR_W-132;DE.saver_dx=-DE.saver_dx;hit=1;}if(DE.saver_y+38>DE_SCR_H){DE.saver_y=DE_SCR_H-38;DE.saver_dy=-DE.saver_dy;hit=1;}if(hit)DE.saver_color=(DE.saver_color+1)%5;}
    (void)colors;
}
static void draw_saver(void){
    static const color32_t colors[]={RGB(0xD0,0x55,0x55),RGB(0xCF,0xB1,0x50),RGB(0x65,0xB5,0x79),RGB(0x6F,0xA2,0xC7),RGB(0xB5,0x82,0xB4)};color32_t c=colors[DE.saver_color%5];
    vbe_clear(C_CRUST);R(DE.saver_x-4,DE.saver_y-4,132,38,C_BASE);BOX(DE.saver_x-4,DE.saver_y-4,132,38,c);T(DE.saver_x+8,DE.saver_y+5,"ATMKOALA",c,C_BASE);T(DE.saver_x+8,DE.saver_y+21,"desktop idle mode",C_SUBTEXT,C_BASE);T(12,DE_SCR_H-22,"Idle screensaver — move mouse or press any key",C_OVERLAY0,C_CRUST);
}

static void full_redraw(void){
    draw_desktop();
    for(int i=0;i<DE.win_count;i++){
        exp_win_t *w=&DE.wins[i];
        if(w->minimized) continue;
        draw_chrome(w,i==DE.active);
        draw_win_content(w);
    }
    if(DE.alttab_open) draw_alttab();
    if(DE.launcher_open) draw_launcher();
    draw_notifs();
    draw_taskbar();
    draw_mouse_cursor();
    vbe_present();
}

/* ─── Main loop ──────────────────────────────────────────── */
void exp_init(void){
    l10n_init();
    kmemset(&DE,0,sizeof(exp_state_t));
    if(gui_app_count==0) gui_demo_register();
    DE.active=-1; DE.running=1;
    exp_force_redraw=0;
    /* v0.9 ignores legacy desktop.ui_scale and always uses physical pixels. */
    exp_ui_scale_pct=100;
    const char *ui_theme=sysconf_get("desktop","ui_theme");
    exp_theme_set(ui_theme&&!kstrcmp(ui_theme,"white")?EXP_THEME_WHITE:EXP_THEME_DARK,0);
    const char *wp=sysconf_get("desktop","wallpaper");
    int saved_wallpaper=wp&&wp[0]?kstrtoi(wp):1;wallpaper_id=(saved_wallpaper>=1&&saved_wallpaper<=EXP_BUILTIN_WALLPAPERS)?saved_wallpaper-1:0;
    const char *wallpaper_file=sysconf_get("desktop","wallpaper_file");
    if(wallpaper_file&&wallpaper_file[0]&&wallpaper_file_set(wallpaper_file,0)<0)exp_notify("Saved wallpaper could not be loaded",C_YELLOW);
    DE.last_clock=pit_get_ticks();
    DE.last_activity_tick=DE.last_clock;
    (void)vbe_double_buffer_enable();
    vbe_clear(C_BASE);
}

void exp_run(void){
    exp_init();
    full_redraw();
    exp_open_app(APP_TERMINAL,NULL);
    full_redraw();
    uint32_t last_ref=0;
    int last_mouse_x=-1,last_mouse_y=-1; uint8_t last_mouse_buttons=0;
    while(DE.running){
        clock_update();
        uint32_t now=pit_get_ticks();
        mouse_state_t mouse_sample;const mouse_state_t *mouse=mouse_snapshot(&mouse_sample)?&mouse_sample:NULL;
        if(mouse && mouse->available){
            int changed=(mouse->x!=last_mouse_x || mouse->y!=last_mouse_y || mouse->buttons!=last_mouse_buttons);
            int button_changed=mouse->buttons!=last_mouse_buttons;
            int meaningful_activity=saver_mouse_activity(mouse->x,mouse->y,mouse->buttons,last_mouse_x,last_mouse_y,last_mouse_buttons);
            mouse_state_t virtual_mouse=*mouse;
            virtual_mouse.x=EXP_UNSCALE(mouse->x); virtual_mouse.y=EXP_UNSCALE(mouse->y);
            if(meaningful_activity&&DE.saver_active){saver_note_activity();last_mouse_x=mouse->x;last_mouse_y=mouse->y;last_mouse_buttons=mouse->buttons;full_redraw();continue;}
            exp_mouse_update(&virtual_mouse,last_mouse_buttons);
            last_mouse_x=mouse->x; last_mouse_y=mouse->y; last_mouse_buttons=mouse->buttons;
            if(meaningful_activity)saver_note_activity();
            /* PS/2 mice can report far faster than software VBE can present in
             * a VM. Preserve click feedback, but coalesce motion-only frames. */
            if(changed&&(button_changed||now-mouse_last_tick>=3)){mouse_last_tick=now;full_redraw();continue;}
        }
        if(DE.saver_active){int wake=keyboard_poll();if(wake){saver_note_activity();full_redraw();continue;}saver_tick();draw_saver();vbe_present();__asm__ volatile("pause");continue;}
        if(now-DE.last_activity_tick>=saver_timeout_ticks()){saver_begin();draw_saver();vbe_present();continue;}
        if(now-last_ref>=10){
            int animated=0;last_ref=now;
            for(int i=0;i<DE.win_count;i++){
                if(DE.wins[i].app==APP_SYSMON)sysmon_upd(&DE.wins[i]);
                if(DE.wins[i].app==APP_SNAKE&&!DE.wins[i].minimized){snake_tick(&DE.wins[i]);animated=1;}
                if(DE.wins[i].app==APP_FLAPPY&&!DE.wins[i].minimized){flappy_tick(&DE.wins[i]);animated=1;}
                if(DE.wins[i].app==APP_TETRIS&&!DE.wins[i].minimized){tetris_tick(&DE.wins[i]);animated=1;}
                if(DE.wins[i].app==APP_CALENDAR&&!DE.wins[i].minimized&&calendar_refresh(&DE.wins[i]))animated=1;
                if(DE.wins[i].app==APP_TINYGL&&!DE.wins[i].minimized)animated=1;
            }
            if(animated)full_redraw();else {draw_taskbar();draw_notifs();draw_mouse_cursor();vbe_present();}
        }
        int k=keyboard_poll(); if(!k){__asm__ volatile("pause");continue;}
        saver_note_activity();
        int alt2=keyboard_alt(), ctrl2=keyboard_ctrl();
        /* Exit */
        if(ctrl2&&alt2&&(k=='\b'||k==8)){DE.running=0;break;}
        if(alt2&&k==KEY_F1){DE.running=0;break;}
        /* F2 launcher */
        if(k==KEY_F2){
            DE.launcher_open=!DE.launcher_open;
            DE.launcher_sel=0; full_redraw(); continue;
        }
        /* Alt+Tab */
        if(alt2&&k==KEY_TAB){
            if(!DE.alttab_open){DE.alttab_open=1;DE.alttab_sel=DE.active<0?0:DE.active;}
            if(DE.win_count>0) DE.alttab_sel=(DE.alttab_sel+1)%DE.win_count;
            full_redraw(); continue;
        }
        if(DE.alttab_open&&!alt2){
            win_to_front(DE.alttab_sel); DE.alttab_open=0; full_redraw(); continue;
        }
        /* Alt combos */
        if(alt2){
            if(k=='\n'||k=='\r'){exp_open_app(APP_TERMINAL,NULL);full_redraw();continue;}
            if(k=='f'||k=='F'){exp_open_app(APP_FILES,"/");full_redraw();continue;}
            if(k=='m'||k=='M'){exp_open_app(APP_SYSMON,NULL);full_redraw();continue;}
            if(k=='e'||k=='E'){exp_open_app(APP_NOTEPAD,NULL);full_redraw();continue;}
            if(k=='s'||k=='S'){exp_open_app(APP_SETTINGS,NULL);full_redraw();continue;}
            if(k=='p'||k=='P'){exp_open_app(APP_POWER,NULL);full_redraw();continue;}
            if(k==KEY_F4&&DE.win_count>0){win_close(DE.active);full_redraw();continue;}
            if(k==KEY_F5&&DE.active>=0&&DE.active<DE.win_count){
                win_max(&DE.wins[DE.active]); full_redraw(); continue;
            }
            /* Move window */
            if(DE.active>=0&&DE.active<DE.win_count&&!DE.wins[DE.active].maximized){
                exp_win_t *aw=&DE.wins[DE.active];
                if(k==KEY_LEFT){aw->x-=16;if(aw->x<0)aw->x=0;full_redraw();continue;}
                if(k==KEY_RIGHT){aw->x+=16;if(aw->x+aw->w>DE_SCR_W)aw->x=DE_SCR_W-aw->w;full_redraw();continue;}
                if(k==KEY_UP){aw->y-=16;if(aw->y<0)aw->y=0;full_redraw();continue;}
                if(k==KEY_DOWN){aw->y+=16;if(aw->y+aw->h>DE_SCR_H-DE_TASKBAR_H)aw->y=DE_SCR_H-DE_TASKBAR_H-aw->h;full_redraw();continue;}
            }
        }
        /* Launcher input */
        if(DE.launcher_open){
            if(k==KEY_ESC){DE.launcher_open=0;full_redraw();continue;}
            if(k==KEY_UP&&DE.launcher_sel>0){DE.launcher_sel--;draw_launcher();draw_mouse_cursor();vbe_present();continue;}
            if(k==KEY_DOWN&&DE.launcher_sel<NLA-1){DE.launcher_sel++;draw_launcher();draw_mouse_cursor();vbe_present();continue;}
            if(k>='1'&&k<='9'){
                DE.launcher_sel=k-'1'; DE.launcher_open=0;
                exp_open_app(LA[DE.launcher_sel].app,NULL); full_redraw(); continue;
            }
            if(k=='0'){
                DE.launcher_sel=NLA-1; DE.launcher_open=0;
                exp_open_app(LA[DE.launcher_sel].app,NULL); full_redraw(); continue;
            }
            if(k=='\n'||k=='\r'){
                DE.launcher_open=0;
                exp_open_app(LA[DE.launcher_sel].app,NULL);
                full_redraw(); continue;
            }
            continue;
        }
        /* Desktop icon navigation */
        if(DE.active<0||DE.win_count==0){
            if(k==KEY_TAB||k==KEY_RIGHT){
                icon_sel=(icon_sel+1)%NICONS;
                draw_desktop(); draw_taskbar(); draw_mouse_cursor(); vbe_present(); continue;
            }
            if(k==KEY_LEFT){
                icon_sel=(icon_sel-1+NICONS)%NICONS;
                draw_desktop(); draw_taskbar(); draw_mouse_cursor(); vbe_present(); continue;
            }
            if((k=='\n'||k=='\r')&&icon_sel>=0){
                exp_open_app(ICONS[icon_sel].app,NULL);
                full_redraw(); continue;
            }
        }
        /* Ctrl+Tab cycle */
        if(ctrl2&&k==KEY_TAB&&DE.win_count>0){
            DE.active=(DE.active+1)%DE.win_count;
            full_redraw(); continue;
        }
        /* Ctrl+number */
        if(ctrl2&&k>='1'&&k<='9'){
            int idx=k-'1';
            if(idx<DE.win_count){DE.active=idx;full_redraw();continue;}
        }
        /* Pass to active window */
        if(DE.active>=0&&DE.active<DE.win_count){
            int active_before=DE.active;
            exp_win_t *w=&DE.wins[DE.active];
            switch(w->app){
            case APP_TERMINAL: term_key(w,k); break;
            case APP_FILES:    fm_key(w,k);   break;
            case APP_EDITOR:
            case APP_VIEWER:   if(w->image_path[0]) image_viewer_key(w,k); else ed_key(w,k); break;
            case APP_ARCHIVEEX: archiveex_key(w,k); break;
            case APP_NOTEPAD:  notepad_key(w,k,ctrl2); break;
            case APP_JOURNAL:  notepad_key(w,k,ctrl2); break;
            case APP_EVENTLOG: eventlog_key(w,k); break;
            case APP_POWER:    power_key(w,k); break;
            case APP_SCREENSHOTS: screenshots_key(w,k); break;
            case APP_SETTINGS: settings_key(w,k); break;
            case APP_CALCULATOR: calc_key(w,k); break;
            case APP_TASKS:    tasks_key(w,k); break;
            case APP_CLOCK:    clock_key(w,k); break;
            case APP_CALENDAR: calendar_key(w,k); break;
            case APP_MINES:    mines_key(w,k); break;
            case APP_SNAKE:    snake_key(w,k); break;
            case APP_FLAPPY:   flappy_key(w,k); break;
            case APP_TETRIS:   tetris_key(w,k); break;
            case APP_PAINT:    paint_key(w,k); break;
            case APP_TINYGL:   tinygl_key(w,k); break;
            case APP_IMAGE_VIEWER: image_viewer_key(w,k); break;
            case APP_EXTERNAL:
                if(w->ext_slot>=0&&w->ext_slot<gui_app_count&&gui_apps[w->ext_slot].key){exp_gui_context_t ctx;gui_context(&ctx,w);gui_apps[w->ext_slot].key(&ctx,k,ctrl2,alt2);}break;
            default: break;
            }
            /* A terminal command may have opened/closed a window and changed
             * focus.  Redraw the complete z-order instead of painting the old
             * terminal over the newly created active window. */
            if(DE.active!=active_before){full_redraw();continue;}
            if(exp_force_redraw){exp_force_redraw=0;full_redraw();continue;}
            draw_chrome(w,1);
            draw_win_content(w);
            draw_taskbar();
            draw_notifs();
            draw_mouse_cursor();
            vbe_present();
        }
    }
    vbe_double_buffer_disable();
}
