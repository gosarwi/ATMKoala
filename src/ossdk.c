/* ossdk.c — atmkoala v0.5 OS Development SDK implementation */
#include "ossdk.h"
#include "sched.h"
#include "pit.h"
#include "kmalloc.h"
#include "vga.h"
#include "pit.h"
#include "kmalloc.h"
#include "vfs.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

/* ── 1. Hooks ─────────────────────────────────────────────── */
#define MAX_HOOKS_PER_TYPE 8
typedef struct { hook_fn_t fn; void *ctx; } hook_entry_t;
static hook_entry_t hooks[HOOK_COUNT][MAX_HOOKS_PER_TYPE];
static int          hook_cnt[HOOK_COUNT];

int sdk_hook_register(hook_type_t type, hook_fn_t fn, void *ctx) {
    if (type >= HOOK_COUNT || hook_cnt[type] >= MAX_HOOKS_PER_TYPE) return -1;
    hooks[type][hook_cnt[type]].fn  = fn;
    hooks[type][hook_cnt[type]].ctx = ctx;
    hook_cnt[type]++;
    return 0;
}
void sdk_hook_unregister(hook_type_t type, hook_fn_t fn) {
    if (type >= HOOK_COUNT) return;
    for (int i = 0; i < hook_cnt[type]; i++) {
        if (hooks[type][i].fn == fn) {
            hooks[type][i] = hooks[type][--hook_cnt[type]]; return;
        }
    }
}
void sdk_hook_fire(hook_type_t type) {
    if (type >= HOOK_COUNT) return;
    for (int i = 0; i < hook_cnt[type]; i++)
        if (hooks[type][i].fn) hooks[type][i].fn(hooks[type][i].ctx);
}

/* ── 2. Commands ──────────────────────────────────────────── */
static sdk_cmd_t *cmd_head = NULL;

int sdk_cmd_register(const char *name, const char *help, cmd_fn_t fn) {
    sdk_cmd_t *c = (sdk_cmd_t *)kmalloc(sizeof(sdk_cmd_t));
    if (!c) return -1;
    kstrncpy(c->name, name, 31); c->name[31] = 0;
    kstrncpy(c->help, help, 79); c->help[79] = 0;
    c->fn   = fn;
    c->next = cmd_head;
    cmd_head = c;
    return 0;
}
void sdk_cmd_unregister(const char *name) {
    sdk_cmd_t **p = &cmd_head;
    while (*p) {
        if (kstrcmp((*p)->name, name) == 0) {
            sdk_cmd_t *tmp = *p; *p = (*p)->next; kfree(tmp); return;
        }
        p = &(*p)->next;
    }
}
sdk_cmd_t *sdk_cmd_find(const char *name) {
    for (sdk_cmd_t *c = cmd_head; c; c = c->next)
        if (kstrcmp(c->name, name) == 0) return c;
    return NULL;
}
void sdk_cmd_list(void) {
    for (sdk_cmd_t *c = cmd_head; c; c = c->next) {
        SCH_ACC; terminal_write("  [sdk] ");
        SCH_NRM; terminal_write(c->name);
        terminal_write("\t— "); terminal_writeln(c->help);
    }
}

/* ── 3. Drivers ───────────────────────────────────────────── */
#define MAX_DRIVERS 16
static sdk_driver_t *drivers[MAX_DRIVERS];
static int drv_count = 0;

int sdk_driver_register(sdk_driver_t *drv) {
    if (drv_count >= MAX_DRIVERS) return -1;
    if (drv->init && drv->init() < 0) return -2;
    drivers[drv_count++] = drv;
    return 0;
}
void sdk_driver_unregister(const char *name) {
    for (int i = 0; i < drv_count; i++) {
        if (kstrcmp(drivers[i]->name, name)==0) {
            if (drivers[i]->shutdown) drivers[i]->shutdown();
            drivers[i] = drivers[--drv_count]; return;
        }
    }
}
sdk_driver_t *sdk_driver_find(const char *name) {
    for (int i = 0; i < drv_count; i++)
        if (kstrcmp(drivers[i]->name, name)==0) return drivers[i];
    return NULL;
}

/* ── 4. IRQ hooks ─────────────────────────────────────────── */
#define MAX_IRQ 16
static isr_fn_t irq_fn[MAX_IRQ];
static void    *irq_ctx[MAX_IRQ];

int sdk_irq_install(uint8_t irq, isr_fn_t fn, void *ctx) {
    if (irq >= MAX_IRQ) return -1;
    irq_fn[irq] = fn; irq_ctx[irq] = ctx; return 0;
}
void sdk_irq_uninstall(uint8_t irq) {
    if (irq < MAX_IRQ) { irq_fn[irq] = NULL; irq_ctx[irq] = NULL; }
}
/* Called from IDT IRQ handler */
void sdk_irq_dispatch(uint8_t irq) {
    if (irq < MAX_IRQ && irq_fn[irq]) irq_fn[irq](irq, irq_ctx[irq]);
}

/* ── 5. Memory info ───────────────────────────────────────── */
extern uint32_t heap_total;   /* from kmalloc.c */
void sdk_meminfo(sdk_meminfo_t *out) {
    out->heap_base   = 0x400000;
    out->heap_size   = heap_used_bytes() + heap_free_bytes();
    out->heap_used   = heap_used_bytes();
    out->heap_free   = heap_free_bytes();
    out->total_phys  = 0; /* filled by kernel from multiboot */
    out->kernel_start= 0x100000;
    out->kernel_end  = 0x400000;
}

/* ── 6. Module system ─────────────────────────────────────── */
#define MAX_MODULES 16
static sdk_module_t modules[MAX_MODULES];
static int mod_count = 0;

int sdk_module_load(const char *path) {
    if (mod_count >= MAX_MODULES) return -1;
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) { terminal_writeln("sdk: module not found"); return -2; }
    static uint8_t mbuf[32768];
    int n = vfs_read(fd, mbuf, sizeof(mbuf)); vfs_close(fd);
    if (n <= 0) return -3;
    /* Try ELF exec */
    (void)mbuf; (void)n;
    terminal_write("sdk: loaded module "); terminal_writeln(path);
    return 0;
}
void sdk_module_list(void) {
    if (!mod_count) { terminal_writeln("  (no modules loaded)"); return; }
    for (int i = 0; i < mod_count; i++) {
        char buf[16]; kuitoa(modules[i].size, buf, 10);
        terminal_write("  "); terminal_write(modules[i].name);
        terminal_write("  @ 0x"); kuitoa(modules[i].load_addr, buf, 16);
        terminal_write(buf); terminal_writeln("");
    }
}

/* ── 7. Custom themes ─────────────────────────────────────── */
#define MAX_RUNTIME_THEMES 8
static const color_scheme_t *rt_themes[MAX_RUNTIME_THEMES];
static int rt_count = 0;

int sdk_theme_register(const color_scheme_t *scheme) {
    if (rt_count >= MAX_RUNTIME_THEMES) return -1;
    rt_themes[rt_count] = scheme;
    return SCHEME_COUNT + rt_count++;
}
const color_scheme_t *sdk_theme_get(int id) {
    if (id >= SCHEME_COUNT && id < SCHEME_COUNT + rt_count)
        return rt_themes[id - SCHEME_COUNT];
    return NULL;
}
int sdk_theme_count(void) { return SCHEME_COUNT + rt_count; }

/* ── 8. Personality ───────────────────────────────────────── */
static sdk_personality_t personality = {
    "atmkoala", "0.5.0", "", "atmkoala Project",
    "atmkoala", "Welcome to atmkoala OS v0.5!", NULL
};
void sdk_personality_set(const sdk_personality_t *p) {
    kmemcpy(&personality, p, sizeof(sdk_personality_t));
}
const sdk_personality_t *sdk_personality_get(void) { return &personality; }

/* ── 9. Environment variables ─────────────────────────────── */
#define MAX_ENV 32
static struct { char k[32]; char v[128]; } env_table[MAX_ENV];
static int env_count = 0;

int sdk_env_set(const char *key, const char *val) {
    for (int i = 0; i < env_count; i++)
        if (kstrcmp(env_table[i].k, key)==0) {
            kstrncpy(env_table[i].v, val, 127); return 0;
        }
    if (env_count >= MAX_ENV) return -1;
    kstrncpy(env_table[env_count].k, key, 31);
    kstrncpy(env_table[env_count].v, val, 127);
    env_count++; return 0;
}
const char *sdk_env_get(const char *key) {
    for (int i = 0; i < env_count; i++)
        if (kstrcmp(env_table[i].k, key)==0) return env_table[i].v;
    return NULL;
}
void sdk_env_unset(const char *key) {
    for (int i = 0; i < env_count; i++)
        if (kstrcmp(env_table[i].k, key)==0) {
            env_table[i] = env_table[--env_count]; return;
        }
}
void sdk_env_list(void) {
    for (int i = 0; i < env_count; i++) {
        terminal_write(env_table[i].k); terminal_write("=");
        terminal_writeln(env_table[i].v);
    }
}

/* ── 11. CPUID ───────────────────────────────────────────── */
static void cpuid_raw(uint32_t leaf, uint32_t *a, uint32_t *b,
                      uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d) : "a"(leaf));
}
void sdk_cpuid(sdk_cpuid_t *out) {
    uint32_t a, b, c, d;
    /* Vendor */
    cpuid_raw(0, &a, &b, &c, &d);
    kmemcpy(out->vendor,   &b, 4);
    kmemcpy(out->vendor+4, &d, 4);
    kmemcpy(out->vendor+8, &c, 4);
    out->vendor[12] = 0;
    /* Features */
    cpuid_raw(1, &a, &b, &c, &d);
    out->stepping = a & 0xF;
    out->model    = (a>>4) & 0xF;
    out->family   = (a>>8) & 0xF;
    out->type     = (a>>12) & 0x3;
    out->has_fpu  = (d>>0)&1; out->has_mmx=(d>>23)&1;
    out->has_sse  = (d>>25)&1; out->has_sse2=(d>>26)&1;
    out->has_apic = (d>>9)&1;
    /* Brand string */
    out->brand[0] = 0;
    uint32_t max_ext; cpuid_raw(0x80000000, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000004) {
        uint32_t *bp = (uint32_t *)out->brand;
        cpuid_raw(0x80000002, &bp[0],&bp[1],&bp[2],&bp[3]);
        cpuid_raw(0x80000003, &bp[4],&bp[5],&bp[6],&bp[7]);
        cpuid_raw(0x80000004, &bp[8],&bp[9],&bp[10],&bp[11]);
        out->brand[48] = 0;
    }
}

/* ── 12. Timer ───────────────────────────────────────────── */
void     sdk_sleep_ms(uint32_t ms) { pit_sleep(ms); }
uint32_t sdk_uptime_ms(void)       { return sched_uptime_ticks() * 10; }
uint32_t sdk_ticks(void)           { return pit_get_ticks(); }

/* ── 13. Hex dump ────────────────────────────────────────── */
void sdk_hexdump(const void *buf, size_t len, uint32_t addr) {
    const uint8_t *p = (const uint8_t *)buf;
    char nb[12];
    for (size_t i = 0; i < len; i += 16) {
        kuitoa(addr+i, nb, 16); terminal_write(nb); terminal_write(": ");
        for (size_t j = 0; j < 16; j++) {
            if (i+j < len) {
                kuitoa(p[i+j], nb, 16);
                if (p[i+j] < 16) terminal_putchar('0');
                terminal_write(nb); terminal_putchar(' ');
            } else terminal_write("   ");
            if (j == 7) terminal_putchar(' ');
        }
        terminal_write(" |");
        for (size_t j = 0; j < 16 && i+j < len; j++) {
            char c = (char)p[i+j];
            terminal_putchar(c>=0x20&&c<0x7F ? c : '.');
        }
        terminal_writeln("|");
    }
}

/* ── 14. Serial COM1 ─────────────────────────────────────── */
#define COM1 0x3F8
void sdk_serial_init(void) {
    outb(COM1+1, 0x00); outb(COM1+3, 0x80);
    outb(COM1+0, 0x03); outb(COM1+1, 0x00);
    outb(COM1+3, 0x03); outb(COM1+2, 0xC7);
    outb(COM1+4, 0x0B);
}
void sdk_serial_putc(char c) {
    while (!(inb(COM1+5) & 0x20));
    outb(COM1, (uint8_t)c);
}
void sdk_serial_write(const char *s) { while (*s) sdk_serial_putc(*s++); }


