#ifndef OSSDK_H
#define OSSDK_H
/*
 * ossdk.h — atmkoala v0.5 OS Development SDK
 *
 * Provides a clean API for extending or customizing atmkoala,
 * or as a reference base for writing a new OS on top.
 *
 * Sections:
 *   1. Kernel hooks         — register callbacks for boot/tick/syscall
 *   2. Custom commands      — add shell commands at runtime
 *   3. Custom drivers       — register device drivers
 *   4. Custom filesystems   — register VFS backends
 *   5. Interrupt hooking    — install custom ISR/IRQ handlers
 *   6. Memory regions       — query and manage physical memory
 *   7. Module system        — load/unload kernel modules from .tar.zst
 *   8. Theming API          — define custom color schemes
 *   9. Build info           — compile-time metadata
 */

#include "util.h"
#include "vga.h"
#include "vfs.h"
#include "keyboard.h"
#include "kmalloc.h"
#include <stdint.h>
#include <stddef.h>

/* ── Version ──────────────────────────────────────────────── */
#define ATMKOALA_VERSION_MAJOR  0
#define ATMKOALA_VERSION_MINOR  5
#define ATMKOALA_VERSION_PATCH  0
#define ATMKOALA_ARCH           "x86-64"

/* ── 1. Kernel hooks ──────────────────────────────────────── */
typedef void (*hook_fn_t)(void *ctx);

typedef enum {
    HOOK_BOOT_EARLY = 0,   /* before VFS/drivers */
    HOOK_BOOT_LATE,        /* after all init */
    HOOK_TICK,             /* every PIT tick (100Hz) */
    HOOK_PANIC,            /* on kpanic() */
    HOOK_SHUTDOWN,         /* on halt/reboot */
    HOOK_COUNT
} hook_type_t;

int  sdk_hook_register(hook_type_t type, hook_fn_t fn, void *ctx);
void sdk_hook_unregister(hook_type_t type, hook_fn_t fn);
void sdk_hook_fire(hook_type_t type);

/* ── 2. Custom shell commands ─────────────────────────────── */
typedef int (*cmd_fn_t)(int argc, char *argv[]);

typedef struct sdk_cmd {
    char      name[32];
    char      help[80];
    cmd_fn_t  fn;
    struct sdk_cmd *next;
} sdk_cmd_t;

int  sdk_cmd_register(const char *name, const char *help, cmd_fn_t fn);
void sdk_cmd_unregister(const char *name);
sdk_cmd_t *sdk_cmd_find(const char *name);
void sdk_cmd_list(void);       /* used by 'help' to show custom cmds */

/* ── 3. Custom drivers ────────────────────────────────────── */
typedef struct {
    char     name[32];
    int      (*init)(void);
    void     (*shutdown)(void);
    int      (*ioctl)(uint32_t cmd, void *arg);
    int      type;    /* 0=char 1=block 2=net */
} sdk_driver_t;

int  sdk_driver_register(sdk_driver_t *drv);
void sdk_driver_unregister(const char *name);
sdk_driver_t *sdk_driver_find(const char *name);

/* ── 4. Interrupt hooking ─────────────────────────────────── */
typedef void (*isr_fn_t)(uint8_t irq, void *ctx);

int  sdk_irq_install(uint8_t irq, isr_fn_t fn, void *ctx);
void sdk_irq_uninstall(uint8_t irq);

/* ── 5. Memory info ───────────────────────────────────────── */
typedef struct {
    uint32_t total_phys;   /* bytes */
    uint32_t heap_base;
    uint32_t heap_size;
    uint32_t heap_used;
    uint32_t heap_free;
    uint32_t kernel_start;
    uint32_t kernel_end;
} sdk_meminfo_t;

void sdk_meminfo(sdk_meminfo_t *out);

/* ── 6. Module system ─────────────────────────────────────── */
typedef struct {
    char     name[32];
    uint32_t load_addr;
    uint32_t size;
    int      (*init)(void);
    void     (*exit)(void);
} sdk_module_t;

int  sdk_module_load(const char *path);     /* load .cmod as kernel module */
void sdk_module_unload(const char *name);
void sdk_module_list(void);

/* ── 7. Custom color themes ───────────────────────────────── */
/*
 * Add a fully custom theme at runtime.
 * The scheme must be statically or heap-allocated — pointer is stored.
 * Returns assigned theme ID (≥ SCHEME_COUNT for runtime themes).
 *
 * Example:
 *   static color_scheme_t my_theme = {
 *       VGA_LIGHT_CYAN, VGA_BLACK,    // normal fg/bg
 *       VGA_WHITE,      VGA_LIGHT_RED, // prompt, error
 *       VGA_YELLOW,     VGA_WHITE,    // accent, header
 *       VGA_BLACK,      VGA_LIGHT_GREEN, // header_bg, success
 *       VGA_YELLOW,     VGA_DARK_GREY,   // warn, dim
 *       "mytheme"
 *   };
 *   int id = sdk_theme_register(&my_theme);
 *   terminal_set_scheme((scheme_id_t)id);
 */
int sdk_theme_register(const color_scheme_t *scheme);
const color_scheme_t *sdk_theme_get(int id);
int  sdk_theme_count(void);   /* total themes including runtime */

/* ── 8. OS personality / branding ────────────────────────── */
typedef struct {
    char os_name[64];
    char os_version[32];
    char os_codename[32];
    char os_author[64];
    char os_hostname[64];
    char os_motd[256];          /* message of the day */
    void (*print_logo)(void);   /* NULL = use default */
} sdk_personality_t;

void sdk_personality_set(const sdk_personality_t *p);
const sdk_personality_t *sdk_personality_get(void);

/* ── 9. Shell environment variables ──────────────────────── */
int   sdk_env_set(const char *key, const char *val);
const char *sdk_env_get(const char *key);
void  sdk_env_unset(const char *key);
void  sdk_env_list(void);

/* ── 10. Paging helpers (stub, ready to expand) ─────────── */
#define PAGE_SIZE       4096
#define PAGE_ALIGN(x)   (((x) + PAGE_SIZE-1) & ~(PAGE_SIZE-1))
#define PAGE_OF(x)      ((x) & ~(PAGE_SIZE-1))
#define VIRT_TO_PHYS(v) ((uint32_t)(v))   /* identity mapped */
#define PHYS_TO_VIRT(p) ((uint32_t)(p))

/* ── 11. CPUID ───────────────────────────────────────────── */
typedef struct {
    char vendor[13];
    char brand[49];
    uint32_t stepping, model, family, type;
    int has_fpu, has_mmx, has_sse, has_sse2, has_apic;
} sdk_cpuid_t;

void sdk_cpuid(sdk_cpuid_t *out);

/* ── 12. Timer / delay ───────────────────────────────────── */
void     sdk_sleep_ms(uint32_t ms);
uint32_t sdk_uptime_ms(void);
uint32_t sdk_ticks(void);

/* ── 13. Debug helpers ───────────────────────────────────── */
void sdk_hexdump(const void *buf, size_t len, uint32_t addr);
void sdk_backtrace(void);   /* prints EIP/ESP chain */

/* ── 14. Serial port (COM1 debug output) ─────────────────── */
void sdk_serial_init(void);
void sdk_serial_write(const char *s);
void sdk_serial_putc(char c);

#endif
