#ifndef ATM_AWM_H
#define ATM_AWM_H

#include <stdint.h>

/* ATM Window Manager/Server (AWM): native local window-server API.
 * It borrows the client/server separation of X, but implements neither
 * X11 protocol nor Xlib/XCB/Linux binary compatibility. */
#define AWM_MAX_APPS   24
#define AWM_MAX_EVENTS 32
#define AWM_TITLE_MAX  40

typedef enum {
    AWM_EV_NONE=0, AWM_EV_KEY, AWM_EV_POINTER_MOVE,
    AWM_EV_POINTER_DOWN, AWM_EV_POINTER_UP, AWM_EV_EXPOSE,
    AWM_EV_CLOSE, AWM_EV_FOCUS
} awm_event_type_t;

typedef struct {
    awm_event_type_t type;
    int window_id;
    int x, y;
    int key;
    uint32_t buttons;
} awm_event_t;

typedef struct {
    int x, y, w, h;
    uint32_t fg, bg;
    int clip_x, clip_y, clip_w, clip_h;
} awm_surface_t;

typedef struct {
    int id;
    char id_name[24];
    char title[AWM_TITLE_MAX];
    char category[20];
    int builtin;
} awm_app_t;

typedef struct {
    int running;
    int protocol_major, protocol_minor;
    int screen_w, screen_h;
    int focused_window;
    int event_head, event_tail, event_count;
    awm_event_t events[AWM_MAX_EVENTS];
    int app_count;
    awm_app_t apps[AWM_MAX_APPS];
} awm_server_t;

extern awm_server_t g_awm;

void awm_init(void);
int awm_register_app(const char *id, const char *title, const char *category, int builtin);
int awm_event_push(const awm_event_t *ev);
int awm_event_pop(awm_event_t *ev);
void awm_focus(int window_id);
void awm_surface_init(awm_surface_t *s, int x, int y, int w, int h, uint32_t fg, uint32_t bg);
void awm_fill(awm_surface_t *s, int x, int y, int w, int h, uint32_t color);
void awm_frame(awm_surface_t *s, int x, int y, int w, int h, uint32_t color);
void awm_text(awm_surface_t *s, int x, int y, const char *utf8, uint32_t fg, uint32_t bg);
void awm_print_status(void);

#endif
