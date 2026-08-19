#include "awm.h"
#include "vbe.h"
#include "ttf.h"
#include "util.h"
#include "vga.h"

awm_server_t g_awm;

static int clip_x(const awm_surface_t *s, int x) { return x < s->clip_x ? s->clip_x : x; }
static int clip_y(const awm_surface_t *s, int y) { return y < s->clip_y ? s->clip_y : y; }

void awm_init(void) {
    kmemset(&g_awm, 0, sizeof(g_awm));
    g_awm.running = 1;
    g_awm.protocol_major = 1;
    g_awm.protocol_minor = 0;
    g_awm.screen_w = (int)vbe.width;
    g_awm.screen_h = (int)vbe.height;
    g_awm.focused_window = -1;
    awm_register_app("terminal", "Terminal", "System", 1);
    awm_register_app("files", "Files", "System", 1);
    awm_register_app("settings", "Settings", "System", 1);
    awm_register_app("tinygl", "TinyGL Demo", "Graphics", 1);
    awm_register_app("mines", "Minesweeper", "Games", 1);
    awm_register_app("snake", "Snake", "Games", 1);
}

int awm_register_app(const char *id, const char *title, const char *category, int builtin) {
    if (!id || !title || !category || !id[0] || g_awm.app_count >= AWM_MAX_APPS) return -1;
    for (int i = 0; i < g_awm.app_count; i++) if (!kstrcmp(g_awm.apps[i].id_name, id)) return -2;
    awm_app_t *a = &g_awm.apps[g_awm.app_count];
    kmemset(a, 0, sizeof(*a));
    a->id = g_awm.app_count + 1;
    kstrncpy(a->id_name, id, sizeof(a->id_name) - 1);
    kstrncpy(a->title, title, sizeof(a->title) - 1);
    kstrncpy(a->category, category, sizeof(a->category) - 1);
    a->builtin = builtin ? 1 : 0;
    return g_awm.app_count++;
}

int awm_event_push(const awm_event_t *ev) {
    if (!ev || g_awm.event_count >= AWM_MAX_EVENTS) return -1;
    g_awm.events[g_awm.event_tail] = *ev;
    g_awm.event_tail = (g_awm.event_tail + 1) % AWM_MAX_EVENTS;
    g_awm.event_count++;
    return 0;
}

int awm_event_pop(awm_event_t *ev) {
    if (!ev || g_awm.event_count == 0) return -1;
    *ev = g_awm.events[g_awm.event_head];
    g_awm.event_head = (g_awm.event_head + 1) % AWM_MAX_EVENTS;
    g_awm.event_count--;
    return 0;
}

void awm_focus(int window_id) {
    if (g_awm.focused_window == window_id) return;
    g_awm.focused_window = window_id;
    awm_event_t ev = { .type = AWM_EV_FOCUS, .window_id = window_id };
    awm_event_push(&ev);
}

void awm_surface_init(awm_surface_t *s, int x, int y, int w, int h, uint32_t fg, uint32_t bg) {
    if (!s) return;
    s->x=x; s->y=y; s->w=w; s->h=h; s->fg=fg; s->bg=bg;
    s->clip_x=x; s->clip_y=y; s->clip_w=w; s->clip_h=h;
}

void awm_fill(awm_surface_t *s, int x, int y, int w, int h, uint32_t color) {
    if (!s || !vbe.active || w <= 0 || h <= 0) return;
    int x0=clip_x(s,x), y0=clip_y(s,y);
    int x1=x+w, y1=y+h;
    int cx1=s->clip_x+s->clip_w, cy1=s->clip_y+s->clip_h;
    if (x1>cx1) x1=cx1; if (y1>cy1) y1=cy1;
    if (x1>x0 && y1>y0) vbe_fill_rect(x0,y0,x1-x0,y1-y0,color);
}

void awm_frame(awm_surface_t *s, int x, int y, int w, int h, uint32_t color) {
    if (!s || w < 2 || h < 2) return;
    awm_fill(s,x,y,w,1,color); awm_fill(s,x,y+h-1,w,1,color);
    awm_fill(s,x,y,1,h,color); awm_fill(s,x+w-1,y,1,h,color);
}

void awm_text(awm_surface_t *s, int x, int y, const char *utf8, uint32_t fg, uint32_t bg) {
    if (!s || !utf8 || x < s->clip_x || y < s->clip_y || x >= s->clip_x+s->clip_w || y >= s->clip_y+s->clip_h) return;
    if (ttf_loaded()) ttf_render_string(x,y,utf8,fg,bg); else vbe_puts(x,y,utf8,fg,bg);
}

void awm_print_status(void) {
    char n[12];
    kprintf("awm: native local window server v%d.%d, screen %dx%d\n", g_awm.protocol_major, g_awm.protocol_minor, g_awm.screen_w, g_awm.screen_h);
    kprintf("     X11 protocol: not implemented; client ABI: ATMKoala native\n");
    kprintf("     apps: "); kuitoa((uint32_t)g_awm.app_count,n,10); terminal_writeln(n);
    for (int i=0;i<g_awm.app_count;i++) kprintf("       %s  [%s]\n",g_awm.apps[i].title,g_awm.apps[i].category);
}
