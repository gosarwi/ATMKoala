/* kmod.c — atmkoala v0.5 Dynamic Kernel Module Loader */
#include "kmod.h"
#include "vfs.h"
#include "util.h"
#include "vga.h"
#include "kmalloc.h"
#include "ossdk.h"
#include "kernel_panic.h"
#include <stdint.h>
#include <stddef.h>

static kmod_t   modules[KMOD_MAX];
static int      mod_count = 0;
static kmod_api_t kapi;

/* kprintf declared in util.h */
extern void exp_notify(const char *msg, uint32_t color);

/* ── Build kernel API table ───────────────────────────────── */
void kmod_init(void) {
    kmemset(modules, 0, sizeof(modules));
    mod_count = 0;

    kapi.kmalloc           = kmalloc;
    kapi.kfree             = kfree;
    kapi.vfs_open          = vfs_open;
    kapi.vfs_read          = vfs_read;
    kapi.vfs_write         = vfs_write;
    kapi.vfs_close         = vfs_close;
    kapi.vfs_mkdir         = vfs_mkdir;
    kapi.terminal_write    = terminal_write;
    kapi.terminal_writeln  = terminal_writeln;
    kapi.terminal_set_color= terminal_set_color;
    kapi.kprintf           = kprintf;
    kapi.sdk_cmd_register  = (int(*)(const char*,const char*,void*))sdk_cmd_register;
    kapi.sdk_cmd_unregister= sdk_cmd_unregister;
    kapi.exp_notify         = (void(*)(const char*,uint32_t))exp_notify;
    kapi.edn_warn          = edn_warn;

    /* Create /modules directory */
    vfs_mkdir("/modules", 0755);
}

kmod_api_t *kmod_get_api(void) { return &kapi; }

/* ── XOR32 checksum ───────────────────────────────────────── */
static uint32_t xor32(const uint8_t *buf, uint32_t len) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < len; i++) s ^= buf[i];
    return s;
}

/* ── Load module from path ────────────────────────────────── */
int kmod_load(const char *path) {
    if (mod_count >= KMOD_MAX) {
        EDN("kmod_load: too many modules");
        return -1;
    }

    /* Open file */
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("kmod: %s: not found\n", path);
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -2;
    }

    /* Read header */
    kmod_header_t hdr;
    int n = vfs_read(fd, (uint8_t*)&hdr, sizeof(hdr));
    if (n < (int)sizeof(hdr)) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("kmod: %s: too small (header %d/%d bytes)\n", path, n, (int)sizeof(hdr));
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vfs_close(fd);
        return -3;
    }

    /* Validate magic */
    if (hdr.magic != KMOD_MAGIC) {
        terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("kmod: %s: bad magic 0x%x (expected 0x%x)\n",
                path, hdr.magic, KMOD_MAGIC);
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vfs_close(fd);
        return -4;
    }

    /* Check not already loaded */
    if (kmod_find(hdr.name)) {
        terminal_set_color(VGA_YELLOW, VGA_BLACK);
        kprintf("kmod: %s: already loaded\n", hdr.name);
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vfs_close(fd);
        return -5;
    }

    /* Allocate code buffer */
    uint8_t *code = (uint8_t*)kmalloc(hdr.code_size + 4);
    if (!code) {
        EDN("kmod_load: out of memory");
        vfs_close(fd);
        return -6;
    }

    /* Seek to code section and read */
    /* For simplicity: read full file */
    static uint8_t filebuf[65536];
    vfs_close(fd);
    fd = vfs_open(path, O_RDONLY, 0);
    int total = vfs_read(fd, filebuf, sizeof(filebuf)-1);
    vfs_close(fd);

    if (total < (int)(hdr.code_offset + hdr.code_size)) {
        kfree(code);
        EDN("kmod_load: file too short for code section");
        return -7;
    }

    kmemcpy(code, filebuf + hdr.code_offset, hdr.code_size);

    /* Verify checksum */
    uint32_t ck = xor32(code, hdr.code_size);
    if (ck != hdr.checksum) {
        terminal_set_color(VGA_YELLOW, VGA_BLACK);
        kprintf("kmod: %s: checksum mismatch (got 0x%x expected 0x%x) — loading anyway\n",
                hdr.name, ck, hdr.checksum);
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }

    /* Set up module entry */
    kmod_t *m = &modules[mod_count];
    m->hdr   = hdr;
    m->state = KMOD_STATE_LOADING;
    m->code  = code;
    m->load_addr = (uint32_t)(uintptr_t)code;

    /* Resolve init/exit */
    if (hdr.init_offset < hdr.code_size)
        m->init = (int(*)(kmod_api_t*))((uintptr_t)code + hdr.init_offset);
    else
        m->init = NULL;

    if (hdr.exit_offset && hdr.exit_offset < hdr.code_size)
        m->exit = (void(*)(void))((uintptr_t)code + hdr.exit_offset);
    else
        m->exit = NULL;

    /* Call init */
    if (m->init) {
        int ret = m->init(&kapi);
        if (ret != 0) {
            terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
            kprintf("kmod: %s: init() returned %d — load failed\n", hdr.name, ret);
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            kfree(code);
            m->state = KMOD_STATE_ERROR;
            return -8;
        }
    }

    m->state = KMOD_STATE_ACTIVE;
    mod_count++;

    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("kmod: loaded '%s' @ 0x%x (%u bytes)\n",
            hdr.name, m->load_addr, hdr.code_size);
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    return 0;
}

/* ── Unload module ────────────────────────────────────────── */
int kmod_unload(const char *name) {
    for (int i = 0; i < mod_count; i++) {
        if (kstrcmp(modules[i].hdr.name, name) == 0) {
            kmod_t *m = &modules[i];
            if (m->exit) m->exit();
            kfree(m->code);
            /* Shift array */
            for (int j = i; j < mod_count-1; j++) modules[j] = modules[j+1];
            mod_count--;
            terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            kprintf("kmod: unloaded '%s'\n", name);
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            return 0;
        }
    }
    kprintf("kmod: '%s' not found\n", name);
    return -1;
}

/* ── Find module ──────────────────────────────────────────── */
kmod_t *kmod_find(const char *name) {
    for (int i = 0; i < mod_count; i++)
        if (kstrcmp(modules[i].hdr.name, name) == 0)
            return &modules[i];
    return NULL;
}

int kmod_count(void) { return mod_count; }

/* ── List modules ─────────────────────────────────────────── */
void kmod_list(void) {
    if (mod_count == 0) {
        terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
        terminal_writeln("  (no modules loaded)");
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("  Name                  State   Addr       Size");
    terminal_writeln("  ─────────────────────────────────────────────");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < mod_count; i++) {
        kmod_t *m = &modules[i];
        const char *st[] = {"unloaded","loading","active","error"};
        kprintf("  %-20s  %-8s  0x%08x  %u B\n",
                m->hdr.name,
                st[(int)m->state < 4 ? m->state : 3],
                m->load_addr,
                m->hdr.code_size);
    }
}

/* ── Module info ──────────────────────────────────────────── */
void kmod_info(const char *name) {
    kmod_t *m = kmod_find(name);
    if (!m) { kprintf("kmod: '%s' not found\n", name); return; }
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("Module: %s\n", m->hdr.name);
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("  Author:  %s\n", m->hdr.author);
    kprintf("  Desc:    %s\n", m->hdr.description);
    kprintf("  Depends: %s\n", m->hdr.depends[0]?m->hdr.depends:"(none)");
    kprintf("  Address: 0x%x\n", m->load_addr);
    kprintf("  Size:    %u bytes\n", m->hdr.code_size);
    const char *st[]={"unloaded","loading","active","error"};
    kprintf("  State:   %s\n", st[(int)m->state<4?m->state:3]);
}
