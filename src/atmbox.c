#include "atmbox.h"
#include "vfs.h"
#include "util.h"
#include "vga.h"
#include "vbe.h"
#include <stdint.h>
#include <stddef.h>

static void outc(char c) {
    if (vbe.active) vbe_console_putchar(c); else terminal_putchar(c);
}
static void out(const char *s) {
    if (vbe.active) vbe_console_write(s); else terminal_write(s);
}
static void outln(const char *s) { out(s); outc('\n'); }
static void err(const char *applet, const char *message) {
    terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
    out("atm-box "); out(applet); out(": "); outln(message);
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void atmbox_print_applets(void) {
    outln("atm-box native applets:");
    outln("  basename dirname cat cp mv rm mkdir touch head wc echo true false");
    outln("  Usage: atm-box <applet> [arguments]");
}

static const char *path_base(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
    return base;
}
static int print_parent(const char *path) {
    char outbuf[VFS_PATH_MAX];
    size_t len = kstrlen(path);
    if (!len) { outln("."); return 0; }
    while (len > 1 && path[len-1] == '/') len--;
    size_t cut = len;
    while (cut > 0 && path[cut-1] != '/') cut--;
    if (!cut) kstrcpy(outbuf, ".");
    else if (cut == 1) kstrcpy(outbuf, "/");
    else { kmemcpy(outbuf, path, cut-1); outbuf[cut-1] = 0; }
    outln(outbuf); return 0;
}
static int copy_file(const char *src, const char *dst) {
    int in = vfs_open(src, O_RDONLY, 0);
    if (in < 0) return -1;
    int outfd = vfs_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (outfd < 0) { vfs_close(in); return -2; }
    uint8_t buf[512]; int64_t n;
    while ((n = vfs_read(in, buf, sizeof(buf))) > 0) {
        if (vfs_write(outfd, buf, (uint64_t)n) != n) { vfs_close(in); vfs_close(outfd); return -3; }
    }
    vfs_close(in); vfs_close(outfd);
    return n < 0 ? -4 : 0;
}
static int mkdir_parents(const char *path) {
    char part[VFS_PATH_MAX];
    if (!path || path[0] != '/' || kstrlen(path) >= sizeof(part)) return -1;
    part[0] = 0;
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *start = p; while (*p && *p != '/') p++;
        if (kstrlen(part) + (kstrlen(part) ? 1u : 0u) + (size_t)(p-start) + 1u >= sizeof(part)) return -1;
        kstrcat(part, "/");
        size_t used = kstrlen(part); kmemcpy(part + used, start, (size_t)(p-start)); part[used + (size_t)(p-start)] = 0;
        vfs_mkdir(part, 0755);
    }
    return 0;
}
static int app_cat(int argc, char *argv[]) {
    if (argc < 3) { err("cat", "usage: atm-box cat <file>"); return -1; }
    int fd = vfs_open(argv[2], O_RDONLY, 0); if (fd < 0) { err("cat", "not found"); return -1; }
    uint8_t buf[512]; int64_t n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) for (int64_t i = 0; i < n; i++) outc((char)buf[i]);
    vfs_close(fd); outc('\n'); return n < 0 ? -1 : 0;
}
static int app_head(int argc, char *argv[]) {
    if (argc < 3) { err("head", "usage: atm-box head [-n N] <file>"); return -1; }
    int arg = 2, lines = 10;
    if (argc >= 5 && !kstrcmp(argv[2], "-n")) { lines = kstrtoi(argv[3]); arg = 4; }
    int fd = vfs_open(argv[arg], O_RDONLY, 0); if (fd < 0) { err("head", "not found"); return -1; }
    uint8_t buf[256]; int64_t n;
    while (lines > 0 && (n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (int64_t i = 0; i < n && lines > 0; i++) { outc((char)buf[i]); if (buf[i] == '\n') lines--; }
    }
    vfs_close(fd); return 0;
}
static int app_wc(int argc, char *argv[]) {
    if (argc < 3) { err("wc", "usage: atm-box wc <file>"); return -1; }
    int fd = vfs_open(argv[2], O_RDONLY, 0); if (fd < 0) { err("wc", "not found"); return -1; }
    uint8_t buf[512]; int64_t n; uint32_t lines=0, words=0, bytes=0; int word=0;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) for (int64_t i=0;i<n;i++) {
        uint8_t c=buf[i]; bytes++; if(c=='\n') lines++;
        if(c==' '||c=='\t'||c=='\n'||c=='\r') word=0; else if(!word){words++;word=1;}
    }
    vfs_close(fd); char b[16]; kuitoa(lines,b,10); out(b); out(" "); kuitoa(words,b,10); out(b); out(" "); kuitoa(bytes,b,10); out(b); out(" "); outln(argv[2]); return 0;
}

int atmbox_dispatch(int argc, char *argv[]) {
    if (argc < 2 || !kstrcmp(argv[1], "--help") || !kstrcmp(argv[1], "help") || !kstrcmp(argv[1], "--list")) { atmbox_print_applets(); return 0; }
    const char *app = argv[1];
    if (!kstrcmp(app,"true")) return 0;
    if (!kstrcmp(app,"false")) return 1;
    if (!kstrcmp(app,"echo")) { for(int i=2;i<argc;i++){ if(i>2)outc(' '); out(argv[i]); } outc('\n'); return 0; }
    if (!kstrcmp(app,"basename")) { if(argc<3){err(app,"usage: atm-box basename <path>");return -1;} outln(path_base(argv[2])); return 0; }
    if (!kstrcmp(app,"dirname")) { if(argc<3){err(app,"usage: atm-box dirname <path>");return -1;} return print_parent(argv[2]); }
    if (!kstrcmp(app,"cat")) return app_cat(argc,argv);
    if (!kstrcmp(app,"head")) return app_head(argc,argv);
    if (!kstrcmp(app,"wc")) return app_wc(argc,argv);
    if (!kstrcmp(app,"touch")) { if(argc<3){err(app,"usage: atm-box touch <file>");return -1;} int fd=vfs_open(argv[2],O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0){err(app,"cannot create file");return -1;} vfs_close(fd); return 0; }
    if (!kstrcmp(app,"mkdir")) { int i=2, pflag=0; if(argc>2&&!kstrcmp(argv[2],"-p")){pflag=1;i++;} if(i>=argc){err(app,"usage: atm-box mkdir [-p] <dir>");return -1;} int r=pflag?mkdir_parents(argv[i]):vfs_mkdir(argv[i],0755); if(r<0){err(app,"cannot create directory");return -1;} return 0; }
    if (!kstrcmp(app,"rm")) { if(argc<3){err(app,"usage: atm-box rm <file>");return -1;} if(vfs_unlink(argv[2])<0){err(app,"cannot remove file");return -1;} return 0; }
    if (!kstrcmp(app,"cp")) { if(argc<4){err(app,"usage: atm-box cp <source> <dest>");return -1;} if(copy_file(argv[2],argv[3])<0){err(app,"copy failed");return -1;} return 0; }
    if (!kstrcmp(app,"mv")) { if(argc<4){err(app,"usage: atm-box mv <source> <dest>");return -1;} if(vfs_rename(argv[2],argv[3])<0){err(app,"rename failed");return -1;} return 0; }
    err(app,"applet not available"); return -1;
}
