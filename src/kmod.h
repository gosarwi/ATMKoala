#ifndef KMOD_H
#define KMOD_H
/*
 * kmod.h — atmkoala v0.5 Dynamic Kernel Modules
 *
 * Modules are ELF32 position-independent objects loaded from VFS.
 * Each module exports a kmod_info_t struct at a fixed symbol.
 *
 * Module file format: .cmod (CaramelY Kernel Module)
 *   - Actually a flat binary (not real ELF relocation — simplified)
 *   - Header at offset 0: kmod_header_t
 *   - Code follows immediately
 *   - Module calls kmod_register() from its init()
 *
 * Usage:
 *   kmod load /modules/hello.cmod
 *   kmod list
 *   kmod unload hello
 *   kmod info hello
 *
 * Writing a module:
 *   See tools/cmod_example.c for a template.
 */
#include <stdint.h>
#include <stddef.h>

#define KMOD_MAGIC    0xCAFE0D01
#define KMOD_VERSION  1
#define KMOD_NAME_LEN 32
#define KMOD_MAX      16

/* Module states */
typedef enum {
    KMOD_STATE_UNLOADED = 0,
    KMOD_STATE_LOADING,
    KMOD_STATE_ACTIVE,
    KMOD_STATE_ERROR,
} kmod_state_t;

/* Module header (first 64 bytes of .cmod file) */
typedef struct __attribute__((packed)) {
    uint32_t  magic;          /* KMOD_MAGIC */
    uint8_t   version;
    uint8_t   reserved[3];
    char      name[KMOD_NAME_LEN];
    char      author[32];
    char      description[64];
    char      depends[32];    /* comma-separated module names */
    uint32_t  init_offset;    /* offset of init() from start of code */
    uint32_t  exit_offset;    /* offset of exit() — 0 if none */
    uint32_t  code_offset;    /* offset of code section from file start */
    uint32_t  code_size;
    uint32_t  checksum;       /* XOR32 of code section */
} kmod_header_t;

/* Kernel API table passed to modules */
typedef struct {
    /* Memory */
    void  *(*kmalloc)(size_t n);
    void   (*kfree)(void *p);
    /* VFS */
    int     (*vfs_open)(const char *path, uint32_t flags, uint32_t mode);
    int64_t (*vfs_read)(int fd, void *buf, uint64_t len);
    int64_t (*vfs_write)(int fd, const void *buf, uint64_t len);
    void    (*vfs_close)(int fd);
    int     (*vfs_mkdir)(const char *path, uint32_t mode);
    /* Console */
    void   (*terminal_write)(const char *s);
    void   (*terminal_writeln)(const char *s);
    void   (*terminal_set_color)(uint8_t fg, uint8_t bg);
    void   (*kprintf)(const char *fmt, ...);
    /* SDK */
    int    (*sdk_cmd_register)(const char *name, const char *help, void *fn);
    void   (*sdk_cmd_unregister)(const char *name);
    void   (*exp_notify)(const char *msg, uint32_t color);
    /* Panic */
    void   (*edn_warn)(const char *msg, const char *file, int line);
} kmod_api_t;

/* Loaded module instance */
typedef struct {
    kmod_header_t hdr;
    kmod_state_t  state;
    uint8_t      *code;       /* allocated code buffer */
    uint32_t      load_addr;
    /* init/exit function pointers */
    int  (*init)(kmod_api_t *api);
    void (*exit)(void);
} kmod_t;

/* ── API ──────────────────────────────────────────────────── */
void  kmod_init(void);
int   kmod_load(const char *path);
int   kmod_unload(const char *name);
kmod_t *kmod_find(const char *name);
void  kmod_list(void);
void  kmod_info(const char *name);
int   kmod_count(void);

/* Get kernel API table */
kmod_api_t *kmod_get_api(void);

#endif /* KMOD_H */
