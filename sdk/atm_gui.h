#ifndef ATM_GUI_H
#define ATM_GUI_H

/*
 * ATM Native GUI ABI v1 — public, MIT project interface.
 *
 * This is a data-contract header for statically linked ATM applications.
 * v0.9 does not yet expose GUI syscalls or an IPC compositor endpoint; an
 * application must query capabilities and receive ENOSYS until that runtime
 * milestone lands. Keeping the types versioned now prevents Exp internals
 * from becoming an accidental third-party ABI.
 */

#include <stdint.h>
#include <stddef.h>

#define ATM_GUI_ABI_MAJOR 1u
#define ATM_GUI_ABI_MINOR 0u
#define ATM_GUI_ABI_VERSION ((ATM_GUI_ABI_MAJOR << 16) | ATM_GUI_ABI_MINOR)

#define ATM_GUI_MAKE_HANDLE(slot,generation) \
    ((((uint32_t)(generation) & 0xffffu) << 16) | ((uint32_t)(slot) & 0xffffu))
#define ATM_GUI_HANDLE_SLOT(handle)       ((uint16_t)((handle) & 0xffffu))
#define ATM_GUI_HANDLE_GENERATION(handle) ((uint16_t)(((handle) >> 16) & 0xffffu))

typedef uint32_t atm_gui_handle_t;
typedef uint32_t atm_gui_window_t;
typedef uint32_t atm_gui_surface_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t capabilities;
    uint32_t reserved;
} atm_gui_runtime_info_t;

enum {
    ATM_GUI_CAP_WINDOWS      = 1u << 0,
    ATM_GUI_CAP_SURFACES     = 1u << 1,
    ATM_GUI_CAP_DAMAGE       = 1u << 2,
    ATM_GUI_CAP_INPUT        = 1u << 3,
    ATM_GUI_CAP_CLIPBOARD    = 1u << 4,
    ATM_GUI_CAP_SHARED_MEMORY= 1u << 5,
    ATM_GUI_CAP_FONTS        = 1u << 6,
};

typedef enum {
    ATM_GUI_EVENT_NONE=0,
    ATM_GUI_EVENT_WINDOW_CLOSE,
    ATM_GUI_EVENT_WINDOW_FOCUS,
    ATM_GUI_EVENT_WINDOW_RESIZE,
    ATM_GUI_EVENT_KEY_DOWN,
    ATM_GUI_EVENT_KEY_UP,
    ATM_GUI_EVENT_POINTER_MOVE,
    ATM_GUI_EVENT_POINTER_BUTTON,
    ATM_GUI_EVENT_SURFACE_PRESENTED,
} atm_gui_event_type_t;

typedef struct {
    int32_t x, y, width, height;
} atm_gui_rect_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    atm_gui_window_t window;
    uint32_t serial;
    union {
        struct { int32_t width, height; } resize;
        struct { uint32_t keycode, modifiers; } key;
        struct { int32_t x, y; uint32_t buttons; } pointer;
        struct { uint32_t frame_serial; } presented;
    } data;
} atm_gui_event_t;

typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    int32_t width, height;
    const char *title_utf8;
    uint32_t reserved[4];
} atm_gui_window_desc_t;

typedef struct {
    uint32_t struct_size;
    uint32_t format;
    int32_t width, height;
    uint32_t stride_bytes;
    uint32_t flags;
} atm_gui_surface_desc_t;

enum {
    ATM_GUI_PIXFMT_XRGB8888 = 1u,
    ATM_GUI_PRESENT_ASYNC   = 1u << 0,
    ATM_GUI_PRESENT_VSYNC   = 1u << 1,
};

/* Future entry points. Until GUI IPC exists each returns -1 and sets errno
 * to ENOSYS; v0.9 clients can nevertheless compile against stable layouts. */
int atm_gui_runtime_info(atm_gui_runtime_info_t *out);
int atm_gui_window_create(const atm_gui_window_desc_t *desc, atm_gui_window_t *out);
int atm_gui_window_destroy(atm_gui_window_t window);
int atm_gui_surface_create(atm_gui_window_t window, const atm_gui_surface_desc_t *desc,
                           atm_gui_surface_t *out, void **pixels_out);
int atm_gui_surface_present(atm_gui_surface_t surface, const atm_gui_rect_t *damage,
                            uint32_t damage_count, uint32_t flags);
int atm_gui_event_next(atm_gui_event_t *out_event);

#endif
