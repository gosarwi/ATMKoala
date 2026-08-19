#include "atmbox.h"
#include "vfs.h"
#include "util.h"
#include "vga.h"
#include "vbe.h"
#include "sched.h"
#include "mesa_foundation.h"
#include "kmalloc.h"
#include "disk.h"
#include <stdint.h>
#include <stddef.h>

#define ATMBOX_LIST_MAX 256u
#define ATMBOX_DUMP_MAX 4096u

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
static void outu(uint64_t value) {
    char buf[24]; ku64toa(value,buf,10); out(buf);
}
static void out_hex_byte(uint8_t value) {
    static const char hex[]="0123456789abcdef";
    char buf[3]; buf[0]=hex[value>>4]; buf[1]=hex[value&15]; buf[2]=0; out(buf);
}
static void out_hex64(uint64_t value) {
    static const char hex[]="0123456789abcdef";
    char buf[17];
    for(int i=15;i>=0;i--){buf[i]=hex[value&15];value>>=4;}
    buf[16]=0; out(buf);
}

void atmbox_print_applets(void) {
    outln("atm-box native Toybox-compatible applets:");
    outln("  basename cat cmp cp dirname echo false free gpuinfo head hexdump id");
    outln("  iostat kill ls mkdir mv ps rm stat touch true uname uptime wc");
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
static int join_path(char outbuf[VFS_PATH_MAX],const char *dir,const char *name){
    if(!dir || !name || !dir[0] || kstrlen(dir)+kstrlen(name)+2>VFS_PATH_MAX)return -1;
    kstrcpy(outbuf,dir);
    if(kstrcmp(dir,"/"))kstrcat(outbuf,"/");
    kstrcat(outbuf,name);
    return 0;
}
static char entry_kind(uint8_t type){
    switch(type){
        case DT_DIR:return 'd'; case DT_LNK:return 'l'; case DT_CHR:return 'c';
        case DT_BLK:return 'b'; case DT_FIFO:return 'p'; case DT_SOCK:return 's';
        case DT_REG:return '-'; default:return '?';
    }
}
static int copy_file(const char *src, const char *dst) {
    int in = vfs_open(src, O_RDONLY, 0);
    if (in < 0) return -1;
    int outfd = vfs_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
        if(vfs_mkdir(part, 0755)<0 && !vfs_lookup(part)) return -1;
    }
    return 0;
}
static int app_cat(int argc, char *argv[]) {
    if (argc < 3) { err("cat", "usage: atm-box cat <file>"); return -1; }
    int fd = vfs_open(argv[2], O_RDONLY, 0); if (fd < 0) { err("cat", "not found"); return -1; }
    uint8_t buf[512]; int64_t n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) for (int64_t i = 0; i < n; i++) outc((char)buf[i]);
    vfs_close(fd); return n < 0 ? -1 : 0;
}
static int app_head(int argc, char *argv[]) {
    if (argc < 3) { err("head", "usage: atm-box head [-n N] <file>"); return -1; }
    int arg = 2, lines = 10;
    if (argc >= 5 && !kstrcmp(argv[2], "-n")) { lines = kstrtoi(argv[3]); arg = 4; }
    if(lines<0 || arg>=argc){err("head","invalid arguments");return -1;}
    int fd = vfs_open(argv[arg], O_RDONLY, 0); if (fd < 0) { err("head", "not found"); return -1; }
    uint8_t buf[256]; int64_t n;
    while (lines > 0 && (n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (int64_t i = 0; i < n && lines > 0; i++) { outc((char)buf[i]); if (buf[i] == '\n') lines--; }
    }
    vfs_close(fd); return n < 0 ? -1 : 0;
}
static int app_wc(int argc, char *argv[]) {
    if (argc < 3) { err("wc", "usage: atm-box wc <file>"); return -1; }
    int fd = vfs_open(argv[2], O_RDONLY, 0); if (fd < 0) { err("wc", "not found"); return -1; }
    uint8_t buf[512]; int64_t n; uint32_t lines=0, words=0, bytes=0; int word=0;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) for (int64_t i=0;i<n;i++) {
        uint8_t c=buf[i]; bytes++; if(c=='\n') lines++;
        if(c==' '||c=='\t'||c=='\n'||c=='\r') word=0; else if(!word){words++;word=1;}
    }
    vfs_close(fd); outu(lines); out(" "); outu(words); out(" "); outu(bytes); out(" "); outln(argv[2]); return n<0?-1:0;
}
static int app_ls(int argc,char *argv[]){
    int arg=2,longfmt=0;
    if(arg<argc && !kstrcmp(argv[arg],"-l")){longfmt=1;arg++;}
    if(arg+1<argc){err("ls","usage: atm-box ls [-l] [directory]");return -1;}
    const char *path=arg<argc?argv[arg]:"/";
    DIR_t *dir=vfs_opendir(path); if(!dir){err("ls","not a directory or not found");return -1;}
    uint32_t count=0; vfs_dirent_t *ent;
    while(count<ATMBOX_LIST_MAX && (ent=vfs_readdir_next(dir))){
        if(longfmt){
            char full[VFS_PATH_MAX]; vfs_stat_t st;
            if(join_path(full,path,ent->name)==0 && vfs_stat(full,&st)==0){
                outc(entry_kind(ent->d_type)); out(" mode="); outu(st.st_mode&0777u);
                out(" uid="); outu(st.st_uid); out(" gid="); outu(st.st_gid);
                out(" size="); outu(st.st_size); out(" ");
            }
        }
        outln(ent->name); count++;
    }
    vfs_closedir(dir);
    if(count==ATMBOX_LIST_MAX) outln("atm-box ls: output capped at 256 entries");
    return 0;
}
static int app_stat(int argc,char *argv[]){
    if(argc!=3){err("stat","usage: atm-box stat <path>");return -1;}
    vfs_stat_t st; if(vfs_stat(argv[2],&st)<0){err("stat","not found");return -1;}
    out("Path: ");outln(argv[2]); out("Inode: ");outu(st.st_ino);out("  Mode: ");outu(st.st_mode);out("  Links: ");outu(st.st_nlink);outc('\n');
    out("UID: ");outu(st.st_uid);out("  GID: ");outu(st.st_gid);out("  Size: ");outu(st.st_size);out("  Blocks: ");outu(st.st_blocks);outc('\n');
    return 0;
}
static int app_cmp(int argc,char *argv[]){
    if(argc!=4){err("cmp","usage: atm-box cmp <file1> <file2>");return -1;}
    int a=vfs_open(argv[2],O_RDONLY,0),b=vfs_open(argv[3],O_RDONLY,0);if(a<0||b<0){if(a>=0)vfs_close(a);if(b>=0)vfs_close(b);err("cmp","cannot open file");return -1;}
    uint8_t ba[256],bb[256]; uint64_t off=0; int result=0;
    for(;;){int64_t na=vfs_read(a,ba,sizeof(ba)),nb=vfs_read(b,bb,sizeof(bb));if(na<0||nb<0){result=-1;break;}if(na!=nb){result=1;break;}if(!na)break;for(int64_t i=0;i<na;i++)if(ba[i]!=bb[i]){off+=(uint64_t)i;result=1;goto finish;}off+=(uint64_t)na;}
finish:
    vfs_close(a);vfs_close(b);if(result==0){outln("files are identical");return 0;}if(result>0){out("files differ at byte ");outu(off);outc('\n');return 1;}err("cmp","read failed");return -1;
}
static int app_hexdump(int argc,char *argv[]){
    if(argc!=3){err("hexdump","usage: atm-box hexdump <file>");return -1;}
    int fd=vfs_open(argv[2],O_RDONLY,0);if(fd<0){err("hexdump","not found");return -1;}
    uint8_t buf[16];uint64_t off=0,total=0;int64_t n;
    while(total<ATMBOX_DUMP_MAX && (n=vfs_read(fd,buf,sizeof(buf)))>0){
        out_hex64(off);out(": ");for(int64_t i=0;i<n;i++){out_hex_byte(buf[i]);outc(' ');}for(int64_t i=n;i<16;i++)out("   ");out(" |");for(int64_t i=0;i<n;i++)outc(buf[i]>=32&&buf[i]<127?(char)buf[i]:'.');outln("|");off+=(uint64_t)n;total+=(uint64_t)n;
    }
    vfs_close(fd);if(n<0){err("hexdump","read failed");return -1;}if(total==ATMBOX_DUMP_MAX)outln("atm-box hexdump: output capped at 4096 bytes");return 0;
}
static int app_id(void){out("uid=");outu(vfs_current_uid());out(" gid=");outu(vfs_current_gid());outc('\n');return 0;}
static const char *task_state_name(task_state_t state){
    switch(state){case TASK_READY:return "ready";case TASK_RUNNING:return "running";case TASK_BLOCKED:return "blocked";case TASK_ZOMBIE:return "zombie";default:return "unused";}
}
static int app_ps(void){
    outln("PID PPID STATE    PRI CPUtk MEMB RD WR SW NAME");
    for(uint32_t i=0;i<TASK_MAX;i++){
        sched_task_info_t info;if(sched_task_info(i,&info)<0)continue;
        outu(info.pid);out(" ");outu(info.ppid);out(" ");out(task_state_name(info.state));out(" ");outu(info.priority);out(" ");
        outu(info.cpu_ticks);out(" ");outu(info.resident_bytes);out(" ");outu(info.io_read_bytes);out(" ");outu(info.io_write_bytes);out(" ");outu(info.context_switches);out(" ");outln(info.name);
    }
    return 0;
}
static int app_free(void){
    uint64_t used=heap_used_bytes(),freeb=heap_free_bytes(),resident=sched_total_resident_bytes();
    out("heap_used=");outu(used);out(" heap_free=");outu(freeb);out(" process_resident=");outu(resident);outc('\n');
    out("uptime_ticks=");outu(sched_uptime_ticks());out(" cpu_busy_ticks=");outu(sched_busy_ticks());out(" tasks=");outu(sched_task_count());outc('\n');
    return 0;
}
static int app_iostat(void){
    uint64_t bytes=0;for(int i=0;i<disk_count;i++)bytes+=(uint64_t)disk_drives[i].sectors_total*SECTOR_SIZE;
    out("drives=");outu(disk_count);out(" capacity_bytes=");outu(bytes);out(" read_ops=");outu(disk_read_ops);out(" write_ops=");outu(disk_write_ops);out(" errors=");outu(disk_io_errors);outc('\n');
    outln("per-process RD/WR in ps are completed native FD bytes; ATA attribution per process is not available yet.");
    return 0;
}
static int app_gpuinfo(void){
    atm_gfx_capabilities_t caps;
    if(mesa_foundation_query(&caps)<0){err("gpuinfo","capability query failed");return -1;}
    out("display=");outln(caps.display_backend);
    out("renderer=");outln(caps.renderer_backend);
    out("surface=");outu(caps.width);out("x");outu(caps.height);out(" bpp=");outu(caps.bpp);out(" pitch=");outu(caps.pitch);out(" bytes=");outu(caps.framebuffer_bytes);outc('\n');
    out("capabilities=0x");out_hex64(caps.capabilities);outc('\n');
    outln("hardware acceleration, GPU scheduling and per-process GPU accounting are unavailable.");
    return 0;
}
static int app_uptime(void){uint64_t ticks=sched_uptime_ticks();out("uptime_seconds=");outu(ticks/100u);out(" ticks=");outu(ticks);outc('\n');return 0;}
static int app_kill(int argc,char *argv[]){
    int arg=2,sig=15;if(argc>2&&!kstrcmp(argv[2],"-9")){sig=9;arg++;}if(arg>=argc){err("kill","usage: atm-box kill [-9] <pid>");return -1;}
    int pid=kstrtoi(argv[arg]);if(pid<=0||task_kill((uint32_t)pid,128+sig)<0){err("kill","cannot terminate process");return -1;}return 0;
}

int atmbox_dispatch(int argc, char *argv[]) {
    if (argc < 2 || !kstrcmp(argv[1], "--help") || !kstrcmp(argv[1], "help") || !kstrcmp(argv[1], "--list")) { atmbox_print_applets(); return 0; }
    const char *app = argv[1];
    if (!kstrcmp(app,"true")) return 0;
    if (!kstrcmp(app,"false")) return 1;
    if (!kstrcmp(app,"echo")) { int i=2,nl=1;if(i<argc&&!kstrcmp(argv[i],"-n")){nl=0;i++;}for(;i<argc;i++){if(i>2)outc(' ');out(argv[i]);}if(nl)outc('\n');return 0; }
    if (!kstrcmp(app,"basename")) { if(argc<3){err(app,"usage: atm-box basename <path>");return -1;} outln(path_base(argv[2])); return 0; }
    if (!kstrcmp(app,"dirname")) { if(argc<3){err(app,"usage: atm-box dirname <path>");return -1;} return print_parent(argv[2]); }
    if (!kstrcmp(app,"cat")) return app_cat(argc,argv);
    if (!kstrcmp(app,"head")) return app_head(argc,argv);
    if (!kstrcmp(app,"wc")) return app_wc(argc,argv);
    if (!kstrcmp(app,"ls")) return app_ls(argc,argv);
    if (!kstrcmp(app,"stat")) return app_stat(argc,argv);
    if (!kstrcmp(app,"cmp")) return app_cmp(argc,argv);
    if (!kstrcmp(app,"hexdump")) return app_hexdump(argc,argv);
    if (!kstrcmp(app,"id")) return app_id();
    if (!kstrcmp(app,"ps")) return app_ps();
    if (!kstrcmp(app,"free")) return app_free();
    if (!kstrcmp(app,"iostat")) return app_iostat();
    if (!kstrcmp(app,"gpuinfo")) return app_gpuinfo();
    if (!kstrcmp(app,"uptime")) return app_uptime();
    if (!kstrcmp(app,"kill")) return app_kill(argc,argv);
    if (!kstrcmp(app,"uname")) {outln("ATMKoala 0.9 x86_64");return 0;}
    if (!kstrcmp(app,"touch")) { if(argc<3){err(app,"usage: atm-box touch <file>");return -1;} int fd=vfs_open(argv[2],O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0){err(app,"cannot create file");return -1;} vfs_close(fd); return 0; }
    if (!kstrcmp(app,"mkdir")) { int i=2, pflag=0; if(argc>2&&!kstrcmp(argv[2],"-p")){pflag=1;i++;} if(i>=argc){err(app,"usage: atm-box mkdir [-p] <dir>");return -1;} int r=pflag?mkdir_parents(argv[i]):vfs_mkdir(argv[i],0755); if(r<0){err(app,"cannot create directory");return -1;} return 0; }
    if (!kstrcmp(app,"rm")) { if(argc<3){err(app,"usage: atm-box rm <file>");return -1;} if(vfs_unlink(argv[2])<0){err(app,"cannot remove file");return -1;} return 0; }
    if (!kstrcmp(app,"cp")) { if(argc<4){err(app,"usage: atm-box cp <source> <dest>");return -1;} if(copy_file(argv[2],argv[3])<0){err(app,"copy failed");return -1;} return 0; }
    if (!kstrcmp(app,"mv")) { if(argc<4){err(app,"usage: atm-box mv <source> <dest>");return -1;} if(vfs_rename(argv[2],argv[3])<0){err(app,"rename failed");return -1;} return 0; }
    err(app,"applet not available"); return -1;
}
