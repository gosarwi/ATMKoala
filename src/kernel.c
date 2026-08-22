/* kernel.c === atmkoala v0.5
 *
 * NEW in v7:
 *   - Games: Snake, Tetris (ASCII, fully playable)
 *   - Easter eggs: alya (ASCII art), crash (Windows BSOD), matrix (rain)
 *   - Alt+C hotkey in readline === opens config create
 *   - [command] section in system.conf === full command aliasing/remapping
 *   - Desktop customization: style windows/mac/caramel, bg color, wallpaper text
 *   - Kernel config: kernel_name, hostname_color, boot_msg, prompt_format,
 *                    disable_cmd, accent_char, motd_color, sidebar_info
 *   - Bug fixes: readline backspace at col 0, ls after rm, sysconf save theme
 *
 * READLINE v6 (inherited, fixed):
 *   - Alt+C during input === dispatch "config create"
 *   - Ctrl+C fixed for VBE path
 */

#include "vga.h"
#include "vbe.h"
#include "mesa_foundation.h"
#include "keyboard.h"
#include "mouse.h"
#include "util.h"
#include "vfs.h"
#include "gdt.h"
#include "idt.h"
#include "pit.h"
#include "rtc.h"
#include "atm_time.h"
#include "ntp.h"
#include "tzif.h"
#include "kmalloc.h"
#include "sched.h"
#include "elf.h"
#include "net.h"
#include "disk.h"
#include "partmgr.h"
#include "catfs.h"
#include "catfs_vfs.h"
#include "config.h"
#include "tarzst.h"
#include "http_client.h"
#include "mp3.h"
#include "atmbox.h"
#include "fileformat.h"
#include "image_fixtures.h"
#include "image_decode.h"
#include "exp.h"
#include "awm.h"
#include "ttf.h"
#include "ossdk.h"
#include "fat32.h"
#include "ext2.h"
#include "btrfs.h"
#include "hw_y116.h"
#include "osbuilder.h"
#include "gamesdk.h"
#include "gamelauncher.h"
#include "store.h"
#include "kernel_panic.h"
#include "kmod.h"
#include "fish_shell.h"
#include "unm.h"
#include "icmp.h"
#include "untui.h"
#include "diskmgr.h"
#include "bsd_compat.h"
#include "vga_modeset.h"
#include "users.h"
#include "atminit.h"
#include "atm_posix.h"
#include "atm_syscall.h"
#include "native_app.h"
#include "native_fd.h"
#include "native_dir.h"
#include "installer.h"
#include "paging.h"
#include "uaccess.h"
#include "usermode.h"
extern char keyboard_utf8_buf[4];
#include <stddef.h>
#include <stdint.h>

/* ── Multiboot2 information ─────────────────────────────────
 * GRUB's `multiboot2` command passes a variable-length, 8-byte aligned
 * tag list.  Do not interpret it as the old Multiboot1 fixed structure.
 */
#define MB2_BOOTLOADER_MAGIC 0x36D76289u
#define MB2_TAG_END          0u
#define MB2_TAG_CMDLINE      1u
#define MB2_TAG_MODULE       3u
#define MB2_TAG_BASIC_MEM    4u
#define MB2_TAG_FRAMEBUFFER  8u

typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t reserved;
} mb2_info_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct __attribute__((packed)) {
    uint32_t type, size;
    uint32_t mem_lower, mem_upper;
} mb2_tag_basic_mem_t;

typedef struct __attribute__((packed)) {
    uint32_t type, size;
    uint64_t mod_start, mod_end;
    char     string[];
} mb2_tag_module_t;

typedef struct __attribute__((packed)) {
    uint32_t type, size;
    uint64_t addr;
    uint32_t pitch, width, height;
    uint8_t  bpp, fb_type;
    uint16_t reserved;
    uint8_t  red_pos, red_mask, green_pos, green_mask, blue_pos, blue_mask;
} mb2_tag_framebuffer_t;

#define HEAP_ADDR 0x8000000UL  /* 128MB — above the 64MB kernel image and BSS */
#define HEAP_SIZE (8*1024*1024)

/* ====== Global state ==================================================================================================================================== */
static int       use_vbe   = 0;
static int       live_mode = 1;
static const mb2_info_t *g_mb2 = NULL;
static const mb2_tag_framebuffer_t *g_mb2_fb = NULL;
static uint32_t   g_mem_upper = 0;
static char       g_boot_cmdline[128];
static int        boot_text_mode = 0;
static int        installer_mode = 0;
static int        test_mode = 0;
static int        serial_console_ready = 0;
static int        sudo_mode  = 0;

static int boot_has_arg(const char *arg) {
    const char *p=g_boot_cmdline; size_t alen=kstrlen(arg);
    while (*p) {
        while (*p==' '||*p=='\t') p++;
        const char *start=p; while (*p&&*p!=' '&&*p!='\t') p++;
        if ((size_t)(p-start)==alen && !kstrncmp(start,arg,alen)) return 1;
    }
    return 0;
}

static const mb2_tag_t *mb2_next_tag(const mb2_tag_t *tag, const uint8_t *end) {
    if (!tag || tag->size < sizeof(mb2_tag_t)) return NULL;
    uintptr_t next = ((uintptr_t)tag + tag->size + 7u) & ~(uintptr_t)7u;
    if (next + sizeof(mb2_tag_t) > (uintptr_t)end) return NULL;
    return (const mb2_tag_t *)next;
}

static void mb2_parse(uint64_t magic, uint64_t info_phys) {
    g_mb2 = NULL;
    g_mb2_fb = NULL;
    g_mem_upper = 0;
    g_boot_cmdline[0]=0; boot_text_mode=0; installer_mode=0;
    if ((uint32_t)magic != MB2_BOOTLOADER_MAGIC || !info_phys) return;

    const mb2_info_t *info = (const mb2_info_t *)(uintptr_t)info_phys;
    if (info->total_size < sizeof(mb2_info_t) + sizeof(mb2_tag_t)) return;
    const uint8_t *end = (const uint8_t *)info + info->total_size;
    const mb2_tag_t *tag = (const mb2_tag_t *)(info + 1);
    g_mb2 = info;

    while ((const uint8_t *)tag + sizeof(mb2_tag_t) <= end) {
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_FRAMEBUFFER &&
            tag->size >= sizeof(mb2_tag_framebuffer_t))
            g_mb2_fb = (const mb2_tag_framebuffer_t *)tag;
        else if (tag->type == MB2_TAG_CMDLINE && tag->size > sizeof(mb2_tag_t)) {
            const char *line=(const char *)tag+sizeof(mb2_tag_t);
            kstrncpy(g_boot_cmdline,line,sizeof(g_boot_cmdline)-1);
            g_boot_cmdline[sizeof(g_boot_cmdline)-1]=0;
        }
        else if (tag->type == MB2_TAG_BASIC_MEM &&
                 tag->size >= sizeof(mb2_tag_basic_mem_t))
            g_mem_upper = ((const mb2_tag_basic_mem_t *)tag)->mem_upper;
        const mb2_tag_t *next = mb2_next_tag(tag, end);
        if (!next) break;
        tag = next;
    }
    boot_text_mode=boot_has_arg("novbe");
    installer_mode=boot_has_arg("installer");
}

/* ====== Console abstraction =============================================================================================================== */
static void con_putchar(char c) {
    if (use_vbe) vbe_console_putchar(c);
    else { terminal_putchar(c); if(serial_console_ready) sdk_serial_putc(c); }
}
static void con_write(const char *s) { while(*s) con_putchar(*s++); }
static void con_writeln(const char *s){ con_write(s); con_putchar('\n'); }
static void con_clear(void) {
    if (use_vbe) vbe_console_clear(); else terminal_clear();
}
static void con_erase(void) {
    if (!use_vbe) terminal_erase_char();
}
static void con_erase_eol(void) {
    if (use_vbe) vbe_console_erase_eol();
    else terminal_erase_eol();
}
static void con_get_cursor(int *row, int *col) {
    if (use_vbe) vbe_console_get_cursor(row, col);
    else terminal_get_cursor(row, col);
}
static void con_set_cursor(int row, int col) {
    if (use_vbe) vbe_console_set_cursor(row, col);
    else terminal_set_cursor(row, col);
}
static void con_move_left(void) {
    int row, col;
    con_get_cursor(&row, &col);
    if (col > 0) con_set_cursor(row, col - 1);
}

/* Color helpers */
#define C_NRM() terminal_set_color(SCH->normal_fg,  SCH->normal_bg)
#define C_ERR() terminal_set_color(SCH->error_fg,   SCH->normal_bg)
#define C_ACC() terminal_set_color(SCH->accent_fg,  SCH->normal_bg)
#define C_OK()  terminal_set_color(SCH->success_fg, SCH->normal_bg)
#define C_HDR() terminal_set_color(SCH->header_fg,  SCH->normal_bg)
#define C_WRN() terminal_set_color(SCH->warn_fg,    SCH->normal_bg)
#define C_DIM() terminal_set_color(SCH->dim_fg,     SCH->normal_bg)
#define C_PRM() terminal_set_color(SCH->prompt_fg,  SCH->normal_bg)

/* cprintf */
static void cprintf(const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    char nb[32];
    while (*fmt) {
        if (*fmt != '%') { con_putchar(*fmt++); continue; }
        fmt++;
        int w=0,zp=0,left=0;
        if (*fmt=='-'){left=1;fmt++;}
        if (*fmt=='0'){zp=1;fmt++;} while(*fmt>='0'&&*fmt<='9'){w=w*10+(*fmt-'0');fmt++;}
        if (*fmt=='d'){int n=__builtin_va_arg(ap,int);kitoa(n,nb,10);
            int l=(int)kstrlen(nb); if(!left)for(int i=l;i<w;i++)con_putchar(zp?'0':' ');
            con_write(nb); if(left)for(int i=l;i<w;i++)con_putchar(' ');}
        else if (*fmt=='u'){uint32_t n=__builtin_va_arg(ap,uint32_t);kuitoa(n,nb,10);
            int l=(int)kstrlen(nb); if(!left)for(int i=l;i<w;i++)con_putchar(zp?'0':' ');
            con_write(nb); if(left)for(int i=l;i<w;i++)con_putchar(' ');}
        else if (*fmt=='x'){uint32_t n=__builtin_va_arg(ap,uint32_t);kuitoa(n,nb,16);
            int l=(int)kstrlen(nb); if(!left)for(int i=l;i<w;i++)con_putchar(zp?'0':' ');
            con_write(nb); if(left)for(int i=l;i<w;i++)con_putchar(' ');}
        else if (*fmt=='p'){uint32_t n=__builtin_va_arg(ap,uint32_t);kuitoa(n,nb,16);
            con_write("0x"); con_write(nb);}
        else if (*fmt=='s'){const char*s=__builtin_va_arg(ap,const char*);s=s?s:"(null)";
            int l=(int)kstrlen(s); if(!left)for(int i=l;i<w;i++)con_putchar(' ');
            con_write(s); if(left)for(int i=l;i<w;i++)con_putchar(' ');}
        else if (*fmt=='c'){con_putchar((char)__builtin_va_arg(ap,int));}
        else if (*fmt=='%'){con_putchar('%');}
        else {con_putchar('%');con_putchar(*fmt);}
        fmt++;
    }
    __builtin_va_end(ap);
}
#undef kprintf
#define kprintf cprintf

/* ====== Shell state ======================================================================================================================================= */
#define CMD_MAX       512
#define HIST_SIZE     256
#define RL_MAX        512
#define COMP_MAX       64

static char  cwd[128]   = "/home";
static char  history[HIST_SIZE][CMD_MAX];
static int   hist_count  = 0;
static char  hostname[64] = "atmkoala";
static int   redirect_fd  = -1;

/* ====== READLINE v6 (fixed + Alt+C hotkey) =============================================================== */
static int  rl_prompt_col = 0;
static int  rl_prompt_row = 0;

void print_prompt(void);  /* used by fish_shell.c */

static void rl_redraw(const char *linebuf, int len, int pos, int old_len) {
    /* The Text GRUB entry can still retain a Multiboot framebuffer.  Both
     * terminal backends must therefore redraw the editable line; returning
     * early for VBE accepted keys but made every typed byte invisible. */

    /* v9 FIX: ================ redraw ==== ============ ====================== ============== ==============.
     * ================:
     *   1. ====================== ============ ========== ==== (rl_prompt_row, rl_prompt_col).
     *   2. ============== ====== ============ ==== ========== + 1 ============ ======== (==== ============ ================).
     *   3. ======================== ======== ========== == ============.
     *   4. ================== ============ ==== ============ pos.
     * ====== ================== ======================== ================ ====== ================ ============.
     */
    int cur_row, cur_col;
    con_get_cursor(&cur_row, &cur_col);

    /* ====== 1: ========================== ==== ============ ============ ========== */
    /* ======== ==== ==== ====== ==== ============ === ============ ======== ========== */
    /* ======== ======== ======== === ======== ========== ========== \b ==== col=0, ========== CR */
    if (cur_row == rl_prompt_row) {
        while (cur_col > rl_prompt_col) { con_move_left(); cur_col--; }
    } else {
        /* ============== ================ === ============== ============== ============ == ======================== */
        con_erase_eol();
        /* CR == ============== ==== ============ ========== */
        con_set_cursor(rl_prompt_row, rl_prompt_col);
    }

    /* ====== 2: ============== ============ ============== ==== ============== ============== ==== ========== ============ */
    con_erase_eol();
    /* Clear a possible wrapped continuation line using the active console
     * geometry. This keeps deletion/backspace correct in both VGA and VBE. */
    int text_cols = use_vbe ? (int)(vbe.width / 8u) : VGA_WIDTH;
    int text_rows = use_vbe ? (int)(vbe.height / 16u) : VGA_HEIGHT;
    if (text_cols > 0 && rl_prompt_col + old_len >= text_cols) {
        int next_row = rl_prompt_row + 1;
        if (next_row < text_rows) {
            int sr, sc2; con_get_cursor(&sr, &sc2);
            con_set_cursor(next_row, 0);
            con_erase_eol();
            con_set_cursor(sr, sc2);
        }
    }

    /* ====== 3: ======================== ======== ========== */
    C_NRM();
    for (int i = 0; i < len; i++) con_putchar(linebuf[i]);

    /* ====== 4: ============== ============ ==== pos */
    /* ============== col = rl_prompt_col + len (mod VGA_WIDTH) */
    /* ============ ======== ========== len-pos ====== */
    for (int i = len; i > pos; i--) con_move_left();
}

/* Forward declare dispatch so readline can call it for Alt+C */

void dispatch(char *line);


static void readline_v6(char *out, int maxlen) {
    char    buf[RL_MAX];
    int     len = 0, pos = 0;
    int     hist_idx = hist_count;
    char    saved[RL_MAX]; saved[0] = 0;

    /* Tab completion candidates */
    static const char *builtins[] = {
        "ls","ll","la","cat","view","less","installer-log","head","tail","file","hd","hexdump","wc",
        "grep","sort","uniq","cut","tr","tee","find","tree","write","append","touch","dd",
        "rm","cp","mv","mkdir","rmdir","cd","pwd","stat","chmod","chown","ln",
        "ps","kill","sleep","wait","sh","source","uname","info","hwinfo","lscpu","cpucompat",
        "uptime","mem","free","dmesg","date","timezone","cube","gears","glxgears","hostname","whoami","id","env","set",
        "unset","export","printenv","lsblk","swap","gpu","df","du","mount","umount","mkfs","sync",
        "ifconfig","netstat","ping","arp","route","unm","untui","netui",
        "diskmgr","fdisk","cfdisk",
        "readelf","exec","sdk","serial","modinfo","malloc","which","man","history","nano","calc","bc",
        "clear","reset","logo","echo","printf","de","install","live","modules","aiy",
        "help","reboot","halt","poweroff","sudo","su","login","logout","adduser","deluser","usermod","passwd","users","openrc","rc","rc-status","rc-service","service","rc-update","initctl","xargs","atm-box","atmbox","busybox",
        /* v10 new */
        "snake","tetris","pong","desktop","kernel","notepad","posix","syscall",
        NULL
    };

    con_get_cursor(&rl_prompt_row, &rl_prompt_col);

    while (1) {
        /* Poll with alt-awareness */
        int k;
        do {
            while (!(inb(0x64) & 1)) __asm__ volatile("pause");
            k = keyboard_poll();
        } while (k == 0);

        /* ====== Alt+C hotkey === open config create ====== */
        if (keyboard_alt() && (k == 'c' || k == 'C' || k == 3)) {
            /* Clear current line visually */
            if (!use_vbe) {
                for (int i = pos; i < len; i++) con_putchar(' ');
                for (int i = 0; i < len; i++) terminal_putchar('\b');
                for (int i = 0; i < len; i++) con_putchar(' ');
                for (int i = 0; i < len; i++) terminal_putchar('\b');
            }
            con_putchar('\n');
            C_ACC(); con_writeln("[Alt+C] Opening Config Creator..."); C_NRM();
            dispatch((char*)"config create");
            /* Reprint prompt */
            print_prompt();
            len = 0; pos = 0;
            if (!use_vbe) {
                int r2, c2; terminal_get_cursor(&r2, &c2);
                rl_prompt_col = c2; rl_prompt_row = r2;
            }
            continue;
        }

        int old_len = len;

        if (k == '\n' || k == '\r') {
            /* Move to end so next prompt is on new line. This also makes a
             * line edited with arrow keys consistent on the VBE backend. */
            for (int i = pos; i < len; i++) con_putchar(buf[i]);
            con_putchar('\n');
            buf[len] = 0;
            kstrcpy(out, buf);
            return;
        }

        /* Ctrl+C */
        if (k == 3) {
            con_putchar('^'); con_putchar('C'); con_putchar('\n');
            out[0] = 0; return;
        }
        /* Ctrl+D */
        if (k == 4 && len == 0) { con_putchar('\n'); out[0] = 0; return; }
        /* Ctrl+L */
        if (k == 12) {
            con_clear();
            print_prompt();
            if (!use_vbe) {
                int r2,c2; terminal_get_cursor(&r2,&c2);
                rl_prompt_col=c2; rl_prompt_row=r2;
            }
            rl_redraw(buf,len,pos,0);
            continue;
        }
        /* Ctrl+A === beginning of line */
        if (k == 1) {
            pos = 0; rl_redraw(buf,len,pos,old_len); continue;
        }
        /* Ctrl+E === end of line */
        if (k == 5) {
            pos = len; rl_redraw(buf,len,pos,old_len); continue;
        }
        /* Ctrl+K === kill to end */
        if (k == 11) {
            len = pos; buf[len] = 0; rl_redraw(buf,len,pos,old_len); continue;
        }
        /* Ctrl+U === kill to start */
        if (k == 21) {
            if (!use_vbe) {
                for (int i=0;i<pos;i++) terminal_putchar('\b');
                for (int i=0;i<len;i++) con_putchar(' ');
                for (int i=0;i<len;i++) terminal_putchar('\b');
            }
            kmemmove(buf, buf+pos, (size_t)(len-pos));
            len -= pos; pos = 0; buf[len] = 0;
            rl_redraw(buf,len,pos,old_len); continue;
        }
        /* Ctrl+W === delete word before cursor */
        if (k == 23) {
            int p2 = pos;
            while (p2 > 0 && buf[p2-1] == ' ') p2--;
            while (p2 > 0 && buf[p2-1] != ' ') p2--;
            int del = pos - p2;
            kmemmove(buf+p2, buf+pos, (size_t)(len-pos));
            len -= del; pos = p2; buf[len] = 0;
            rl_redraw(buf,len,pos,old_len); continue;
        }

        /* Up arrow === history */
        if (k == KEY_UP) {
            if (hist_idx == hist_count) kstrcpy(saved, buf);
            if (hist_idx > 0) {
                hist_idx--;
                kstrcpy(buf, history[hist_idx]);
                len = (int)kstrlen(buf); pos = len;
                rl_redraw(buf,len,pos,old_len);
            }
            continue;
        }
        /* Down arrow === history */
        if (k == KEY_DOWN) {
            if (hist_idx < hist_count) {
                hist_idx++;
                if (hist_idx == hist_count) kstrcpy(buf, saved);
                else kstrcpy(buf, history[hist_idx]);
                len = (int)kstrlen(buf); pos = len;
                rl_redraw(buf,len,pos,old_len);
            }
            continue;
        }
        /* Left/Right */
        if (k == KEY_LEFT)  { if(pos>0) pos--; rl_redraw(buf,len,pos,old_len); continue; }
        if (k == KEY_RIGHT) { if(pos<len) pos++; rl_redraw(buf,len,pos,old_len); continue; }
        if (k == KEY_HOME)  { pos=0; rl_redraw(buf,len,pos,old_len); continue; }
        if (k == KEY_END)   { pos=len; rl_redraw(buf,len,pos,old_len); continue; }

        /* PgUp/PgDn === scroll */
        if (k == KEY_PGUP) { if(!use_vbe) terminal_scroll_up(10);   continue; }
        if (k == KEY_PGDN) { if(!use_vbe) terminal_scroll_down(10); continue; }

        /* Delete key */
        if (k == KEY_DEL) {
            if (pos < len) {
                kmemmove(buf+pos, buf+pos+1, (size_t)(len-pos-1));
                len--; buf[len]=0;
                rl_redraw(buf,len,pos,old_len);
            }
            continue;
        }

        /* Backspace */
        if (k == '\b' || k == 127) {
            if (pos > 0) {
                kmemmove(buf+pos-1, buf+pos, (size_t)(len-pos));
                pos--; len--; buf[len]=0;
                /* Redraw both VGA and framebuffer text consoles. */
                rl_redraw(buf,len,pos,old_len);
            }
            continue;
        }

        /* Tab completion */
        if (k == '\t') {
            /* Find prefix */
            int ws = pos;
            while (ws > 0 && buf[ws-1] != ' ') ws--;
            char prefix[64]; int pl = pos - ws;
            if (pl >= 63) { continue; }
            kmemcpy(prefix, buf+ws, (size_t)pl); prefix[pl] = 0;

            /* Collect matches */
            const char *matches[COMP_MAX]; int mc = 0;
            for (int i = 0; builtins[i] && mc < COMP_MAX; i++) {
                if (kstrncmp(builtins[i], prefix, (size_t)pl) == 0)
                    matches[mc++] = builtins[i];
            }
            /* SDK commands */
            sdk_cmd_t *sc = sdk_cmd_find(NULL);
            while (sc && mc < COMP_MAX) {
                if (kstrncmp(sc->name, prefix, (size_t)pl)==0) matches[mc++]=sc->name;
                sc = sc->next;
            }

            if (mc == 1) {
                /* Complete */
                const char *m = matches[0];
                int mlen = (int)kstrlen(m);
                int add = mlen - pl;
                if (len + add < maxlen - 1) {
                    kmemmove(buf+pos+add, buf+pos, (size_t)(len-pos));
                    kmemcpy(buf+pos, m+pl, (size_t)add);
                    len += add; pos += add; buf[len] = 0;
                    rl_redraw(buf,len,pos,old_len);
                }
            } else if (mc > 1) {
                con_putchar('\n');
                for (int i = 0; i < mc; i++) {
                    C_DIM(); con_write(matches[i]); con_putchar(' ');
                }
                C_NRM(); con_putchar('\n');
                print_prompt();
                if (!use_vbe) {
                    int r2,c2; terminal_get_cursor(&r2,&c2);
                    rl_prompt_col=c2; rl_prompt_row=r2;
                }
                rl_redraw(buf,len,pos,0);
            }
            continue;
        }

        /* ====== ============== ============== (UTF-8) ====== */
        if (k == KEY_UTF8) {
            int ulen = (int)kstrlen(keyboard_utf8_buf);
            if (len + ulen < RL_MAX - 1 && len + ulen < maxlen - 1) {
                kmemmove(buf+pos+ulen, buf+pos, (size_t)(len-pos));
                kmemcpy(buf+pos, keyboard_utf8_buf, (size_t)ulen);
                pos += ulen; len += ulen; buf[len] = 0;
                rl_redraw(buf, len, pos, old_len);
            }
            continue;
        }

        /* Normal printable character */
        if (k >= ' ' && k <= '~' && len < maxlen-1) {
            kmemmove(buf+pos+1, buf+pos, (size_t)(len-pos));
            buf[pos] = (char)k;
            pos++; len++; buf[len] = 0;
            rl_redraw(buf,len,pos,old_len);
        }
    }
}

void readline_v9(char *out, int maxlen) { readline_v6(out, maxlen); }

/* ====== build_abs: resolve path relative to cwd ================================================ */
static void build_abs(const char *rel, char *out) {
    if (rel[0] == '/') { kstrcpy(out, rel); return; }
    kstrcpy(out, cwd);
    if (out[kstrlen(out)-1] != '/') kstrcat(out, "/");
    kstrcat(out, rel);
}

/* ====== arg parser ============================================================================================================================================= */
static int parse_args(char *line, char **argv, int maxargc) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (argc >= maxargc-1) break;
        char q = 0;
        if (*p == '"' || *p == '\'') { q = *p++; }
        argv[argc++] = p;
        if (q) {
            while (*p && *p != q) p++;
            if (*p) *p++ = 0;
        } else {
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* ====== ixpy: bounded expression/print interpreter ============================================= */
typedef struct {char name[16];int value;} ixpy_var_t;
typedef struct {const char *p;ixpy_var_t vars[8];int var_count,error,last_value,prints;} ixpy_ctx_t;
static void ixpy_ws(ixpy_ctx_t*c){while(*c->p==' '||*c->p=='\t'||*c->p=='\n'||*c->p=='\r')c->p++;}
static int ixpy_ident(ixpy_ctx_t*c,char out[16]){int n=0;ixpy_ws(c);if(!((*c->p>='a'&&*c->p<='z')||(*c->p>='A'&&*c->p<='Z')||*c->p=='_'))return -1;while(((*c->p>='a'&&*c->p<='z')||(*c->p>='A'&&*c->p<='Z')||(*c->p>='0'&&*c->p<='9')||*c->p=='_')&&n<15)out[n++]=*c->p++;out[n]=0;return n?0:-1;}
static int ixpy_get(ixpy_ctx_t*c,const char*name,int*out){for(int i=0;i<c->var_count;i++)if(!kstrcmp(c->vars[i].name,name)){*out=c->vars[i].value;return 0;}c->error=1;return -1;}
static int ixpy_set(ixpy_ctx_t*c,const char*name,int value){for(int i=0;i<c->var_count;i++)if(!kstrcmp(c->vars[i].name,name)){c->vars[i].value=value;return 0;}if(c->var_count>=8){c->error=1;return -1;}kstrncpy(c->vars[c->var_count].name,name,15);c->vars[c->var_count++].value=value;return 0;}
static int ixpy_expr(ixpy_ctx_t*c,int*out);
static int ixpy_primary(ixpy_ctx_t*c,int*out){char name[16];int v=0;ixpy_ws(c);if(*c->p=='('){c->p++;if(ixpy_expr(c,&v)<0)return -1;ixpy_ws(c);if(*c->p!=')'){c->error=1;return -1;}c->p++;*out=v;return 0;}if(*c->p>='0'&&*c->p<='9'){while(*c->p>='0'&&*c->p<='9'){v=v*10+(*c->p-'0');c->p++;}*out=v;return 0;}if(ixpy_ident(c,name)==0){if(ixpy_get(c,name,&v)<0)return -1;*out=v;return 0;}c->error=1;return -1;}
static int ixpy_term(ixpy_ctx_t*c,int*out){int v=0,r=0;if(ixpy_primary(c,&v)<0)return -1;for(;;){ixpy_ws(c);char op=*c->p;if(op!='*'&&op!='/'&&op!='%')break;c->p++;if(ixpy_primary(c,&r)<0)return -1;if((op=='/'||op=='%')&&!r){c->error=1;return -1;}if(op=='*')v*=r;else if(op=='/')v/=r;else v%=r;}*out=v;return 0;}
static int ixpy_expr(ixpy_ctx_t*c,int*out){int v=0,r=0,sign=1;ixpy_ws(c);if(*c->p=='-'){sign=-1;c->p++;}if(ixpy_term(c,&v)<0)return -1;v*=sign;for(;;){ixpy_ws(c);char op=*c->p;if(op!='+'&&op!='-')break;c->p++;if(ixpy_term(c,&r)<0)return -1;if(op=='+')v+=r;else v-=r;}*out=v;return 0;}
static int ixpy_word(ixpy_ctx_t*c,const char*word){const char*s=c->p;for(;*word;word++)if(*c->p++!=*word){c->p=s;return 0;}if((*c->p>='a'&&*c->p<='z')||(*c->p>='A'&&*c->p<='Z')||*c->p=='_'){c->p=s;return 0;}return 1;}
static int ixpy_execute(const char*source,int emit,ixpy_ctx_t*outctx){ixpy_ctx_t c;kmemset(&c,0,sizeof(c));c.p=source?source:"";ixpy_ws(&c);int outer=0;if(*c.p=='('){outer=1;c.p++;}for(;;){char name[16];int value=0;ixpy_ws(&c);if(outer&&*c.p==')'){c.p++;ixpy_ws(&c);if(*c.p)c.error=1;break;}if(!*c.p)break;if(ixpy_word(&c,"print")){ixpy_ws(&c);if(*c.p!='('){c.error=1;break;}c.p++;ixpy_ws(&c);if(*c.p=='\"'||*c.p=='\''){char q=*c.p++;const char*start=c.p;while(*c.p&&*c.p!=q)c.p++;if(*c.p!=q){c.error=1;break;}if(emit)while(start<c.p)con_putchar(*start++);if(emit)con_putchar('\n');c.p++;}else{if(ixpy_expr(&c,&value)<0)break;c.last_value=value;if(emit)cprintf("%d\n",value);}ixpy_ws(&c);if(*c.p!=')'){c.error=1;break;}c.p++;c.prints++;}else if(ixpy_ident(&c,name)==0){ixpy_ws(&c);if(*c.p!='='){c.error=1;break;}c.p++;if(ixpy_expr(&c,&value)<0)break;if(ixpy_set(&c,name,value)<0)break;}else{c.error=1;break;}ixpy_ws(&c);if(*c.p==';'){c.p++;continue;}if(outer&&*c.p==')')continue;if(!*c.p)break;c.error=1;break;}if(outctx)*outctx=c;return c.error?-1:0;}
static const char *ixpy_source_suffix(const char *line){while(line&&(*line==' '||*line=='\t'))line++;if(!line||kstrncmp(line,"ixpy",4)||(line[4]&&line[4]!=' '&&line[4]!='\t'))return NULL;return line+4;}
static int ixpy_selftest(void){ixpy_ctx_t c;const char *raw=ixpy_source_suffix("ixpy ( print (\"hello world\") )");if(ixpy_execute("(x=7; print(x*2+1))",0,&c)<0||c.last_value!=15||c.prints!=1)return -1;if(!raw||ixpy_execute(raw,0,&c)<0||c.prints!=1)return -1;return ixpy_execute("import os",0,&c)<0?0:-1;}
static void ixpy_command(const char *source){while(source&&(*source==' '||*source=='\t'))source++;if(!source||!*source){con_writeln("usage: ixpy ( print(\"hello world\") )");con_writeln("ixpy supports integers, + - * / %, variables, assignments and print(...).");con_writeln("unsupported: imports, files, functions, loops, classes, eval and CPython modules.");return;}ixpy_ctx_t c;if(ixpy_execute(source,1,&c)<0){C_ERR();con_writeln("ixpy: syntax, name, divide-by-zero or unsupported feature");C_NRM();}}

/* ====== interactive credential input ========================================================================================================================== */
static void read_secret(const char *label, char *out, int maxlen) {
    C_WRN(); con_write(label); C_NRM();
    int n=0;
    while(1){int k=keyboard_getkey();
        if(k=='\n'||k=='\r'){out[n]=0;con_putchar('\n');break;}
        if((k=='\b'||k==127)&&n>0){n--;terminal_putchar('\b');con_erase();terminal_putchar('\b');}
        else if(k>=' '&&k<='~'&&n<maxlen-1){out[n++]=(char)k;con_putchar('*');}
    }
}

static int do_sudo_auth(void) {
    const user_account_t *u=user_current();
    if (!u || !user_can_sudo()) { C_ERR(); con_writeln("sudo: current role is not permitted"); C_NRM(); return 0; }
    char password[64]; read_secret("[sudo] password: ",password,sizeof(password));
    if(user_auth(u->name,password)) return 1;
    C_ERR(); con_writeln("sudo: authentication failure"); C_NRM();
    return 0;
}

static const char *session_name(void) {
    const user_account_t *u=user_current();
    return u ? u->name : "unknown";
}

static int session_is_privileged(void) { return sudo_mode || user_is_admin(); }

static const char *init_state_name(atm_service_state_t state) {
    return state==ATM_SERVICE_STARTED ? "started" : (state==ATM_SERVICE_FAILED ? "failed" : "stopped");
}

static void initctl_show_status(void) {
    cprintf("Runlevel: %s (%d)  uptime: %us\n",atminit_runlevel_name(atminit_runlevel()),(int)atminit_runlevel(),sched_uptime_ticks()/100U);
    con_writeln("SERVICE        STATE     AUTO  START(s)  CHANGE(s)  DEPENDENCIES");
    for (int i=0;i<atminit_service_count();i++) {
        const atm_service_info_t *s=atminit_service_at(i);
        if (!s) continue;
        cprintf("%-14s %-9s %-5s %-9u %-10u %s\n",s->name,init_state_name(s->state),s->enabled?"on":"off",s->start_tick/100U,s->last_change_tick/100U,s->dependencies[0]?s->dependencies:"-");
    }
}

static void initctl_show_log(int tail) {
    int count=atminit_log_count();
    if (tail<=0 || tail>count) tail=count;
    int begin=count-tail;
    cprintf("init log: showing %d/%d entries\n",tail,count);
    for (int i=begin;i<count;i++) {
        const atm_init_log_entry_t *entry=atminit_log_at(i);
        if (entry) cprintf("[%6us] %-7s %-12s %s\n",entry->tick/100U,entry->event,entry->service,entry->detail);
    }
}
static void initctl_show_kernel_log(int tail) {
    int count=atminit_kernel_log_count();
    if (tail<=0 || tail>count) tail=count;
    cprintf("kernel kprintf log: showing %d/%d retained messages\n",tail,count);
    for (int i=count-tail;i<count;i++) {
        const atm_kernel_log_entry_t *entry=atminit_kernel_log_at(i);
        if (entry) cprintf("[#%u] %s\n",entry->sequence,entry->text);
    }
}
static void initctl_show_memory(void) {
    atm_memory_status_t mem;
    atminit_memory_status(&mem);
    cprintf("Memory lamps: heap %u%% [%s]  used %u B  free %u B\n",mem.heap_percent,atminit_memory_pressure_name(mem.pressure),mem.heap_used,mem.heap_free);
    cprintf("Task resident: %u B across %u task(s); this is scheduler accounting, not physical-RAM total.\n",(uint32_t)mem.task_resident,mem.task_count);
}

static void initctl_session(void) {
    char line[CMD_MAX];
    con_writeln("ATM initctl: live native-service console");
    con_writeln("Commands: status | log [N] | kernel [N] | memory | logfile | viewer | start NAME | stop NAME | restart NAME | enable NAME | disable NAME | runlevel 0|1|3|5 | help | exit");
    for (;;) {
        C_ACC(); con_write("initctl> "); C_NRM();
        readline_v6(line,sizeof(line));
        char *argv[6]; int argc=parse_args(line,argv,6);
        if (!argc) continue;
        if (!kstrcmp(argv[0],"exit") || !kstrcmp(argv[0],"quit")) break;
        if (!kstrcmp(argv[0],"help")) {
            con_writeln("status, log [N], kernel [N], memory, logfile, viewer, start/stop/restart NAME, enable/disable NAME, runlevel 0|1|3|5, exit");
            continue;
        }
        if (!kstrcmp(argv[0],"status")) { initctl_show_status(); continue; }
        if (!kstrcmp(argv[0],"log")) { initctl_show_log(argc>1?kstrtoi(argv[1]):20); continue; }
        if (!kstrcmp(argv[0],"kernel")) { initctl_show_kernel_log(argc>1?kstrtoi(argv[1]):20); continue; }
        if (!kstrcmp(argv[0],"memory")) { initctl_show_memory(); continue; }
        if (!kstrcmp(argv[0],"logfile")) { cprintf("Persistent log path: %s (writable CatFS only)\n",atminit_log_path()); continue; }
        if (!kstrcmp(argv[0],"viewer")) {
            if (!use_vbe) { cprintf("initctl: GUI inactive; use view %s\n",atminit_log_path()); continue; }
            if (exp_open_app(APP_VIEWER,atminit_log_path())<0) { C_ERR(); con_writeln("initctl: Viewer has no free window slot"); C_NRM(); }
            else { C_OK(); con_writeln("initctl: log opened in Viewer"); C_NRM(); }
            continue;
        }
        if (!kstrcmp(argv[0],"runlevel")) {
            atm_runlevel_t level;
            if (!session_is_privileged()) { C_ERR(); con_writeln("initctl: administrator privileges required"); C_NRM(); continue; }
            if (argc!=2 || atminit_parse_runlevel(argv[1],&level)<0 || atminit_set_runlevel(level)<0) { C_ERR(); con_writeln("initctl: runlevel transition failed"); C_NRM(); }
            else { C_OK(); cprintf("initctl: entered %s\n",atminit_runlevel_name(level)); C_NRM(); }
            continue;
        }
        if ((!kstrcmp(argv[0],"start") || !kstrcmp(argv[0],"stop") || !kstrcmp(argv[0],"restart") || !kstrcmp(argv[0],"enable") || !kstrcmp(argv[0],"disable")) && argc==2) {
            if (!session_is_privileged()) { C_ERR(); con_writeln("initctl: administrator privileges required"); C_NRM(); continue; }
            int result;
            if (!kstrcmp(argv[0],"start")) result=atminit_start(argv[1]);
            else if (!kstrcmp(argv[0],"stop")) result=atminit_stop(argv[1]);
            else if (!kstrcmp(argv[0],"restart")) result=atminit_restart(argv[1]);
            else result=atminit_set_enabled(argv[1],!kstrcmp(argv[0],"enable"));
            if (result<0) { C_ERR(); con_writeln("initctl: operation failed (service, state or dependency)"); C_NRM(); }
            else { C_OK(); cprintf("initctl: %s %s\n",argv[0],argv[1]); C_NRM(); }
            continue;
        }
        C_ERR(); con_writeln("initctl: invalid command; type help"); C_NRM();
    }
    con_writeln("initctl: session closed");
}

static void apply_session_environment(void) {
    const user_account_t *u=user_current();
    if (!u) return;
    sdk_env_set("HOME",u->home); sdk_env_set("USER",u->name);
    kstrcpy(cwd,u->home);
}

/* ====== Prompt ========================================================================================================================================================= */
void print_prompt(void) {
    /* Check custom prompt_format in kernel config */
    const char *pfmt = sysconf_get("kernel","prompt_format");
    if (pfmt && pfmt[0]) {
        /* Simple substitution: %u=user %h=host %d=cwd %$=# or $ */
        C_PRM();
        const char *p = pfmt;
        while (*p) {
            if (*p == '%' && *(p+1)) {
                p++;
                if (*p=='u') con_write(session_name());
                else if (*p=='h') con_write(hostname);
                else if (*p=='d') con_write(cwd);
                else if (*p=='$') con_putchar(session_is_privileged()?'#':'$');
                else { con_putchar('%'); con_putchar(*p); }
                p++;
            } else {
                con_putchar(*p++);
            }
        }
        C_NRM();
        return;
    }
    /* Default prompt */
    C_PRM();
    con_write(session_name()); con_putchar('@');
    con_write(hostname);
    C_DIM(); con_putchar(':');
    C_ACC(); con_write(cwd);
    C_NRM(); con_putchar(session_is_privileged()?'#':'$'); con_putchar(' ');
}

/* ====== ls helper ================================================================================================================================================ */
static void do_ls(int argc, char *argv[]) {
    int show_all = 0, long_fmt = 0;
    const char *path = cwd;
    for (int i=1;i<argc;i++){
        if(!argv[i]) break;
        if(argv[i][0]=='-'){
            for(int j=1;argv[i][j];j++){
                if(argv[i][j]=='a') show_all=1;
                if(argv[i][j]=='l') long_fmt=1;
            }
        } else path=argv[i];
    }
    char abs[128]; build_abs(path, abs);
    char names[64][VFS_NAME_MAX + 1]; int count=0;
    vfs_listdir(abs, names, &count);
    /* BUG FIX v7: sort names so deleted files don't appear */
    for(int i=0;i<count-1;i++) for(int j=0;j<count-i-1;j++)
        if(kstrcmp(names[j],names[j+1])>0){
            char tmp[VFS_NAME_MAX + 1]; kstrcpy(tmp,names[j]);
            kstrcpy(names[j],names[j+1]); kstrcpy(names[j+1],tmp);
        }
    for(int i=0;i<count;i++){
        size_t nl=kstrlen(names[i]);
        int is_dir=nl>0&&names[i][nl-1]=='/';
        if(!show_all && names[i][0]=='.') continue;
        if(long_fmt){
            char fp[256]; kstrcpy(fp,abs);
            if(fp[kstrlen(fp)-1]!='/') kstrcat(fp,"/");
            char nm2[VFS_NAME_MAX + 1]; kstrcpy(nm2,names[i]);
            if(is_dir&&nm2[kstrlen(nm2)-1]=='/') nm2[kstrlen(nm2)-1]=0;
            kstrcat(fp,nm2);
            vfs_stat_t st; int has=vfs_stat(fp,&st)==0;
            C_DIM(); con_write(is_dir?"drwxr-xr-x":"-rw-r--r--");
            cprintf(" 1 root root %6u ", has?(uint32_t)st.size:0u);
            C_NRM();
            if(is_dir){C_ACC();}else{C_NRM();}
            con_writeln(names[i]); C_NRM();
        } else {
            if(is_dir){C_ACC();}else{C_NRM();}
            con_write(names[i]); con_putchar(' ');
        }
    }
    if(!long_fmt) con_putchar('\n');
    C_NRM();
}

/* ====== cat / view ============================================================================================================================================= */
static void do_cat(const char *path_arg) {
    char p[128]; build_abs(path_arg, p);
    int fd = vfs_open(p, O_RDONLY, 0);
    if (fd < 0) { C_ERR(); cprintf("cat: %s: not found\n", path_arg); C_NRM(); return; }
    static uint8_t buf[4096]; int n;
    while ((n=vfs_read(fd,buf,sizeof(buf)-1))>0) {
        buf[n]=0; con_write((char*)buf);
    }
    vfs_close(fd);
    if (buf[n>0?n-1:0]!='\n') con_putchar('\n');
}

static void do_view(const char *path_arg) { do_cat(path_arg); }

/* ====== stat =============================================================================================================================================================== */
static void do_stat(const char *path_arg) {
    char p[128]; build_abs(path_arg, p);
    vfs_stat_t st;
    if (vfs_stat(p,&st)<0) { C_ERR(); cprintf("stat: %s: not found\n",path_arg); C_NRM(); return; }
    cprintf("  File: %s\n  Size: %u\n  Type: %s\n",
        p, (uint32_t)st.size, (st.type&VFS_DIR)?"directory":"regular file");
}

/* =================================================================================================================================================================================
 *  v10 GAMES
 * ================================================================================================================================================================================= */

/* ====== SNAKE ============================================================================================================================================================ */
#define SN_W  40
#define SN_H  20
#define SN_MAXLEN 200

void game_snake(void) {
    con_clear();
    C_HDR(); con_writeln("  SNAKE === Arrow keys to move, Q to quit"); C_NRM();
    con_writeln("");

    /* State */
    int sx[SN_MAXLEN], sy[SN_MAXLEN];
    int slen = 4;
    int dx = 1, dy = 0;
    int fx, fy;
    int score = 0;
    int dead = 0;

    /* Init snake in center */
    for (int i = 0; i < slen; i++) { sx[i] = SN_W/2 - i; sy[i] = SN_H/2; }
    fx = 10; fy = 5;

    /* Simple pseudo-random food placement using uptime */
    uint32_t rseed = sched_uptime_ticks();
    #define SN_RAND() (rseed = rseed * 1664525u + 1013904223u)

    /* Draw border helper - uses terminal directly */
    #define SN_CX  2
    #define SN_CY  3

    /* Initial draw */
    while (!dead) {
        /* Clear play area */
        for (int row = 0; row <= SN_H+1; row++) {
            terminal_set_cursor(SN_CY + row, SN_CX);
            for (int col = 0; col <= SN_W+1; col++) con_putchar(' ');
        }

        /* Draw border */
        terminal_set_cursor(SN_CY, SN_CX);
        con_putchar('+');
        for (int c = 0; c < SN_W; c++) con_putchar('-');
        con_putchar('+');

        terminal_set_cursor(SN_CY + SN_H + 1, SN_CX);
        con_putchar('+');
        for (int c = 0; c < SN_W; c++) con_putchar('-');
        con_putchar('+');

        for (int r = 1; r <= SN_H; r++) {
            terminal_set_cursor(SN_CY + r, SN_CX); con_putchar('|');
            terminal_set_cursor(SN_CY + r, SN_CX + SN_W + 1); con_putchar('|');
        }

        /* Draw food */
        terminal_set_cursor(SN_CY + 1 + fy, SN_CX + 1 + fx);
        C_ACC(); con_putchar('*'); C_NRM();

        /* Draw snake */
        for (int i = 0; i < slen; i++) {
            if (sx[i] < 0 || sx[i] >= SN_W || sy[i] < 0 || sy[i] >= SN_H) continue;
            terminal_set_cursor(SN_CY + 1 + sy[i], SN_CX + 1 + sx[i]);
            if (i == 0) { C_OK(); con_putchar('@'); }
            else        { C_NRM(); con_putchar('o'); }
        }
        C_NRM();

        /* Score */
        terminal_set_cursor(SN_CY + SN_H + 2, SN_CX);
        cprintf("Score: %d   Length: %d   (Q=quit)", score, slen);

        /* Wait for input === ~150ms polling */
        int moved = 0;
        uint32_t t0 = sched_uptime_ticks();
        while (!moved) {
            uint32_t now = sched_uptime_ticks();
            if (now - t0 > 15) break; /* 150ms at 100Hz */
            int k = keyboard_poll();
            if (!k) { __asm__ volatile("pause"); continue; }
            if (k == KEY_UP    && dy != 1)  { dx=0; dy=-1; moved=1; }
            if (k == KEY_DOWN  && dy != -1) { dx=0; dy=1;  moved=1; }
            if (k == KEY_LEFT  && dx != 1)  { dx=-1;dy=0;  moved=1; }
            if (k == KEY_RIGHT && dx != -1) { dx=1; dy=0;  moved=1; }
            if (k=='q'||k=='Q') { dead=1; moved=1; }
        }
        if (dead) break;

        /* Move snake */
        int nx = sx[0] + dx;
        int ny = sy[0] + dy;

        /* Wall collision */
        if (nx < 0 || nx >= SN_W || ny < 0 || ny >= SN_H) { dead = 1; break; }

        /* Self collision */
        for (int i = 1; i < slen; i++)
            if (sx[i] == nx && sy[i] == ny) { dead = 1; break; }
        if (dead) break;

        /* Shift body */
        for (int i = slen - 1; i > 0; i--) { sx[i]=sx[i-1]; sy[i]=sy[i-1]; }
        sx[0] = nx; sy[0] = ny;

        /* Food eaten? */
        if (nx == fx && ny == fy) {
            score += 10;
            if (slen < SN_MAXLEN) slen++;
            /* New food position */
            SN_RAND();
            fx = (int)(rseed % (uint32_t)SN_W);
            SN_RAND();
            fy = (int)(rseed % (uint32_t)SN_H);
            /* Make sure not on snake */
            for (int i = 0; i < slen; i++)
                if (sx[i]==fx && sy[i]==fy) { fx=(fx+3)%SN_W; fy=(fy+3)%SN_H; }
        }
    }

    con_clear();
    C_HDR(); con_writeln(""); con_writeln("  ~~~ GAME OVER ~~~"); C_NRM();
    cprintf("  Final score: %d   Length: %d\n\n", score, slen);
    C_DIM(); con_writeln("  Press any key to return..."); C_NRM();
    keyboard_getkey();
    con_clear();
    #undef SN_RAND
    #undef SN_CX
    #undef SN_CY
}

/* ====== TETRIS ========================================================================================================================================================= */
#define TT_W   10
#define TT_H   20
#define TT_CX   3
#define TT_CY   2

/* 7 tetrominoes, each 4 rotations, stored as 4x4 bitmask */
static const uint16_t tt_pieces[7][4] = {
    /* I */ { 0x0F00, 0x2222, 0x00F0, 0x4444 },
    /* O */ { 0x6600, 0x6600, 0x6600, 0x6600 },
    /* T */ { 0x0E40, 0x4C40, 0x4E00, 0x4640 },
    /* S */ { 0x06C0, 0x4620, 0x06C0, 0x4620 },
    /* Z */ { 0x0C60, 0x2640, 0x0C60, 0x2640 },
    /* J */ { 0x44C0, 0x8E00, 0x6440, 0x0E20 },
    /* L */ { 0x4460, 0x0E80, 0xC440, 0x2E00 },
};

static const char tt_chars[7] = { '#','O','T','S','Z','J','L' };

static uint8_t tt_board[TT_H][TT_W]; /* 0=empty, 1-7=piece color+1 */

static int tt_piece_cell(int piece, int rot, int r, int c) {
    uint16_t mask = tt_pieces[piece][rot & 3];
    int bit = r*4 + c;
    return (mask >> (15 - bit)) & 1;
}

static int tt_fits(int piece, int rot, int px, int py) {
    for (int r=0;r<4;r++) for (int c=0;c<4;c++) {
        if (!tt_piece_cell(piece, rot, r, c)) continue;
        int bx = px+c, by = py+r;
        if (bx<0||bx>=TT_W||by>=TT_H) return 0;
        if (by>=0 && tt_board[by][bx]) return 0;
    }
    return 1;
}

static void tt_place(int piece, int rot, int px, int py) {
    for (int r=0;r<4;r++) for (int c=0;c<4;c++) {
        if (!tt_piece_cell(piece, rot, r, c)) continue;
        int bx=px+c, by=py+r;
        if (by>=0&&by<TT_H&&bx>=0&&bx<TT_W) tt_board[by][bx]=(uint8_t)(piece+1);
    }
}

static int tt_clear_lines(void) {
    int cleared = 0;
    for (int r = TT_H-1; r >= 0; r--) {
        int full = 1;
        for (int c=0;c<TT_W;c++) if (!tt_board[r][c]) { full=0; break; }
        if (full) {
            cleared++;
            /* Shift rows down */
            for (int rr=r;rr>0;rr--)
                for (int c=0;c<TT_W;c++) tt_board[rr][c]=tt_board[rr-1][c];
            for (int c=0;c<TT_W;c++) tt_board[0][c]=0;
            r++; /* re-check this row */
        }
    }
    return cleared;
}

static void tt_draw_board(int piece, int rot, int px, int py, int score, int lines) {
    /* Draw border + board */
    for (int r=0;r<TT_H;r++) {
        terminal_set_cursor(TT_CY+r, TT_CX-1);
        con_putchar('|');
        for (int c=0;c<TT_W;c++) {
            terminal_set_cursor(TT_CY+r, TT_CX+c);
            uint8_t cell = tt_board[r][c];
            /* Check if active piece covers this cell */
            int in_piece = 0;
            for (int pr=0;pr<4;pr++) for (int pc=0;pc<4;pc++) {
                if (tt_piece_cell(piece,rot,pr,pc) && px+pc==c && py+pr==r)
                    in_piece=1;
            }
            if (in_piece) { C_ACC(); con_putchar(tt_chars[piece]); C_NRM(); }
            else if (cell) { C_NRM(); con_putchar('#'); }
            else con_putchar('.');
        }
        terminal_set_cursor(TT_CY+r, TT_CX+TT_W);
        con_putchar('|');
    }
    /* Bottom */
    terminal_set_cursor(TT_CY+TT_H, TT_CX-1);
    con_putchar('+');
    for (int c=0;c<TT_W;c++) con_putchar('-');
    con_putchar('+');
    /* Top label */
    terminal_set_cursor(TT_CY-1, TT_CX-1);
    C_HDR(); con_write("+--TETRIS--+"); C_NRM();
    /* Score */
    terminal_set_cursor(TT_CY+1, TT_CX+TT_W+3);
    cprintf("Score:");
    terminal_set_cursor(TT_CY+2, TT_CX+TT_W+3);
    cprintf("%d     ", score);
    terminal_set_cursor(TT_CY+4, TT_CX+TT_W+3);
    cprintf("Lines:");
    terminal_set_cursor(TT_CY+5, TT_CX+TT_W+3);
    cprintf("%d     ", lines);
    terminal_set_cursor(TT_CY+8, TT_CX+TT_W+3);
    C_DIM(); con_write("Arrows:"); 
    terminal_set_cursor(TT_CY+9, TT_CX+TT_W+3);
    con_write("move");
    terminal_set_cursor(TT_CY+10,TT_CX+TT_W+3);
    con_write("Up=rot");
    terminal_set_cursor(TT_CY+11,TT_CX+TT_W+3);
    con_write("Spc=drop");
    terminal_set_cursor(TT_CY+12,TT_CX+TT_W+3);
    con_write("Q=quit");
    C_NRM();
}

void game_tetris(void) {
    con_clear();

    /* Clear board */
    for (int r=0;r<TT_H;r++) for (int c=0;c<TT_W;c++) tt_board[r][c]=0;

    int score = 0, lines_total = 0;
    int dead = 0;
    uint32_t rseed = sched_uptime_ticks() ^ 0xDEADBEEF;
    #define TT_RAND() (rseed = rseed*1664525u+1013904223u, (int)(rseed%7u))

    int cur_piece = TT_RAND() % 7;
    int cur_rot = 0;
    int cur_x = TT_W/2 - 2;
    int cur_y = 0;
    uint32_t last_drop = sched_uptime_ticks();
    int drop_interval = 50; /* 0.5s at 100Hz */

    while (!dead) {
        /* Render */
        tt_draw_board(cur_piece, cur_rot, cur_x, cur_y, score, lines_total);

        /* Input */
        int k = keyboard_poll();
        if (k) {
            if (k=='q'||k=='Q') break;
            if (k==KEY_LEFT  && tt_fits(cur_piece,cur_rot,cur_x-1,cur_y)) cur_x--;
            if (k==KEY_RIGHT && tt_fits(cur_piece,cur_rot,cur_x+1,cur_y)) cur_x++;
            if (k==KEY_DOWN  && tt_fits(cur_piece,cur_rot,cur_x,cur_y+1)) cur_y++;
            if (k==KEY_UP) {
                int nr=(cur_rot+1)&3;
                if(tt_fits(cur_piece,nr,cur_x,cur_y)) cur_rot=nr;
            }
            if (k==' ') { /* hard drop */
                while(tt_fits(cur_piece,cur_rot,cur_x,cur_y+1)) cur_y++;
            }
        }

        /* Gravity */
        uint32_t now = sched_uptime_ticks();
        if ((int)(now - last_drop) >= drop_interval) {
            last_drop = now;
            if (tt_fits(cur_piece, cur_rot, cur_x, cur_y+1)) {
                cur_y++;
            } else {
                /* Lock piece */
                tt_place(cur_piece, cur_rot, cur_x, cur_y);
                int cl = tt_clear_lines();
                lines_total += cl;
                if (cl==1) score+=100;
                else if(cl==2) score+=300;
                else if(cl==3) score+=600;
                else if(cl==4) score+=1200;
                /* Speed up every 10 lines */
                drop_interval = 50 - (lines_total/10)*4;
                if (drop_interval < 5) drop_interval = 5;
                /* New piece */
                cur_piece = TT_RAND() % 7;
                cur_rot = 0;
                cur_x = TT_W/2-2;
                cur_y = 0;
                if (!tt_fits(cur_piece,cur_rot,cur_x,cur_y)) dead=1;
            }
        }

        /* tiny yield */
        for (int i=0;i<1000;i++) __asm__ volatile("pause");
    }

    con_clear();
    C_HDR(); con_writeln(""); con_writeln("  ~~~ GAME OVER ~~~"); C_NRM();
    cprintf("  Final score: %d   Lines: %d\n\n", score, lines_total);
    C_DIM(); con_writeln("  Press any key..."); C_NRM();
    keyboard_getkey();
    con_clear();
    #undef TT_RAND
}

/* =================================================================================================================================================================================
 *  v10 EASTER EGGS
 * ================================================================================================================================================================================= */

void game_pong(void) {
    con_clear();
    C_HDR(); con_writeln("  PONG === W/S===========, Up/Down=============, Q==========="); C_NRM();
    #define PN_W 58
    #define PN_H 18
    int bx=PN_W/2,by=PN_H/2,bdx=1,bdy=1,l1=PN_H/2-2,l2=PN_H/2-2,sc1=0,sc2=0;
    while(1){
        for(int r=0;r<=PN_H+1;r++){
            terminal_set_cursor(3+r,3);
            if(r==0||r==PN_H+1){for(int c=0;c<=PN_W+1;c++)con_putchar(r==0?'_':'-');}
            else{
                con_putchar('|');
                for(int c=1;c<=PN_W;c++){
                    int row=r-1;
                    if(c==1&&row>=l1&&row<=l1+3){C_OK();con_putchar('#');C_NRM();}
                    else if(c==PN_W&&row>=l2&&row<=l2+3){C_ACC();con_putchar('#');C_NRM();}
                    else if(c==bx&&row==by){C_WRN();con_putchar('O');C_NRM();}
                    else if(c==PN_W/2)con_putchar(':');
                    else con_putchar(' ');
                }
                con_putchar('|');
            }
        }
        terminal_set_cursor(3+PN_H+2,3);
        cprintf("  P1=%d  P2=%d   W/S=left  Up/Dn=right  Q=quit",sc1,sc2);
        int k=keyboard_poll();
        if(k=='q'||k=='Q')break;
        if(k=='w'||k=='W'){if(l1>0)l1--;} if(k=='s'||k=='S'){if(l1<PN_H-4)l1++;}
        if(k==KEY_UP){if(l2>0)l2--;} if(k==KEY_DOWN){if(l2<PN_H-4)l2++;}
        bx+=bdx; by+=bdy;
        if(by<=0||by>=PN_H-1)bdy=-bdy;
        if(bx<=1&&by>=l1&&by<=l1+3)bdx=1;
        if(bx>=PN_W&&by>=l2&&by<=l2+3)bdx=-1;
        if(bx<=0){sc2++;bx=PN_W/2;by=PN_H/2;bdx=1;}
        if(bx>=PN_W+1){sc1++;bx=PN_W/2;by=PN_H/2;bdx=-1;}
        for(int i=0;i<600000;i++)__asm__ volatile("pause");
    }
    con_clear(); C_HDR(); cprintf("\n  PONG P1:%d P2:%d\n\n",sc1,sc2); C_NRM();
    C_DIM();con_writeln("  Press any key...");C_NRM();keyboard_getkey();con_clear();
    #undef PN_W
    #undef PN_H
}

void game_breakout(void) {
    con_clear();
    C_HDR(); con_writeln("  BREAKOUT === Arrows=paddle, Space=launch, Q=quit"); C_NRM();
    #define BK_W 40
    #define BK_H 20
    static uint8_t bk[5][20];
    for(int r=0;r<5;r++)for(int c=0;c<20;c++)bk[r][c]=1;
    int px=BK_W/2-3,pw=7,bx=BK_W/2,by=BK_H-3,bdx=1,bdy=-1,launched=0,score=0,lives=3;
    while(lives>0){
        for(int r=0;r<=BK_H+1;r++){
            terminal_set_cursor(2+r,3);
            if(r==0||r==BK_H+1){for(int c=0;c<=BK_W+1;c++)con_putchar(r==0?'_':'-');}
            else{
                con_putchar('|');
                for(int c=0;c<BK_W;c++){
                    int row=r-1;
                    if(row<5&&(c/2)<20&&bk[row][c/2]){
                        if(c%2==0){C_ACC();con_putchar('[');}else{con_putchar(']');C_NRM();}
                    }else if(row==BK_H-2&&c>=px&&c<px+pw){C_OK();con_putchar('=');C_NRM();}
                    else if(c==bx&&row==by){C_WRN();con_putchar('*');C_NRM();}
                    else con_putchar(' ');
                }
                con_putchar('|');
            }
        }
        terminal_set_cursor(2+BK_H+2,3);
        cprintf("  Score:%d Lives:%d  [<>=move Spc=launch Q=quit]",score,lives);
        int k=keyboard_poll();
        if(k=='q'||k=='Q')break;
        if(k==KEY_LEFT&&px>0)px--; if(k==KEY_RIGHT&&px<BK_W-pw)px++;
        if(k==' '&&!launched)launched=1;
        if(launched){
            bx+=bdx;by+=bdy;
            if(bx<=0){bx=0;bdx=1;} if(bx>=BK_W-1){bx=BK_W-1;bdx=-1;}
            if(by<=0){by=0;bdy=1;}
            if(by==BK_H-2&&bx>=px&&bx<px+pw)bdy=-1;
            if(by>=BK_H-1){lives--;bx=px+pw/2;by=BK_H-3;launched=0;}
            if(by>=0&&by<5&&(bx/2)<20&&bk[by][bx/2]){bk[by][bx/2]=0;score+=10;bdy=1;}
        }else{bx=px+pw/2;}
        for(int i=0;i<800000;i++)__asm__ volatile("pause");
    }
    con_clear(); C_HDR(); cprintf("\n  BREAKOUT Score:%d\n\n",score); C_NRM();
    C_DIM();con_writeln("  Press any key...");C_NRM();keyboard_getkey();con_clear();
    #undef BK_W
    #undef BK_H
}

static void cmd_desktop(int argc, char **argv) {
    if (argc < 2) {
        con_writeln("  desktop style   <caramel|windows|mac>");
        con_writeln("  desktop bg     <color_name|default>");
        con_writeln("  desktop wallpaper <text>");
        con_writeln("  desktop reset");
        con_writeln("  desktop apply  (restart Exp to apply changes)");
        return;
    }

    if (!kstrcmp(argv[1],"style")) {
        if (argc < 3) { C_ERR(); con_writeln("desktop style: missing mode"); C_NRM(); return; }
        if (kstrcmp(argv[2],"caramel")!=0 && kstrcmp(argv[2],"windows")!=0 && kstrcmp(argv[2],"mac")!=0) {
            C_ERR(); con_writeln("desktop style: caramel | windows | mac"); C_NRM(); return;
        }
        sysconf_set("desktop","style",argv[2]);
        sysconf_save();
        C_OK(); cprintf("Desktop style set to '%s'. Type 'de' to apply.\n", argv[2]); C_NRM();
    }
    else if (!kstrcmp(argv[1],"bg")) {
        if (argc < 3) { C_ERR(); con_writeln("desktop bg: missing color"); C_NRM(); return; }
        sysconf_set("desktop","bg_color",argv[2]);
        sysconf_save();
        C_OK(); cprintf("Desktop background color set to '%s'.\n", argv[2]); C_NRM();
    }
    else if (!kstrcmp(argv[1],"wallpaper")) {
        if (argc < 3) { sysconf_set("desktop","wallpaper",""); sysconf_save(); C_OK(); con_writeln("Wallpaper cleared."); C_NRM(); return; }
        sysconf_set("desktop","wallpaper",argv[2]);
        sysconf_save();
        C_OK(); cprintf("Wallpaper text set to '%s'.\n", argv[2]); C_NRM();
    }
    else if (!kstrcmp(argv[1],"reset")) {
        sysconf_set("desktop","style","caramel");
        sysconf_set("desktop","bg_color","default");
        sysconf_set("desktop","wallpaper","");
        sysconf_save();
        C_OK(); con_writeln("Desktop reset to defaults."); C_NRM();
    }
    else if (!kstrcmp(argv[1],"apply")) {
        C_WRN(); con_writeln("Restart DE with 'de' to apply changes."); C_NRM();
    }
    else {
        C_ERR(); cprintf("desktop: unknown subcommand '%s'\n", argv[1]); C_NRM();
    }
}

/* =================================================================================================================================================================================
 *  v10 KERNEL CONFIG COMMAND
 * ================================================================================================================================================================================= */
static void cmd_kernel_cfg(int argc, char *argv[]) {
    if (argc < 2) {
        C_HDR(); con_writeln("atmkoala v0.5 Kernel Configuration"); C_NRM();
        con_writeln("  [kernel] section keys in /uiu/etc/system.conf:\n");
        const struct { const char *k; const char *desc; } opts[] = {
            {"kernel_name",    "OS display name (e.g. MyOS)"},
            {"hostname_color", "Prompt hostname color 0-15"},
            {"boot_msg",       "Custom boot message"},
            {"prompt_format",  "Prompt: %u=user %h=host %d=cwd %$=sigil"},
            {"disable_cmd",    "Comma-list of disabled commands"},
            {"accent_char",    "Char used in decorations (default *)"},
            {"motd_color",     "MOTD text color 0-15"},
            {"sidebar_info",   "Extra info line in 'info' output"},
            {NULL,NULL}
        };
        for (int i=0;opts[i].k;i++) {
            C_ACC(); cprintf("  %-18s", opts[i].k); C_NRM();
            cprintf(": %s\n", opts[i].desc);
        }
        con_writeln("\nUsage: kernel <key> <value>");
        con_writeln("       kernel show");
        con_writeln("       kernel save");
        return;
    }

    if (!kstrcmp(argv[1],"show")) {
        C_HDR(); con_writeln("[kernel] config:"); C_NRM();
        const char *keys[] = {"kernel_name","hostname_color","boot_msg","prompt_format",
                              "disable_cmd","accent_char","motd_color","sidebar_info",NULL};
        for (int i=0;keys[i];i++) {
            const char *v = sysconf_get("kernel",keys[i]);
            C_ACC(); cprintf("  %-18s", keys[i]); C_NRM();
            cprintf("= %s\n", v?v:"(unset)");
        }
        return;
    }
    if (!kstrcmp(argv[1],"save")) {
        sysconf_save();
        C_OK(); con_writeln("Kernel config saved."); C_NRM();
        return;
    }

    if (argc < 3) { C_ERR(); con_writeln("kernel: usage: kernel <key> <value>"); C_NRM(); return; }
    sysconf_set("kernel", argv[1], argv[2]);
    /* Apply immediately where possible */
    if (!kstrcmp(argv[1],"kernel_name")) {
        /* Update SDK personality */
        sdk_personality_t pers;
        const sdk_personality_t *cur = sdk_personality_get();
        kstrcpy(pers.os_name,    argv[2]);
        kstrcpy(pers.os_version, cur->os_version);
        kstrcpy(pers.os_codename,cur->os_codename);
        kstrcpy(pers.os_author,  cur->os_author);
        kstrcpy(pers.os_motd,    cur->os_motd);
        sdk_personality_set(&pers);
        C_OK(); cprintf("Kernel name changed to '%s' (live).\n", argv[2]); C_NRM();
    } else {
        C_OK(); cprintf("kernel.%s = %s\n", argv[1], argv[2]); C_NRM();
    }
}

/* =================================================================================================================================================================================
 *  CONFIG CREATE === interactive wizard
 * ================================================================================================================================================================================= */
static void cmd_config_create(void) {
    C_HDR(); con_writeln("====================================================================================================================================");
    con_writeln(          "===  atmkoala v0.5 Config Creator             ===");
    con_writeln(          "===  Alt+C to open this at any time          ===");
    con_writeln(          "====================================================================================================================================");
    C_NRM(); con_writeln("");

    char buf[128];
    C_WRN(); con_write("  Hostname ["); con_write(hostname); con_write("]: "); C_NRM();
    readline_v6(buf,64); if(buf[0]) { kstrcpy(hostname,buf); sysconf_set("system","hostname",hostname); }

    C_WRN(); con_write("  OS name  [atmkoala]: "); C_NRM();
    readline_v6(buf,64); if(buf[0]) { sysconf_set("kernel","kernel_name",buf); }

    C_WRN(); con_write("  Prompt format (blank=default, %u@%h:%d%$): "); C_NRM();
    readline_v6(buf,128); if(buf[0]) sysconf_set("kernel","prompt_format",buf);

    C_DIM(); con_writeln("  Root password: manage it with 'passwd root'."); C_NRM();

    C_DIM(); con_writeln("  Interface: fixed Paper white theme."); C_NRM();

    C_WRN(); con_write("  Boot message (blank=skip): "); C_NRM();
    readline_v6(buf,128); if(buf[0]) sysconf_set("kernel","boot_msg",buf);

    sysconf_save();
    con_writeln("");
    C_OK(); con_writeln("  Config saved to /uiu/etc/system.conf"); C_NRM();
    con_writeln("");
}

/* =================================================================================================================================================================================
 *  info / fastfetch
 * ================================================================================================================================================================================= */
static void do_info(void) {
    const sdk_personality_t *pers = sdk_personality_get();
    /* Check custom kernel_name */
    const char *kname = sysconf_get("kernel","kernel_name");
    C_HDR(); con_writeln("ATMKoala system information"); C_NRM();
    con_writeln("");
    sdk_cpuid_t cpu; sdk_cpuid(&cpu);
    char upb[32]; uint32_t s=sched_uptime_ticks()/100;
    ksnprintf(upb,sizeof(upb),"%uh %um %us",s/3600,(s/60)%60,s%60);
    struct { const char *k; const char *v; } rows[] = {
        {"OS",       kname ? kname : pers->os_name},
        {"Version",  "0.5"},
        {"Arch",     ATMKOALA_ARCH},
        {"Boot",     "GRUB Multiboot2"},
        {"Mode",     live_mode?"LIVE (RAM)":"INSTALLED (CatFS)"},
        {"Display",  use_vbe?"VBE framebuffer / Exp":"VGA text mode"},
        {"CPU",      cpu.brand[0]?cpu.brand:cpu.vendor},
        {"Shell",    "atsh + atm-box"},
        {"Users",    "roles, UID/GID and VFS permissions"},
        {"Init",     "atm-init runlevels and services"},
        {"Net",      net.initialized?net_mac_str():"not found"},
        {"Disk",     disk_count>0?"ATA PIO + CatFS":"no drive"},
        {"Desktop",  use_vbe?"Exp with PS/2 mouse":"not active"},
        {"Packages", ".tar.zst raw/RLE"},
        {"Uptime",   upb},
        {NULL,NULL}
    };
    for (int i=0;rows[i].k;i++) {
        if (!rows[i].v || !rows[i].v[0]) continue;
        C_ACC(); con_write("  "); int l=(int)kstrlen(rows[i].k);
        con_write(rows[i].k); for(int j=l;j<12;j++) con_putchar(' ');
        C_NRM(); con_write(": "); con_writeln(rows[i].v);
    }
    C_ACC(); con_write("  RAM        : "); C_NRM();
    char mb[16]; kuitoa(g_mem_upper/1024,mb,10); con_write(mb); con_writeln(" MB");
    C_ACC(); con_write("  Heap free  : "); C_NRM();
    kuitoa(heap_free_bytes(),mb,10); con_write(mb); con_writeln(" B");
    C_DIM(); con_writeln(pers->os_motd); C_NRM();
    con_writeln("");
}

/* ====== Script runner ==================================================================================================================================== */
void dispatch(char *line);  /* forward decl (already used above) */

static void run_script(const char *path_arg) {
    char p[128]; build_abs(path_arg, p);
    int fd = vfs_open(p, O_RDONLY, 0);
    if (fd < 0) { C_ERR(); cprintf("sh: %s: not found\n", path_arg); C_NRM(); return; }
    static uint8_t sb[8192]; int n = vfs_read(fd, sb, sizeof(sb)-1); vfs_close(fd);
    if (n <= 0) return; sb[n] = 0;
    char *cur = (char*)sb;
    while (*cur) {
        char *eol = cur;
        while (*eol && *eol != '\n') eol++;
        char save = *eol; *eol = 0;
        char *trimmed = cur;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed && *trimmed != '#') {
            C_DIM(); cprintf("  + %s\n", trimmed); C_NRM();
            dispatch(trimmed);
        }
        *eol = save;
        cur = (*eol == '\n') ? eol+1 : eol;
    }
}

/* ====== hwinfo ========================================================================================================================================================= */
static void do_hwinfo(void) {
    sdk_cpuid_t cpu; sdk_cpuid(&cpu);
    C_HDR(); con_writeln("Hardware Information"); C_NRM();
    con_writeln("===============================================================================================================");
    cprintf("  CPU Vendor : %s\n", cpu.vendor);
    cprintf("  CPU Brand  : %s\n", cpu.brand[0]?cpu.brand:"(unavailable)");
    cprintf("  Stepping   : %u  Model: %u  Family: %u\n",cpu.stepping,cpu.model,cpu.family);
    cprintf("  FPU: %s  MMX: %s  SSE: %s  SSE2: %s  APIC: %s\n",
        cpu.has_fpu?"yes":"no", cpu.has_mmx?"yes":"no",
        cpu.has_sse?"yes":"no", cpu.has_sse2?"yes":"no",
        cpu.has_apic?"yes":"no");
    /* hwinfo is reporting-only. CPU discovery happened once during boot;
       never restart timer, MSR or board probes from an interactive command. */
    cprintf("  Policy    : %s; codename: %s; NX: %s; HTT: %s\n",
        cpu_compat_check(&g_cpu)==0?"compatible":"unsupported",g_cpu.codename,
        g_cpu.has_nx?"yes":"no",g_cpu.has_htt?"yes":"no");
    if(g_cpu.tsc_freq_hz) cprintf("  TSC        : %u MHz (CPUID)\n",(uint32_t)(g_cpu.tsc_freq_hz/1000000ULL));
    else con_writeln("  TSC        : unavailable (no calibrated timer probe)");
    cprintf("  RAM        : %u MB (%u KB)\n", g_mem_upper/1024, g_mem_upper);
    cprintf("  Heap Base  : 0x%x  Size: %uKB\n", 0x400000u, HEAP_SIZE/1024);
    cprintf("  Free Heap  : %u B\n", heap_free_bytes());
    cprintf("  Process RAM: %u B resident across %u non-idle task(s)\n",(uint32_t)sched_total_resident_bytes(),sched_task_count());
    cprintf("  CR0        : 0x%x\n", cpu_cr0());
    con_writeln("===============================================================================================================");
    cprintf("  Disks      : %d ATA drive(s)\n", disk_count);
    for (int i=0;i<DISK_MAX_DRIVES;i++){
        if (!disk_drives[i].present) continue;
        cprintf("    hd%c: %u sectors (%u MB)  %s\n",
            'a'+i, disk_drives[i].sectors, disk_drives[i].sectors/2048,
            disk_drives[i].model);
    }
    cprintf("  Network    : %s\n", net.initialized?"RTL8139 detected":"none");
    atm_gfx_capabilities_t gfx;
    if(mesa_foundation_query(&gfx)==0){
        cprintf("  Display    : %s\n",gfx.display_backend);
        cprintf("  Renderer   : %s\n",gfx.renderer_backend);
        cprintf("  Gfx ABI    : foundation v%u; acceleration=%s\n",gfx.abi_version,(gfx.capabilities&ATM_GFX_CAP_HW_ACCELERATION)?"yes":"no");
    } else cprintf("  Display    : %s\n", use_vbe?"VBE framebuffer":"VGA text mode");
}

/* ====== /proc virtual files ================================================================================================================== */
static void proc_write(const char *name) {
    char p[64]; kstrcpy(p, "/proc/"); kstrcat(p, name);
    int fd = vfs_open(p, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) return;
    char buf[512];
    if (kstrcmp(name,"version")==0) {
        ksnprintf(buf,sizeof(buf),
            "atmkoala version 0.5 (atmkoala@localhost) "
            "(gcc version 13.0 (atmkoala GCC)) #1 SMP PREEMPT\n");
    } else if (kstrcmp(name,"uptime")==0) {
        uint32_t s=sched_uptime_ticks()/100;
        ksnprintf(buf,sizeof(buf),"%u.00 0.00\n",s);
    } else if (kstrcmp(name,"cpuinfo")==0) {
        sdk_cpuid_t cpu; sdk_cpuid(&cpu);
        ksnprintf(buf,sizeof(buf),
            "processor\t: 0\nvendor_id\t: %s\n"
            "model name\t: %s\n"
            "cpu family\t: %u\nmodel\t\t: %u\nstepping\t: %u\n"
            "fpu\t\t: %s\ncpuid level\t: 1\n",
            cpu.vendor, cpu.brand[0]?cpu.brand:"Unknown",
            cpu.family, cpu.model, cpu.stepping,
            cpu.has_fpu?"yes":"no");
    } else if (kstrcmp(name,"meminfo")==0) {
        uint32_t total=(g_mem_upper)*1024, free2=heap_free_bytes();
        ksnprintf(buf,sizeof(buf),
            "MemTotal:     %u kB\nMemFree:      %u kB\n"
            "MemAvailable: %u kB\nHeapUsed:     %u kB\n",
            total/1024, free2/1024, free2/1024, heap_used_bytes()/1024);
    } else if (kstrcmp(name,"cmdline")==0) {
        kstrcpy(buf,"atmkoala root=/dev/ram0 rw console=tty0\n");
    } else {
        buf[0]=0;
    }
    vfs_write(fd,(uint8_t*)buf,(uint32_t)kstrlen(buf)); vfs_close(fd);
}

static void init_proc_files(void) {
    vfs_mkdir("/proc", 0755);
    const char *pfiles[] = {"version","uptime","cpuinfo","meminfo","cmdline","mounts",NULL};
    for (int i=0; pfiles[i]; i++) proc_write(pfiles[i]);
}

/* ==============================================================================================================================================================================
 *  COMMAND DISABLED CHECK
 * ============================================================================================================================================================================== */
static int cmd_is_disabled(const char *cmd) {
    const char *dis = sysconf_get("kernel","disable_cmd");
    if (!dis || !dis[0]) return 0;
    /* dis is comma-separated list */
    char tmp[256]; kstrcpy(tmp, dis);
    char *p = tmp;
    while (*p) {
        char *comma = p;
        while (*comma && *comma != ',') comma++;
        char save = *comma; *comma = 0;
        if (kstrcmp(p, cmd) == 0) return 1;
        *comma = save;
        p = (*comma == ',') ? comma+1 : comma;
    }
    return 0;
}

static void console_btrfs_status(void) {
    if(!btrfs.valid){con_writeln("btrfs: no valid superblock selected");return;}
    char id[48];int p=0;static const char hx[]="0123456789abcdef";
    for(int i=0;i<16;i++){id[p++]=hx[btrfs.fsid[i]>>4];id[p++]=hx[btrfs.fsid[i]&15];if(i==3||i==5||i==7||i==9)id[p++]='-';}id[p]=0;
    cprintf("btrfs: drive %d partition %d, selected mirror %d, generation %u\n",btrfs.drive,btrfs.part,btrfs.selected_mirror,(uint32_t)btrfs.generation);
    cprintf("       label '%s', total %u MiB, used %u MiB, fsid %s\n",btrfs.label,(uint32_t)(btrfs.total_bytes>>20),(uint32_t)(btrfs.bytes_used>>20),id);
    cprintf("       integrity: %s; label write: %s; file/tree writes unsupported\n",btrfs.trusted?"CRC32C verified":"UNVERIFIED checksum",btrfs.write_enabled?"enabled":"disabled");
    for(int i=0;i<BTRFS_MIRROR_MAX;i++){btrfs_mirror_t*m=&btrfs.mirrors[i];if(!m->offset)continue;cprintf("       mirror %d @%u MiB: %s magic=%s crc32c=%s gen=%u\n",i,(uint32_t)(m->offset>>20),m->present?"read":"unavailable",m->magic_ok?"ok":"bad",m->csum_ok?"ok":"bad",(uint32_t)m->generation);}
}

/* ==============================================================================================================================================================================
 *  Main dispatcher
 * ============================================================================================================================================================================== */
void dispatch(char *line) {
    while (*line == ' ') line++;
    if (!*line) return;

    /* Output redirection */
    char redir_path[128] = {0};
    int  redir_append    = 0;
    char *redir = kstrstr(line, " >> ");
    if (redir) { *redir=0; redir_append=1; build_abs(redir+4, redir_path); }
    else { redir = kstrstr(line, " > ");
           if (redir) { *redir=0; build_abs(redir+3, redir_path); } }

    /* History */
    if (hist_count < HIST_SIZE) kstrcpy(history[hist_count++], line);
    else {
        kmemmove(history[0], history[1], sizeof(char)*(HIST_SIZE-1)*CMD_MAX);
        kstrcpy(history[HIST_SIZE-1], line);
    }

    if (redir_path[0]) {
        redirect_fd = vfs_open(redir_path,
            O_WRONLY|O_CREAT|(redir_append?O_APPEND:O_TRUNC), 0644);
    }

    /* ixpy is parsed from the raw command suffix so nested string quotes are
     * preserved. Generic argv parsing intentionally strips quotes and would
     * turn print(\"hello world\") into an identifier expression. */
    const char *ixpy_source=ixpy_source_suffix(line);
    if(ixpy_source){
        if(cmd_is_disabled("ixpy")){C_ERR();con_writeln("ixpy: command disabled by kernel config");C_NRM();}
        else ixpy_command(ixpy_source);
        goto done;
    }

    char *argv[64]; int argc = parse_args(line, argv, 64);
    if (argc == 0) { if(redirect_fd>=0){vfs_close(redirect_fd);redirect_fd=-1;} return; }
    const char *cmd = argv[0];

    /* ====== Check if command is disabled ====== */
    if (cmd_is_disabled(cmd)) {
        C_ERR(); cprintf("%s: command disabled by kernel config\n", cmd); C_NRM();
        goto done;
    }

    /* sudo: operators and administrators may execute one command as uid 0. */
    if (!kstrcmp(cmd,"sudo")) {
        if (argc<2){C_ERR();con_writeln("sudo: missing command");C_NRM();goto done;}
        if (!do_sudo_auth()) goto done;
        const user_account_t *saved=user_current();
        uint32_t saved_uid=saved?saved->uid:0, saved_gid=saved?saved->gid:0;
        int old=sudo_mode; sudo_mode=1; vfs_set_credentials(0,0);
        char sub[CMD_MAX]; kstrcpy(sub, line+5);
        dispatch(sub); sudo_mode=old; vfs_set_credentials(saved_uid,saved_gid);
        goto done;
    }

    /* su [account]: establish a persistent console session for that account. */
    if (!kstrcmp(cmd,"su")) {
        const char *target=(argc>=2)?argv[1]:"root";
        char password[64]; read_secret("Password: ",password,sizeof(password));
        if(user_login(target,password)==0) { apply_session_environment(); C_OK(); con_write("Switched to "); con_writeln(target); C_NRM(); }
        else { C_ERR(); con_writeln("su: authentication failure"); C_NRM(); }
        goto done;
    }

    /* Account management.  Password characters are never echoed. */
    if (!kstrcmp(cmd,"adduser")) {
        if(!session_is_privileged()) { C_ERR(); con_writeln("adduser: requires administrator privileges"); C_NRM(); goto done; }
        if(argc<2 || argc>3) { C_ERR(); con_writeln("usage: adduser NAME [guest|user|operator]"); C_NRM(); goto done; }
        user_role_t role=ROLE_USER; char pass1[64],pass2[64];
        if(argc==3 && user_parse_role(argv[2],&role)<0) { C_ERR(); con_writeln("adduser: invalid role"); C_NRM(); goto done; }
        if(role==ROLE_ADMIN) { C_ERR(); con_writeln("adduser: the admin role is reserved for root"); C_NRM(); goto done; }
        read_secret("New password: ",pass1,sizeof(pass1)); read_secret("Retype password: ",pass2,sizeof(pass2));
        if(kstrcmp(pass1,pass2)) { C_ERR(); con_writeln("adduser: passwords do not match"); C_NRM(); goto done; }
        if(user_add(argv[1],role,pass1)<0) { C_ERR(); con_writeln("adduser: invalid name, duplicate name, weak password, or account limit reached"); C_NRM(); }
        else { C_OK(); cprintf("adduser: created %s with role %s\n",argv[1],user_role_name(role)); C_NRM(); }
        goto done;
    }
    if (!kstrcmp(cmd,"deluser")) {
        if(!session_is_privileged()) { C_ERR(); con_writeln("deluser: requires administrator privileges"); C_NRM(); goto done; }
        if(argc!=2) { C_ERR(); con_writeln("usage: deluser NAME"); C_NRM(); goto done; }
        if(user_del(argv[1])<0) { C_ERR(); con_writeln("deluser: user not found or protected"); C_NRM(); }
        else { C_OK(); con_writeln("deluser: account deleted"); C_NRM(); }
        goto done;
    }
    if (!kstrcmp(cmd,"usermod")) {
        if(!session_is_privileged()) { C_ERR(); con_writeln("usermod: requires administrator privileges"); C_NRM(); goto done; }
        if(argc!=3) { C_ERR(); con_writeln("usage: usermod NAME guest|user|operator"); C_NRM(); goto done; }
        user_role_t role;
        if(user_parse_role(argv[2],&role)<0 || user_set_role(argv[1],role)<0) { C_ERR(); con_writeln("usermod: invalid user or role"); C_NRM(); }
        else { C_OK(); cprintf("usermod: %s -> %s\n",argv[1],user_role_name(role)); C_NRM(); }
        goto done;
    }
    if (!kstrcmp(cmd,"passwd")) {
        const char *target=(argc>=2)?argv[1]:session_name();
        if(argc>2) { C_ERR(); con_writeln("usage: passwd [NAME]"); C_NRM(); goto done; }
        if(kstrcmp(target,session_name()) && !session_is_privileged()) { C_ERR(); con_writeln("passwd: requires administrator privileges for another account"); C_NRM(); goto done; }
        char pass1[64],pass2[64]; read_secret("New password: ",pass1,sizeof(pass1)); read_secret("Retype password: ",pass2,sizeof(pass2));
        if(kstrcmp(pass1,pass2)) { C_ERR(); con_writeln("passwd: passwords do not match"); C_NRM(); }
        else if(user_set_password(target,pass1)<0) { C_ERR(); con_writeln("passwd: password rejected or user not found"); C_NRM(); }
        else { C_OK(); con_writeln("passwd: password updated"); C_NRM(); }
        goto done;
    }
    if (!kstrcmp(cmd,"login")) {
        if(argc!=2) {
            con_writeln("Available accounts (choose with: login NAME):");
            for(int ui=0;ui<user_count();ui++){const user_account_t *u=user_at(ui);if(u&&u->active)cprintf("  %s  (%s)\n",u->name,user_role_name(u->role));}
            con_writeln("New development accounts use password 'atmkoala' until changed with passwd.");
            goto done;
        }
        char password[64]; read_secret("Password: ",password,sizeof(password));
        if(user_login(argv[1],password)==0) { apply_session_environment(); C_OK(); con_write("Hello, "); con_write(argv[1]); con_writeln("!"); C_NRM(); }
        else { C_ERR(); con_writeln("login: authentication failure"); C_NRM(); }
        goto done;
    }
    if (!kstrcmp(cmd,"logout")) {
        user_logout(); apply_session_environment(); C_OK(); con_writeln("Logged out to the guest session."); C_NRM(); goto done;
    }
    if (!kstrcmp(cmd,"users")) {
        con_writeln("NAME             UID    GID    ROLE       HOME");
        for(int ui=0;ui<user_count();ui++) { const user_account_t *u=user_at(ui); if(u) cprintf("%s  uid=%u gid=%u role=%s home=%s%s\n",u->name,u->uid,u->gid,user_role_name(u->role),u->home,u==user_current()?" *":""); }
        goto done;
    }

    /* Native OpenRC-like service manager.  Services are in-kernel callbacks,
     * therefore this interface deliberately does not claim POSIX init.d support. */
    if (!kstrcmp(cmd,"openrc")) {
        cprintf("atm-init: native callback manager, runlevel %s (%d)\n",atminit_runlevel_name(atminit_runlevel()),(int)atminit_runlevel());
        con_writeln("Use: initctl (interactive) | rc-status | rc-service NAME start|stop|restart|status | rc 1|3|5 | rc-update NAME add|del");
        goto done;
    }
    if (!kstrcmp(cmd,"rc-status")) { initctl_show_status(); goto done; }
    if (!kstrcmp(cmd,"initctl")) { initctl_session(); goto done; }
    if (!kstrcmp(cmd,"rc")) {
        if(argc==1) { cprintf("Runlevel: %s (%d)\n",atminit_runlevel_name(atminit_runlevel()),(int)atminit_runlevel()); goto done; }
        if(!session_is_privileged()) { C_ERR(); con_writeln("rc: requires administrator privileges"); C_NRM(); goto done; }
        atm_runlevel_t level;
        if(argc!=2 || atminit_parse_runlevel(argv[1],&level)<0 || atminit_set_runlevel(level)<0) { C_ERR(); con_writeln("rc: invalid runlevel or service dependency failed"); C_NRM(); }
        else { C_OK(); cprintf("rc: entered %s runlevel\n",atminit_runlevel_name(level)); C_NRM(); }
        goto done;
    }
    if (!kstrcmp(cmd,"rc-service") || !kstrcmp(cmd,"service")) {
        if(argc!=3) { C_ERR(); con_writeln("usage: rc-service NAME start|stop|restart|status"); C_NRM(); goto done; }
        const atm_service_info_t *s=atminit_service_find(argv[1]);
        if(!s) { C_ERR(); con_writeln("rc-service: unknown service"); C_NRM(); goto done; }
        if(!kstrcmp(argv[2],"status")) {
            const char *state=s->state==ATM_SERVICE_STARTED?"started":(s->state==ATM_SERVICE_FAILED?"failed":"stopped");
            cprintf("%s: %s\n",s->name,state); goto done;
        }
        if(!session_is_privileged()) { C_ERR(); con_writeln("rc-service: requires administrator privileges"); C_NRM(); goto done; }
        int result=!kstrcmp(argv[2],"start")?atminit_start(argv[1]):(!kstrcmp(argv[2],"stop")?atminit_stop(argv[1]):(!kstrcmp(argv[2],"restart")?atminit_restart(argv[1]):-1));
        if(result<0) { C_ERR(); con_writeln("rc-service: operation failed (check dependencies)"); C_NRM(); }
        else { C_OK(); cprintf("rc-service: %s %s\n",argv[1],argv[2]); C_NRM(); }
        goto done;
    }
    if (!kstrcmp(cmd,"rc-update")) {
        if(!session_is_privileged()) { C_ERR(); con_writeln("rc-update: requires administrator privileges"); C_NRM(); goto done; }
        if(argc!=3 || (kstrcmp(argv[2],"add") && kstrcmp(argv[2],"del"))) { C_ERR(); con_writeln("usage: rc-update NAME add|del"); C_NRM(); goto done; }
        if(atminit_set_enabled(argv[1],!kstrcmp(argv[2],"add"))<0) { C_ERR(); con_writeln("rc-update: unknown service"); C_NRM(); }
        else { C_OK(); cprintf("rc-update: %s autostart %s\n",argv[1],!kstrcmp(argv[2],"add")?"enabled":"disabled"); C_NRM(); }
        goto done;
    }

    /* ====== v7: [command] alias/remap section in system.conf ====== */
    {
        const char *alias_val = sysconf_get("command", cmd);
        if (alias_val && alias_val[0]) {
            /* Build new command line: alias_val + rest of args */
            char aliased[CMD_MAX];
            kstrcpy(aliased, alias_val);
            for (int ai = 1; ai < argc; ai++) {
                kstrcat(aliased, " ");
                kstrcat(aliased, argv[ai]);
            }
            dispatch(aliased);
            goto done;
        }
    }

    /* ====== Check SDK custom commands ====== */
    {
        sdk_cmd_t *sc = sdk_cmd_find(cmd);
        if (sc) { sc->fn(argc, argv); goto done; }
    }

    /* ====== Built-in commands ====== */
    if (!kstrcmp(cmd,"ls")||!kstrcmp(cmd,"dir"))
        do_ls(argc,argv);
    else if (!kstrcmp(cmd,"ll")){ char *na[]={"ls","-l",argv[1],NULL};do_ls(3,na); }
    else if (!kstrcmp(cmd,"la")){ char *na[]={"ls","-a",argv[1],NULL};do_ls(3,na); }
    else if (!kstrcmp(cmd,"cat"))  { if(argc<2){C_ERR();con_writeln("cat: missing arg");C_NRM();}else for(int i=1;i<argc;i++) do_cat(argv[i]); }
    else if (!kstrcmp(cmd,"view")||!kstrcmp(cmd,"less")) { if(argc<2){C_ERR();con_writeln("view: missing arg");C_NRM();}else do_view(argv[1]); }
    else if (!kstrcmp(cmd,"installer-log")) { do_view("/data/uiu/var/log/installer.log"); }
    else if (!kstrcmp(cmd,"stat")) { if(argc<2){C_ERR();con_writeln("stat: missing arg");C_NRM();}else do_stat(argv[1]); }
    else if (!kstrcmp(cmd,"file")) {
        if(argc<2) goto done;
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0){C_ERR();cprintf("file: %s not found\n",argv[1]);C_NRM();goto done;}
        uint8_t mb2[16]; vfs_read(fd2,mb2,16); vfs_close(fd2);
        file_fmt_t f=fmt_detect(mb2,16,argv[1]);
        cprintf("%s: %s (%s)\n",p,fmt_name(f),fmt_mime(f));
    }
    else if (!kstrcmp(cmd,"hd")||!kstrcmp(cmd,"hexdump")) {
        if(argc<2){C_ERR();con_writeln("hd: missing file");C_NRM();goto done;}
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0){C_ERR();cprintf("hd: %s not found\n",argv[1]);C_NRM();goto done;}
        static uint8_t hb[4096]; int n=vfs_read(fd2,hb,sizeof(hb)); vfs_close(fd2);
        int limit=(argc>=3)?kstrtoi(argv[2]):n;
        sdk_hexdump(hb,(size_t)(limit<n?limit:n),0);
    }
    else if (!kstrcmp(cmd,"wc")) {
        for(int i=1;i<argc;i++){
            char p[128]; build_abs(argv[i],p);
            int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0){C_ERR();cprintf("wc: %s not found\n",argv[i]);C_NRM();continue;}
            static uint8_t wb[8192]; int n=vfs_read(fd2,wb,sizeof(wb)-1); vfs_close(fd2);
            if(n>0){wb[n]=0;wc_result_t r=fmt_wc((char*)wb,(uint32_t)n);cprintf("%d %d %d %s\n",r.lines,r.words,r.chars,argv[i]);}
        }
    }
    else if (!kstrcmp(cmd,"grep")) {
        int recursive=0; const char *pat=NULL; int fstart=argc;
        for(int i=1;i<argc;i++){
            if(!kstrcmp(argv[i],"-r")||!kstrcmp(argv[i],"-R")){recursive=1;continue;}
            if(!pat){pat=argv[i];continue;}
            if(fstart==argc) fstart=i;
        }
        if(!pat||fstart>=argc){C_ERR();con_writeln("grep: usage: grep [-r] <pattern> <file...>");C_NRM();goto done;}
        for(int fi=fstart;fi<argc;fi++){
            char p[128]; build_abs(argv[fi],p);
            int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) continue;
            static uint8_t gb[4096]; int n=vfs_read(fd2,gb,sizeof(gb)-1); vfs_close(fd2);
            if(n<=0) continue; gb[n]=0;
            char *pp=(char*)gb; int lineno=1;
            while(*pp){
                char *eol=pp; while(*eol&&*eol!='\n') eol++;
                char sv=*eol; *eol=0;
                if(kstrstr(pp,pat)){C_ACC();cprintf("%s:%d:",p,lineno);C_NRM();con_writeln(pp);}
                *eol=sv; pp=(*eol=='\n')?eol+1:eol; lineno++;
            }
        }
        (void)recursive;
    }
    else if (!kstrcmp(cmd,"sort")) {
        if(argc<2){C_ERR();con_writeln("sort: missing file");C_NRM();goto done;}
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) goto done;
        static char sb2[4096]; int n=vfs_read(fd2,(uint8_t*)sb2,sizeof(sb2)-1); vfs_close(fd2);
        if(n<=0) goto done; sb2[n]=0;
        static char *lines2[256]; int lc=0;
        lines2[lc++]=sb2;
        for(int i=0;i<n&&lc<255;i++) if(sb2[i]=='\n'){sb2[i]=0;if(i+1<n)lines2[lc++]=sb2+i+1;}
        for(int i=0;i<lc-1;i++) for(int j=0;j<lc-i-1;j++)
            if(kstrcmp(lines2[j],lines2[j+1])>0){char*t=lines2[j];lines2[j]=lines2[j+1];lines2[j+1]=t;}
        for(int i=0;i<lc;i++) con_writeln(lines2[i]);
    }
    else if (!kstrcmp(cmd,"uniq")) {
        if(argc<2){C_ERR();con_writeln("uniq: missing file");C_NRM();goto done;}
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) goto done;
        static uint8_t ub2[4096]; int n=vfs_read(fd2,ub2,sizeof(ub2)-1); vfs_close(fd2);
        if(n<=0) goto done; ub2[n]=0;
        char *pp=(char*)ub2, last2[256]="";
        while(*pp){ char *e=pp; while(*e&&*e!='\n') e++; char sv=*e; *e=0;
            if(kstrcmp(pp,last2)!=0){con_writeln(pp);kstrcpy(last2,pp);} *e=sv; pp=(*e=='\n')?e+1:e; }
    }
    else if (!kstrcmp(cmd,"head")) {
        if(argc<2) goto done;
        int nlines=(argc>=3&&!kstrcmp(argv[1],"-n"))?kstrtoi(argv[2]):10;
        const char *file=(argc>=3&&!kstrcmp(argv[1],"-n"))?argv[3]:argv[1];
        char p[128]; build_abs(file,p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) goto done;
        static uint8_t hb2[4096]; int n=vfs_read(fd2,hb2,sizeof(hb2)-1); vfs_close(fd2);
        if(n<=0) goto done; hb2[n]=0;
        char *pp=(char*)hb2; int row=0;
        while(*pp&&row<nlines){char *e=pp;while(*e&&*e!='\n')e++;char sv=*e;*e=0;con_writeln(pp);*e=sv;pp=(*e=='\n')?e+1:e;row++;}
    }
    else if (!kstrcmp(cmd,"tail")) {
        if(argc<2) goto done;
        int nlines=(argc>=3&&!kstrcmp(argv[1],"-n"))?kstrtoi(argv[2]):10;
        const char *file=(argc>=3&&!kstrcmp(argv[1],"-n"))?argv[3]:argv[1];
        char p[128]; build_abs(file,p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) goto done;
        static uint8_t tb2[4096]; int n=vfs_read(fd2,tb2,sizeof(tb2)-1); vfs_close(fd2);
        if(n<=0) goto done; tb2[n]=0;
        char *lns[512]; int lc=0; lns[lc++]=(char*)tb2;
        for(int i=0;i<n&&lc<511;i++) if(tb2[i]=='\n'){tb2[i]=0;if(i+1<n)lns[lc++]=(char*)tb2+i+1;}
        int start=lc>nlines?lc-nlines:0;
        for(int i=start;i<lc;i++) con_writeln(lns[i]);
    }
    else if (!kstrcmp(cmd,"cut")) {
        char delim='\t'; int field=1;
        const char *file=NULL;
        for(int i=1;i<argc;i++){
            if(argv[i][0]=='-'&&argv[i][1]=='d') delim=argv[i][2]?argv[i][2]:'\t';
            else if(argv[i][0]=='-'&&argv[i][1]=='f') field=kstrtoi(argv[i]+2);
            else file=argv[i];
        }
        if(!file){C_ERR();con_writeln("cut: missing file");C_NRM();goto done;}
        char p[128]; build_abs(file,p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) goto done;
        static uint8_t cb2[4096]; int n=vfs_read(fd2,cb2,sizeof(cb2)-1); vfs_close(fd2);
        if(n<=0) goto done; cb2[n]=0;
        char *pp=(char*)cb2;
        while(*pp){
            char *eol=pp; while(*eol&&*eol!='\n') eol++;
            char sv=*eol; *eol=0;
            char *fp=pp; int f=1;
            while(f<field&&*fp){while(*fp&&*fp!=delim)fp++;if(*fp==delim){fp++;f++;}}
            char *fe=fp; while(*fe&&*fe!=delim) fe++;
            char sfp=*fe; *fe=0; con_writeln(fp); *fe=sfp;
            *eol=sv; pp=(*eol=='\n')?eol+1:eol;
        }
    }
    else if (!kstrcmp(cmd,"tr")) {
        if(argc<4){C_ERR();con_writeln("tr: usage: tr <from> <to> <file>");C_NRM();goto done;}
        char p[128]; build_abs(argv[3],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) goto done;
        static uint8_t tb3[4096]; int n=vfs_read(fd2,tb3,sizeof(tb3)-1); vfs_close(fd2);
        if(n<=0) goto done;
        size_t fl=kstrlen(argv[1]),tl=kstrlen(argv[2]);
        for(int i=0;i<n;i++){
            char c=(char)tb3[i];
            for(size_t j=0;j<fl;j++) if(c==argv[1][j]){c=(j<tl)?argv[2][j]:0;break;}
            if(c) con_putchar(c);
        }
    }
    else if (!kstrcmp(cmd,"tee")) {
        if(argc<2){C_ERR();con_writeln("tee: missing file");C_NRM();goto done;}
        char p[128]; build_abs(argv[1],p);
        C_WRN(); con_writeln("tee: type line then Enter:"); C_NRM();
        char tb4[CMD_MAX]; readline_v6(tb4,CMD_MAX);
        int fd2=vfs_open(p,O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if(fd2>=0){vfs_write(fd2,(uint8_t*)tb4,(uint32_t)kstrlen(tb4));vfs_write(fd2,(uint8_t*)"\n",1);vfs_close(fd2);}
        con_writeln(tb4);
    }
    else if (!kstrcmp(cmd,"find")) {
        if(argc<2) goto done;
        const char *needle=argv[1]; const char *where=(argc>=3)?argv[2]:"/";
        const char *dirs[]={"/","/home","/syls","/syls/bin","/uiu","/uiu/etc","/etc","/data","/tmp",NULL};
        for(int i=0;dirs[i];i++){
            char ns[64][VFS_NAME_MAX + 1]; int c2=0; vfs_listdir(dirs[i],ns,&c2);
            for(int j=0;j<c2;j++){
                char nm[VFS_NAME_MAX + 1]; kstrcpy(nm,ns[j]);
                size_t nl=kstrlen(nm); if(nl&&nm[nl-1]=='/') nm[nl-1]=0;
                if(kstrcmp(nm,needle)==0){
                    con_write(dirs[i]);
                    if(dirs[i][kstrlen(dirs[i])-1]!='/') con_putchar('/');
                    con_writeln(ns[j]);
                }
            }
        }
        (void)where;
    }
    else if (!kstrcmp(cmd,"tree")) {
        char p[128]; if(argc<2) kstrcpy(p,cwd); else build_abs(argv[1],p);
        con_writeln(p);
        char ns[64][VFS_NAME_MAX + 1]; int c2=0; vfs_listdir(p,ns,&c2);
        for(int i=0;i<c2;i++){
            size_t nl=kstrlen(ns[i]); int is_dir=nl&&ns[i][nl-1]=='/';
            C_DIM(); con_write(i==c2-1?"========= ":"========= ");
            if(is_dir) C_ACC(); else C_NRM(); con_writeln(ns[i]);
        }
        C_NRM();
    }
    /* Directory / file ops */
    else if (!kstrcmp(cmd,"cd")) {
        if(argc<2) kstrcpy(cwd,"/home");
        else { char p[128]; build_abs(argv[1],p);
            vfs_stat_t st;
            if(vfs_stat(p,&st)<0||!(st.type&VFS_DIR)){C_ERR();cprintf("cd: %s: not a directory\n",argv[1]);C_NRM();}
            else kstrcpy(cwd,p); }
    }
    else if (!kstrcmp(cmd,"pwd"))   con_writeln(cwd);
    else if (!kstrcmp(cmd,"mkdir")) {
        if(argc<2) goto done;
        char p[128]; build_abs(argv[1],p);
        if(vfs_mkdir(p, 0755)<0){C_ERR();cprintf("mkdir: %s: failed\n",argv[1]);C_NRM();}
        else cprintf("mkdir: created '%s'\n",p);
    }
    else if (!kstrcmp(cmd,"rmdir")||(!kstrcmp(cmd,"rm")&&argc>=2&&!kstrcmp(argv[1],"-d"))) {
        const char *a=(!kstrcmp(cmd,"rmdir"))?argv[1]:argv[2];
        if(!a) goto done;
        char p[128]; build_abs(a,p);
        if(vfs_unlink(p)<0){C_ERR();cprintf("rmdir: %s: failed\n",a);C_NRM();}
    }
    else if (!kstrcmp(cmd,"touch")) {
        for(int i=1;i<argc;i++){char p[128];build_abs(argv[i],p);vfs_create(p, 0644);}
    }
    else if (!kstrcmp(cmd,"rm")) {
        if(argc<2) goto done;
        int force=0;
        for(int i=1;i<argc;i++){
            if(argv[i][0]=='-'){
                for(int j=1;argv[i][j];j++) if(argv[i][j]=='f') force=1;
                continue;
            }
            if(force&&!session_is_privileged()){C_ERR();con_writeln("rm -f: requires administrator privileges");C_NRM();goto done;}
            char p[128]; build_abs(argv[i],p);
            if(vfs_unlink(p)<0){C_ERR();cprintf("rm: %s: not found\n",argv[i]);C_NRM();}
            else cprintf("removed '%s'\n",p);
        }
    }
    else if (!kstrcmp(cmd,"cp")) {
        if(argc<3) goto done;
        char sp[128],dp[128]; build_abs(argv[1],sp); build_abs(argv[2],dp);
        int fs=vfs_open(sp,O_RDONLY, 0);
        if(fs<0){C_ERR();cprintf("cp: %s: not found\n",argv[1]);C_NRM();goto done;}
        int fd2=vfs_open(dp,O_WRONLY|O_CREAT|O_TRUNC, 0644); if(fd2<0){vfs_close(fs);goto done;}
        uint8_t t[512]; int n;
        while((n=vfs_read(fs,t,512))>0) vfs_write(fd2,t,(uint32_t)n);
        vfs_close(fs); vfs_close(fd2); cprintf("'%s' -> '%s'\n",sp,dp);
    }
    else if (!kstrcmp(cmd,"dd")) {
        const char *infile=NULL,*outfile=NULL;uint32_t bs=512,count=0xFFFFFFFFu,skip=0,seek=0;int notrunc=0;
        for(int i=1;i<argc;i++){
            const char *a=argv[i];
            if(!kstrncmp(a,"if=",3))infile=a+3; else if(!kstrncmp(a,"of=",3))outfile=a+3;
            else if(!kstrncmp(a,"bs=",3))bs=kstrtou(a+3,10); else if(!kstrncmp(a,"count=",6))count=kstrtou(a+6,10);
            else if(!kstrncmp(a,"skip=",5))skip=kstrtou(a+5,10); else if(!kstrncmp(a,"seek=",5))seek=kstrtou(a+5,10);
            else if(!kstrcmp(a,"conv=notrunc"))notrunc=1; else {C_ERR();cprintf("dd: unsupported operand %s\n",a);C_NRM();goto done;}
        }
        if(!infile||!outfile||!bs||bs>4096u){C_ERR();con_writeln("dd: usage: dd if=FILE of=FILE [bs=1..4096] [count=N] [skip=N] [seek=N] [conv=notrunc]");C_NRM();goto done;}
        char inpath[128],outpath[128];build_abs(infile,inpath);build_abs(outfile,outpath);
        if(!kstrcmp(inpath,outpath)){C_ERR();con_writeln("dd: input and output must differ");C_NRM();goto done;}
        uint64_t inoff=(uint64_t)skip*bs,outoff=(uint64_t)seek*bs;
        if(inoff>0x7fffffffu||outoff>0x7fffffffu){C_ERR();con_writeln("dd: seek range exceeds current VFS limit");C_NRM();goto done;}
        int infd=vfs_open(inpath,O_RDONLY,0),outfd=vfs_open(outpath,O_WRONLY|O_CREAT|(notrunc?0:O_TRUNC),0644);
        if(infd<0||outfd<0){if(infd>=0)vfs_close(infd);if(outfd>=0)vfs_close(outfd);C_ERR();con_writeln("dd: cannot open input or output path");C_NRM();goto done;}
        if(vfs_lseek(infd,(int64_t)inoff,SEEK_SET)<0||vfs_lseek(outfd,(int64_t)outoff,SEEK_SET)<0){vfs_close(infd);vfs_close(outfd);C_ERR();con_writeln("dd: seek failed");C_NRM();goto done;}
        static uint8_t ddbuf[4096];uint64_t copied=0;uint32_t records=0;int failed=0;
        while(records<count){int64_t nr=vfs_read(infd,ddbuf,bs);if(nr<=0)break;uint32_t off=0;while(off<(uint32_t)nr){int64_t nw=vfs_write(outfd,ddbuf+off,(uint32_t)nr-off);if(nw<=0){failed=1;break;}off+=(uint32_t)nw;}if(failed)break;copied+=(uint64_t)nr;records++;if((uint32_t)nr<bs)break;}
        vfs_close(infd);vfs_close(outfd);if(catfs_vfs_is_mounted())(void)catfs_sync();
        char num[32];ku64toa(copied,num,10);if(failed){C_ERR();con_writeln("dd: write failed");C_NRM();}else cprintf("dd: %u record(s), %s byte(s) copied (VFS files only)\n",records,num);
    }
    else if (!kstrcmp(cmd,"mv")) {
        if(argc<3) goto done;
        char sp[128],dp[128]; build_abs(argv[1],sp); build_abs(argv[2],dp);
        int fs=vfs_open(sp,O_RDONLY, 0); int fd2=vfs_open(dp,O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if(fs<0||fd2<0){if(fs>=0)vfs_close(fs);if(fd2>=0)vfs_close(fd2);goto done;}
        uint8_t t[512]; int n;
        while((n=vfs_read(fs,t,512))>0) vfs_write(fd2,t,(uint32_t)n);
        vfs_close(fs); vfs_close(fd2); vfs_unlink(sp);
    }
    else if (!kstrcmp(cmd,"write")) {
        if(argc<3) goto done;
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_WRONLY|O_CREAT|O_TRUNC, 0644); if(fd2<0) goto done;
        for(int i=2;i<argc;i++){vfs_write(fd2,(uint8_t*)argv[i],(uint32_t)kstrlen(argv[i]));if(i<argc-1)vfs_write(fd2,(uint8_t*)" ",1);}
        vfs_write(fd2,(uint8_t*)"\n",1); vfs_close(fd2);
    }
    else if (!kstrcmp(cmd,"append")) {
        if(argc<3) goto done;
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_WRONLY|O_CREAT|O_APPEND, 0644); if(fd2<0) goto done;
        for(int i=2;i<argc;i++){vfs_write(fd2,(uint8_t*)argv[i],(uint32_t)kstrlen(argv[i]));if(i<argc-1)vfs_write(fd2,(uint8_t*)" ",1);}
        vfs_write(fd2,(uint8_t*)"\n",1); vfs_close(fd2);
    }
    else if (!kstrcmp(cmd,"ln"))    { cprintf("ln: symlink %s -> %s (in-memory only)\n",argc>2?argv[2]:"?",argc>1?argv[1]:"?"); }
    else if (!kstrcmp(cmd,"chmod")) {
        if(argc<3) { C_ERR(); con_writeln("usage: chmod MODE PATH"); C_NRM(); goto done; }
        char path[128]; build_abs(argv[2],path);
        uint32_t mode=kstrtou(argv[1],8);
        if(vfs_chmod(path,mode)<0) { C_ERR(); con_writeln("chmod: permission denied or path not found"); C_NRM(); }
        else { C_OK(); cprintf("chmod: %s -> %s\n",path,argv[1]); C_NRM(); }
    }
    else if (!kstrcmp(cmd,"chown")) {
        if(!session_is_privileged()){C_ERR();con_writeln("chown: requires administrator privileges");C_NRM();goto done;}
        if(argc<3) { C_ERR(); con_writeln("usage: chown USER PATH"); C_NRM(); goto done; }
        const user_account_t *owner=user_find(argv[1]); char path[128]; build_abs(argv[2],path);
        if(!owner || vfs_chown(path,owner->uid,owner->gid)<0) { C_ERR(); con_writeln("chown: invalid user or path"); C_NRM(); }
        else { C_OK(); cprintf("chown: %s owner -> %s\n",path,owner->name); C_NRM(); }
    }
    /* Process / system */
    else if (!kstrcmp(cmd,"ps"))     sched_print_tasks();
    else if (!kstrcmp(cmd,"kill")) {
        if(argc<2){ C_ERR(); con_writeln("kill: missing pid"); C_NRM(); }
        else if(!session_is_privileged()){ C_ERR(); con_writeln("kill: requires administrator privileges"); C_NRM(); }
        else if(task_kill((uint32_t)kstrtoi(argv[1]),15)<0){ C_ERR(); con_writeln("kill: no killable task with that pid"); C_NRM(); }
        else { C_OK(); cprintf("kill: SIGTERM delivered to pid %s\n",argv[1]); C_NRM(); }
    }
    else if (!kstrcmp(cmd,"sleep")) {
        if(argc<2) goto done;
        uint32_t ms=(uint32_t)(kstrtoi(argv[1])*1000);
        sdk_sleep_ms(ms);
    }
    else if (!kstrcmp(cmd,"wait"))   { C_DIM();con_writeln("Waiting... (press any key)");C_NRM(); keyboard_getkey(); }
    else if (!kstrcmp(cmd,"sh"))     { if(argc<2){C_ERR();con_writeln("sh: missing script");C_NRM();}else run_script(argv[1]); }
    else if (!kstrcmp(cmd,"source")||!kstrcmp(cmd,".")) {
        if(argc<2){C_ERR();con_writeln("source: missing file");C_NRM();}else run_script(argv[1]);
    }
    /* System info */
    else if (!kstrcmp(cmd,"uname")) {
        if(argc>1&&!kstrcmp(argv[1],"-a"))
            con_writeln("atmkoala 0.5.0 atmkoala-kernel 0.5.0 #1 SMP x86_64 atmkoala");
        else if(argc>1&&!kstrcmp(argv[1],"-r"))
            con_writeln("0.5.0-atmkoala");
        else con_writeln("atmkoala");
    }
    else if (!kstrcmp(cmd,"uptime")) {
        uint32_t s=sched_uptime_ticks()/100;
        cprintf("up %uh %um %us  load: 0.00 0.00 0.00  users: 1\n",s/3600,(s/60)%60,s%60);
    }
    else if (!kstrcmp(cmd,"mem")||!kstrcmp(cmd,"free")) {
        uint32_t tot=heap_free_bytes()+heap_used_bytes();
        uint64_t resident=sched_total_resident_bytes();
        cprintf("%-16s%8s%8s%8s\nMem:     %8u%8u%8u\nSwap:    %8u%8u%8u\n",
            "","total","used","free",tot,heap_used_bytes(),heap_free_bytes(),0u,0u,0u);
        cprintf("Process resident: %u B across %u non-idle task(s); CPU busy ticks: %u\n",(uint32_t)resident,sched_task_count(),(uint32_t)sched_busy_ticks());
    }
    else if (!kstrcmp(cmd,"dmesg")) {
        C_HDR(); con_writeln("[dmesg] atmkoala v0.5 boot log:"); C_NRM();
        con_writeln("[    0.000] atmkoala v0.5 x86-64 kernel starting");
        con_writeln("[    0.001] GDT: 64-bit descriptors loaded");
        con_writeln("[    0.001] IDT: 256 entries, PIC 8259 remapped IRQ0-15 -> INT32-47");
        con_writeln("[    0.002] PIT: 100Hz");
        con_writeln("[    0.003] Heap: 8MB");
        con_writeln("[    0.003] Keyboard: PS/2 with extended keys");
        cprintf("[    0.004] Display: %s\n", use_vbe?"VBE LFB 800x600x32bpp":"VGA 80x25");
        con_writeln("[    0.005] VFS: ramfs initialized");
        con_writeln("[    0.006] Scheduler: round-robin");
        cprintf("[    0.007] Disk: %d ATA drive(s)\n", disk_count);
        cprintf("[    0.008] Network: %s\n", net.initialized?"RTL8139 detected":"not found");
        con_writeln("[    0.009] UNM: UserNet Manager ready");
        con_writeln("[    0.010] Shell ready");
    }
    else if (!kstrcmp(cmd,"tzif")) {
        if(argc<2){con_writeln("tzif: import <source> <IANA-name> | use <IANA-name> | remove <IANA-name> | status");goto done;}
        if(!kstrcmp(argv[1],"status")){const char *active_name=tzif_active_name();if(active_name[0])cprintf("tzif: active %s (bounded loaded transition set)\n",active_name);else con_writeln("tzif: no archive zone loaded; embedded current-era rules remain active");goto done;}
        if(!kstrcmp(argv[1],"import")&&argc>=4){if(tzif_import(argv[2],argv[3])<0){C_ERR();con_writeln("tzif: import failed (mounted CatFS, regular <=8 KiB TZif source, safe name, or target already exists required)");C_NRM();goto done;}C_OK();con_writeln("tzif: imported; run tzif use <IANA-name> to activate");C_NRM();goto done;}
        if(!kstrcmp(argv[1],"use")&&argc>=3){if(tzif_load(argv[2])<0){C_ERR();con_writeln("tzif: load failed (CatFS mount, safe name, regular file, or bounded TZif v1/v2/v3 validation)");C_NRM();goto done;}if(sysconf_set("system","timezone",argv[2])<0){C_ERR();con_writeln("tzif: loaded but could not save selected name");C_NRM();goto done;}sysconf_save();C_OK();cprintf("tzif: active zone set to %s\n",tzif_active_name());C_NRM();goto done;}
        if(!kstrcmp(argv[1],"remove")&&argc>=3){const char *saved=sysconf_get("system","timezone");if(tzif_remove(argv[2])<0){C_ERR();con_writeln("tzif: remove failed");C_NRM();goto done;}if(saved&&!kstrcmp(saved,argv[2])){sysconf_set("system","timezone","UTC");sysconf_save();}C_OK();con_writeln("tzif: removed; active selection falls back to embedded UTC if needed");C_NRM();goto done;}
        con_writeln("tzif: import <source> <IANA-name> | use <IANA-name> | remove <IANA-name> | status");goto done;
    }
    else if (!kstrcmp(cmd,"ntp")) {
        const char *basis=sysconf_get("system","rtc_basis");
        if(argc<2){con_writeln("ntp: sync <IPv4-or-hostname> | status | write-rtc WRITE_RTC (manual only; UTC RTC required)");goto done;}
        if(!kstrcmp(argv[1],"status")){int64_t correction=0;atm_realtime_correction_seconds(&correction);cprintf("ntp: manual only; volatile UTC correction %+lld seconds; use `ntp write-rtc WRITE_RTC` only after review\n",correction);goto done;}
        if(!kstrcmp(argv[1],"write-rtc")){if(basis&&!kstrcmp(basis,"local")){C_ERR();con_writeln("ntp: RTC writeback requires timezone clock utc");C_NRM();goto done;}if(argc!=3||kstrcmp(argv[2],"WRITE_RTC")){con_writeln("ntp: write-rtc requires exact confirmation: ntp write-rtc WRITE_RTC");goto done;}rtc_datetime_t utc;if(atm_realtime_utc(&utc)<0||rtc_write_datetime(&utc)<0){C_ERR();con_writeln("ntp: RTC writeback failed; volatile correction remains active");C_NRM();goto done;}atm_realtime_clear_correction();C_OK();con_writeln("ntp: confirmed UTC time written to CMOS; volatile correction cleared");C_NRM();goto done;}
        if(!kstrcmp(argv[1],"sync")&&argc>=3){if(basis&&!kstrcmp(basis,"local")){C_ERR();con_writeln("ntp: sync requires timezone clock utc; firmware-local RTC cannot be safely corrected");C_NRM();goto done;}ntp_result_t result;if(ntp_sync_once(argv[2],&result)<0){C_ERR();con_writeln("ntp: sync failed (network, DNS, timeout, or invalid server reply)");C_NRM();goto done;}rtc_datetime_t utc;if(atm_realtime_utc(&utc)<0){C_ERR();con_writeln("ntp: reply accepted but corrected UTC clock is unavailable");C_NRM();goto done;}C_OK();cprintf("ntp: %u.%u.%u.%u stratum %u -> UTC %04d-%02d-%02d %02d:%02d:%02d (%u ms); volatile until reboot\n",result.server_ip[0],result.server_ip[1],result.server_ip[2],result.server_ip[3],result.stratum,utc.year,utc.month,utc.day,utc.hour,utc.minute,utc.second,result.roundtrip_ticks*10u);C_NRM();goto done;}
        con_writeln("ntp: sync <IPv4-or-hostname> | status | write-rtc WRITE_RTC (manual only; UTC RTC required)");goto done;
    }
    else if (!kstrcmp(cmd,"date")) {
        uint32_t s=sched_uptime_ticks()/100;const char *tz=sysconf_get("system","timezone"),*basis=sysconf_get("system","rtc_basis");rtc_datetime_t now;int offset=0,dst=0;
        if((basis&& !kstrcmp(basis,"local")?rtc_read_datetime(&now):atm_realtime_utc(&now))==0){
            if(!basis||kstrcmp(basis,"local")!=0){
                rtc_datetime_t local;if(atm_timezone_convert(tz&&tz[0]?tz:"UTC",&now,&local,&offset,&dst)==0){int a=offset<0?-offset:offset;cprintf("Local %04d-%02d-%02d %02d:%02d:%02d  zone=%s UTC%c%02d:%02d%s\n",local.year,local.month,local.day,local.hour,local.minute,local.second,tz&&tz[0]?tz:"UTC",offset<0?'-':'+',a/60,a%60,dst?" DST":"");con_writeln("CMOS RTC is configured as UTC; local civil time uses ATMKoala's embedded current-era timezone rules.");}
                else {cprintf("CMOS UTC %04d-%02d-%02d %02d:%02d:%02d  zone=%s\n",now.year,now.month,now.day,now.hour,now.minute,now.second,tz&&tz[0]?tz:"UTC");con_writeln("Selected zone is not in the embedded rule table; choose a listed zone.");}
            } else {cprintf("Firmware-local RTC %04d-%02d-%02d %02d:%02d:%02d  zone=%s\n",now.year,now.month,now.day,now.hour,now.minute,now.second,tz&&tz[0]?tz:"UTC");con_writeln("No conversion is applied because timezone clock local was selected.");}
        } else {cprintf("atmkoala uptime %u seconds  timezone=%s\n",s,tz&&tz[0]?tz:"UTC");con_writeln("CMOS RTC date is unavailable or invalid; NTP synchronization is not implemented.");}
    }
    else if (!kstrcmp(cmd,"timezone")) {
        const char *saved_tz=sysconf_get("system","timezone"),*basis=sysconf_get("system","rtc_basis");
        if(argc==1){cprintf("timezone: %s (RTC basis: %s, %u embedded zones)\n",saved_tz&&saved_tz[0]?saved_tz:"UTC",basis&&!kstrcmp(basis,"local")?"local":"UTC",atm_timezone_count());con_writeln("usage: timezone [IANA-style name] | timezone set <name> | timezone now [name] | timezone list | timezone clock utc|local");goto done;}
        if(!kstrcmp(argv[1],"clock")){if(argc<3|| (kstrcmp(argv[2],"utc")&&kstrcmp(argv[2],"local"))){C_ERR();con_writeln("timezone clock: choose utc or local");C_NRM();goto done;}if(sysconf_set("system","rtc_basis",argv[2])<0){C_ERR();con_writeln("timezone: configuration capacity reached");C_NRM();goto done;}sysconf_save();C_OK();cprintf("timezone: firmware RTC basis saved as %s\n",argv[2]);C_NRM();goto done;}
        if(!kstrcmp(argv[1],"list")){uint32_t count=atm_timezone_count();cprintf("Supported embedded zones (%u):\n",count);for(uint32_t i=0;i<count;i++)cprintf("  %3u  %s\n",i+1,atm_timezone_name(i));con_writeln("Use timezone now <name> to preview a zone, or timezone set <name> to save it. Current-era rules only; no NTP or historic TZif database.");goto done;}
        if(!kstrcmp(argv[1],"now")){const char *view=argc>=3?argv[2]:(saved_tz&&saved_tz[0]?saved_tz:"UTC");rtc_datetime_t utc,local;int offset=0,dst=0;if(!atm_timezone_supported(view)){C_ERR();con_writeln("timezone now: choose a listed embedded identifier");C_NRM();goto done;}if(basis&&!kstrcmp(basis,"local")){C_ERR();con_writeln("timezone now: conversion requires timezone clock utc; firmware is configured as local");C_NRM();goto done;}if(rtc_read_datetime(&utc)<0||atm_timezone_convert(view,&utc,&local,&offset,&dst)<0){C_ERR();con_writeln("timezone now: CMOS RTC unavailable or invalid");C_NRM();goto done;}int a=offset<0?-offset:offset;C_OK();cprintf("%s  %04d-%02d-%02d %02d:%02d:%02d  UTC%c%02d:%02d%s\n",view,local.year,local.month,local.day,local.hour,local.minute,local.second,offset<0?'-':'+',a/60,a%60,dst?" DST":"");C_NRM();goto done;}
        const char *tz=!kstrcmp(argv[1],"set")&&argc>=3?argv[2]:argv[1];int valid=tz[0]&&kstrlen(tz)<64;
        for(const char *p=tz;*p&&valid;p++)if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='/'||*p=='_'||*p=='+'||*p=='-'))valid=0;
        if(!valid||!atm_timezone_supported(tz)){C_ERR();con_writeln("timezone: choose a listed identifier with embedded conversion rules");C_NRM();goto done;}
        if(sysconf_set("system","timezone",tz)<0){C_ERR();con_writeln("timezone: configuration capacity reached");C_NRM();goto done;}
        sysconf_save();C_OK();cprintf("timezone: saved %s\n",tz);C_NRM();
    }
    else if (!kstrcmp(cmd,"hostname")) {
        if(argc>1){kstrcpy(hostname,argv[1]);sysconf_set("system","hostname",hostname);}
        else con_writeln(hostname);
    }
    else if (!kstrcmp(cmd,"whoami"))  con_writeln(session_name());
    else if (!kstrcmp(cmd,"id")) {
        const user_account_t *u=user_current();
        if(u) cprintf("uid=%u(%s) gid=%u(%s) role=%s\n",u->uid,u->name,u->gid,u->name,user_role_name(u->role));
    }
    else if (!kstrcmp(cmd,"info"))    do_info();
    else if (!kstrcmp(cmd,"hwinfo"))  do_hwinfo();
    else if (!kstrcmp(cmd,"lscpu"))   { cpu_detect(&g_cpu);cprintf("Vendor: %s\nModel: %s\nFamily: %u Model: %u Stepping: %u (%s)\nSSE: %s SSE2: %s SSE4.2: %s AES: %s HTT: %s NX: %s LM: %s\n",g_cpu.vendor,g_cpu.brand,g_cpu.family,g_cpu.model,g_cpu.stepping,g_cpu.codename,g_cpu.has_sse?"yes":"no",g_cpu.has_sse2?"yes":"no",g_cpu.has_sse4_2?"yes":"no",g_cpu.has_aes?"yes":"no",g_cpu.has_htt?"yes":"no",g_cpu.has_nx?"yes":"no",g_cpu.has_lm?"yes":"no"); }
    else if (!kstrcmp(cmd,"cpucompat")){ cpu_detect(&g_cpu);if(cpu_compat_check(&g_cpu)<0)cprintf("cpucompat: UNSUPPORTED — ATMKoala needs x86-64 long mode and SSE2.\n");else cprintf("cpucompat: compatible — x86-64 long mode and SSE2 present.\n");if(g_cpu.family==15)cprintf("cpucompat: NetBurst policy; only 64-bit Prescott-class P4 qualifies; NX may be absent.\n");if(g_cpu.family==6&&g_cpu.model==0x7A)cprintf("cpucompat: Gemini Lake J4105 profile selected.\n"); }
    else if (!kstrcmp(cmd,"awm") || !kstrcmp(cmd,"xserver")){ if(!use_vbe){C_ERR();con_writeln("awm: VBE framebuffer required");C_NRM();}else awm_print_status(); }
    else if (!kstrcmp(cmd,"font")){
        if(argc<2||!kstrcmp(argv[1],"status")) cprintf("font: %s, %dx%d, scale %d\n",ttf_loaded()?"PSF2 loaded":"built-in bitmap fallback",ttf_glyph_width(),ttf_glyph_height(),ttf_get_scale());
        else if(!kstrcmp(argv[1],"scale")&&argc>=3){int s=kstrtoi(argv[2]);ttf_set_scale(s);char sb[4];kitoa(ttf_get_scale(),sb,10);sysconf_set("desktop","font_scale",sb);sysconf_save();if(exp_is_active())exp_request_full_redraw();}
        else if(!kstrcmp(argv[1],"load")&&argc>=3){char fp[128];build_abs(argv[2],fp);if(ttf_load_psf(fp)<0){C_ERR();con_writeln("font: PSF2 load failed");C_NRM();}else{sysconf_set("desktop","psf_font",fp);sysconf_save();if(exp_is_active())exp_request_full_redraw();C_OK();con_writeln("font: PSF2 font loaded");C_NRM();}}
        else con_writeln("font: status | scale <1..4> | load <psf2-path>");
    }
    else if (!kstrcmp(cmd,"env"))     sdk_env_list();
    else if (!kstrcmp(cmd,"set")&&argc>=3)  sdk_env_set(argv[1],argv[2]);
    else if (!kstrcmp(cmd,"unset"))   { if(argc>=2) sdk_env_unset(argv[1]); }
    else if (!kstrcmp(cmd,"export"))  { if(argc>=2){ char *eq=kstrchr(argv[1],'='); if(eq){*eq=0;sdk_env_set(argv[1],eq+1);}else sdk_env_set(argv[1],"");} }
    else if (!kstrcmp(cmd,"printenv")){ if(argc>=2){const char*v=sdk_env_get(argv[1]);if(v)con_writeln(v);}else sdk_env_list(); }
    /* Disk / FS */
    else if (!kstrcmp(cmd,"lsblk"))  {
        if(argc>=2 && !kstrcmp(argv[1],"--rescan")){
            if(catfs.mounted){C_WRN();con_writeln("lsblk: rescan refused while CatFS is mounted; unmount first.");C_NRM();goto done;}
            disk_init();
        }
        con_writeln("NAME  TYPE   SIZE    PTYPE  FS       MODEL / RANGE");
        for(int i=0;i<DISK_MAX_DRIVES;i++){
            if(!disk_drives[i].present) continue;
            uint32_t mib=disk_capacity_mib(i);
            cprintf("hd%c   disk  %5uM  ATA    -        %s\n",'a'+i,mib,disk_drives[i].model[0]?disk_drives[i].model:"ATA PIO disk");
            mbr_table_t table;int mbr_rc=mbr_read(i,&table);
            if(mbr_rc==0 && mbr_validate_drive(i,&table)==0){
                for(int p=0;p<PART_MAX_ENTRIES;p++){
                    mbr_entry_t *e=&table.entries[p];
                    if(!e->type || !e->sector_count) continue;
                    cprintf("hd%c%d  part  %5uM  0x%x  %s  lba=%u sectors=%u (%s)\n",'a'+i,p+1,e->sector_count/2048u,e->type,mbr_probe_filesystem(i,e),e->lba_start,e->sector_count,part_type_name(e->type));
                }
            } else if(mbr_rc<0) {
                cprintf("hd%c   table  -      -      -        no MBR 55AA signature or sector read failure\n",'a'+i);
            } else {
                cprintf("hd%c   table  -      -      -        MBR signature present but ranges/status overlap or capacity validation failed\n",'a'+i);
            }
        }
        if(!disk_count){
            int transports=0;C_WRN();con_writeln("No ATA PIO block disks detected.");
            for(int i=0;i<g_pci.count;i++){pci_device_t*p=&g_pci.devs[i];if(p->class_code!=0x01)continue;const char *kind=p->subclass==0x06?"AHCI":p->subclass==0x08?"NVMe":"other storage";cprintf("  PCI %02x:%02x.%u: %s controller %04x:%04x detected, but no block driver is bound.\n",p->bus,p->dev,p->fn,kind,p->vendor,p->device);transports++;}
            if(!transports)con_writeln("  No PCI AHCI/NVMe storage controller was discovered on scanned buses.");
            con_writeln("USB flash media also needs xHCI/EHCI enumeration plus BOT/SCSI mass-storage; it is not exposed as a block device yet.");C_NRM();
        }
    }
    else if (!kstrcmp(cmd,"swap")) {
        int found=0;
        con_writeln("swap: inventory only; ATMKoala has no virtual-memory pager or active swap backing store.");
        for(int i=0;i<DISK_MAX_DRIVES;i++){
            mbr_table_t table;if(!disk_drives[i].present||mbr_read(i,&table)<0||mbr_validate_drive(i,&table)<0)continue;
            for(int p=0;p<PART_MAX_ENTRIES;p++){mbr_entry_t *e=&table.entries[p];if(e->type!=PART_TYPE_LINUX_SWAP||!e->sector_count)continue;cprintf("  hd%c%d: Linux-swap MBR type, %u MiB, LBA %u (detected; inactive)\n",'a'+i,p+1,e->sector_count/2048u,e->lba_start);found++;}
        }
        if(!found)con_writeln("  no primary MBR Linux-swap (0x82) partition detected on ATA PIO disks.");
        con_writeln("  No page file, swap partition activation, eviction, or swap I/O driver is implemented.");
    }
    else if (!kstrcmp(cmd,"gpu")) {
        int found=0;con_writeln("=== PCI graphics diagnostics ===");
        for(int i=0;i<g_pci.count;i++){
            pci_device_t *p=&g_pci.devs[i];if(p->class_code!=0x03)continue;found++;
            const char *vendor=p->vendor==PCI_VENDOR_INTEL?"Intel":p->vendor==0x10DE?"NVIDIA":p->vendor==0x1002?"AMD":"unknown vendor";
            const char *state="PCI device detected; no native hardware GPU driver";
            if(p->vendor==PCI_VENDOR_INTEL&&p->device==PCI_DEV_UHD600)state=g_i915.initialized?"Intel UHD 600: boot framebuffer adopted; native modeset/3D unavailable":"Intel UHD 600 detected; boot framebuffer unavailable";
            cprintf("  %02x:%02x.%u  %s %04x:%04x class 03:%02x IRQ%u — %s\n",p->bus,p->dev,p->fn,vendor,p->vendor,p->device,p->subclass,p->irq,state);
        }
        if(!found)con_writeln("  no PCI display controller (class 0x03) found.");
        con_writeln("Display output uses a boot-provided framebuffer where available; UHD 600 detection does not program display registers, modeset, GTT or 3D.");
    }
    else if (!kstrcmp(cmd,"usb")) {
        int hosts=0;
        C_HDR();con_writeln("USB controller discovery");C_NRM();
        for(int i=0;i<g_pci.count;i++){
            pci_device_t *p=&g_pci.devs[i];
            if(p->class_code!=0x0C || p->subclass!=0x03) continue;
            const char *kind=p->prog_if==0x00?"UHCI":p->prog_if==0x10?"OHCI":p->prog_if==0x20?"EHCI":p->prog_if==0x30?"xHCI":"unknown";
            cprintf("  %02x:%02x.%u  %s host controller (interface=0x%x) vendor=%04x device=%04x BAR0=0x%x\n",p->bus,p->dev,p->fn,kind,p->prog_if,p->vendor,p->device,p->bar[0]);
            hosts++;
        }
        if(!hosts) con_writeln("  no PCI USB host controller detected");
        C_DIM();con_writeln("PCI USB discovery is available, but USB enumeration, xHCI/EHCI scheduling, HID mouse reports and BOT/SCSI mass-storage are not implemented; no USB mouse or flash disk is exposed by this build yet.");C_NRM();
    }
    else if (!kstrcmp(cmd,"mouse")) {
        const mouse_state_t *ms=mouse_state();
        cprintf("mouse: %s  available=%s pos=%d,%d buttons=0x%x packets=%u dropped=%u\n",mouse_status_string(),ms&&ms->available?"yes":"no",ms?ms->x:0,ms?ms->y:0,ms?ms->buttons:0,ms?ms->packets:0,ms?ms->dropped_packets:0);
        if(ms)cprintf("mouse: irq-bytes=%u sync-loss=%u controller-drained=%u\n",ms->irq_bytes,ms->sync_losses,ms->controller_drained);
        if(!ms||!ms->available)con_writeln("mouse: only legacy PS/2 auxiliary input is implemented; USB/HID mouse transport is not available yet.");
    }
    else if (!kstrcmp(cmd,"df")) {
        con_writeln("Filesystem  1K-blocks   Used  Avail  Use%  Mounted");
        con_writeln("ramfs             n/a    n/a    n/a     -  /");
        if(catfs.mounted){
            uint32_t t=catfs.sb.total_blocks*512/1024, f=catfs.sb.free_blocks*512/1024;
            cprintf("qewoxfs    %8u %6u %6u  %3u%%  /data\n",t,t-f,f,t?(t-f)*100/t:0);
        }
    }
    else if (!kstrcmp(cmd,"du")) {
        if(argc<2) goto done;
        char p[128]; build_abs(argv[1],p); vfs_stat_t st;
        if(vfs_stat(p,&st)==0) cprintf("%u\t%s\n",(st.size+1023)/1024,p);
    }
    else if (!kstrcmp(cmd,"mount")) {
        int drv=0,part=-1,rc=-1;const char *dev=argc>=2?argv[1]:"hda";
        if(dev[0]=='h'&&dev[1]=='d'&&dev[2]>='a'&&dev[2]<'a'+DISK_MAX_DRIVES){drv=dev[2]-'a';if(dev[3]>='1'&&dev[3]<='4'&&dev[4]==0)part=dev[3]-'1';}
        if(!disk_drives[drv].present){C_ERR();cprintf("mount: hd%c: not found\n",'a'+drv);C_NRM();goto done;}
        if(part>=0){mbr_table_t table;if(mbr_read(drv,&table)==0&&mbr_validate_drive(drv,&table)==0&&table.entries[part].type&&table.entries[part].sector_count)rc=catfs_mount_at(drv,table.entries[part].lba_start);}
        else rc=catfs_mount(drv);
        if(rc<0){C_WRN();con_writeln("mount: target is not a valid CatFS volume");C_NRM();goto done;}
        if(catfs_vfs_mount("/data")<0){catfs_sync();catfs.mounted=0;C_ERR();con_writeln("mount: VFS adapter failed");C_NRM();goto done;}
        live_mode=0; C_OK(); cprintf("Mounted %s on /data\n",dev); C_NRM();
    }
    else if (!kstrcmp(cmd,"umount")) {
        catfs_vfs_unmount(); live_mode=1; catfs_sync(); catfs.mounted=0;
        C_OK(); con_writeln("Unmounted."); C_NRM();
    }
    else if (!kstrcmp(cmd,"mkfs")) {
        int drv=0; if(argc>=2) drv=argv[argc-1][2]-'a';
        if(!disk_drives[drv].present){C_ERR();con_writeln("mkfs: no drive");C_NRM();goto done;}
        C_WRN(); cprintf("Format hd%c? [y/N] ",'a'+drv); C_NRM();
        char a[4]; readline_v6(a,4);
        if(a[0]!='y'&&a[0]!='Y'){con_writeln("Cancelled.");goto done;}
        if(catfs_vfs_is_mounted()) catfs_vfs_unmount();
        if(catfs_format(drv,"catfs")<0){C_ERR();con_writeln("mkfs: failed");C_NRM();}
        else if(catfs_vfs_mount("/data")<0){catfs_sync();catfs.mounted=0;C_ERR();con_writeln("mkfs: VFS adapter failed");C_NRM();}
        else{live_mode=0;C_OK();cprintf("Formatted hd%c as QewoxFS and mounted on /data\n",'a'+drv);C_NRM();}
    }
    else if (!kstrcmp(cmd,"fsck")) {
        int repair=argc>=2&&!kstrcmp(argv[1],"-y"),arg=repair?2:1;
        if(argc<=arg){con_writeln("fsck: usage: fsck [-y] hda1 (CatFS primary partition)");goto done;}
        const char *dev=argv[arg];
        if(dev[0]!='h'||dev[1]!='d'||dev[2]<'a'||dev[2]>='a'+DISK_MAX_DRIVES||dev[3]<'1'||dev[3]>'4'||dev[4]){con_writeln("fsck: expected hda1 through hdd4");goto done;}
        int drv=dev[2]-'a',part=dev[3]-'1';mbr_table_t table;
        if(!disk_drives[drv].present||mbr_read(drv,&table)<0||mbr_validate_drive(drv,&table)<0||!table.entries[part].type||!table.entries[part].sector_count||kstrcmp(mbr_probe_filesystem(drv,&table.entries[part]),"CatFS")){con_writeln("fsck: target is not a detected CatFS primary partition");goto done;}
        if(repair&&catfs.mounted){con_writeln("fsck: -y refused while CatFS is mounted; run umount first");goto done;}
        int issues=catfs_fsck_at(drv,table.entries[part].lba_start,repair);
        if(issues<0){C_ERR();con_writeln("fsck: check failed");C_NRM();}else cprintf("fsck: %s: %d issue(s)%s\n",dev,issues,repair?" repaired where possible":"");
    }
    else if (!kstrcmp(cmd,"sync"))   { if(catfs.mounted){catfs_sync();C_OK();con_writeln("Synced.");C_NRM();}else{C_WRN();con_writeln("No disk mounted.");C_NRM();} }
    /* Network */
    else if (!kstrcmp(cmd,"ifconfig")) {
        if(argc==1){con_writeln("lo:    127.0.0.1  LOOPBACK  UP");net_print_info();}
        else if(argc>=3){
            uint8_t ip[4]={0}; int o2=0; uint32_t v=0;
            for(char *pp=(char*)argv[2];*pp&&o2<4;pp++){
                if(*pp>='0'&&*pp<='9') v=v*10+(uint32_t)(*pp-'0');
                else if(*pp=='.'){ip[o2++]=(uint8_t)v;v=0;}
            } ip[o2]=(uint8_t)v;
            net_set_ip(ip[0],ip[1],ip[2],ip[3]); con_writeln("IP updated.");
        }
    }
    else if (!kstrcmp(cmd,"netstat")) net_print_stats();
    else if (!kstrcmp(cmd,"net")) {
        if(argc>=2 && !kstrcmp(argv[1],"test")) {
            int arp_rc=net_arp_selftest(),tcp_rc=atm_tcp_selftest();
            cprintf("net test: arp-cache=%s tcp-checksum-window=%s\n",arp_rc==0?"OK":"FAIL",tcp_rc==0?"OK":"FAIL");
        } else if(argc>=2 && !kstrcmp(argv[1],"drivers")) net_print_drivers();
        else con_writeln("net: test | drivers");
    }

    else if (!kstrcmp(cmd,"ping")) {
        if(argc<2){ C_ERR(); con_writeln("ping: missing host argument"); C_NRM(); goto done; }
        int count = 4;
        for(int i=1;i<argc;i++) if(!kstrcmp(argv[i],"-c")&&i+1<argc){count=kstrtoi(argv[i+1]);break;}

        uint8_t dst_ip[4];
        if (unm_dns_resolve(argv[1], dst_ip) < 0) {
            C_ERR(); cprintf("ping: cannot resolve %s\n", argv[1]); C_NRM();
            goto done;
        }
        icmp_ping(dst_ip, count);
    }
    else if (!kstrcmp(cmd,"arp"))    { if(argc<2) goto done; cprintf("ARP: broadcasting for %s...\n",argv[1]); }
    else if (!kstrcmp(cmd,"route"))  con_writeln("Destination  Gateway   Iface\n0.0.0.0      10.0.2.2  eth0\n127.0.0.0    *         lo");

    /* ====== UNM — UserNet Manager ====== */
    else if (!kstrcmp(cmd,"unm")) {
        if (argc < 2) {
            C_HDR(); con_writeln("UNM — UserNet Manager"); C_NRM();
            con_writeln("  unm status              — show connection state");
            con_writeln("  unm connect             — DHCP connect");
            con_writeln("  unm disconnect          — bring interface down");
            con_writeln("  unm profiles            — list saved profiles");
            con_writeln("  unm save                — save current profile");
            con_writeln("  unm static <ip> <gw> <dns> — set static IP");
            con_writeln("  unm dns <host>          — resolve hostname");
            con_writeln("  untui                   — open interactive TUI");
            goto done;
        }
        if (!kstrcmp(argv[1],"status")) {
            C_HDR(); con_writeln("=== UNM Status ==="); C_NRM();
            unm_status();
        }
        else if (!kstrcmp(argv[1],"connect")) {
            C_HDR(); con_writeln("=== UNM Connect ==="); C_NRM();
            unm_connect();
        }
        else if (!kstrcmp(argv[1],"disconnect")) {
            unm_disconnect();
        }
        else if (!kstrcmp(argv[1],"profiles")) {
            C_HDR(); con_writeln("=== UNM Profiles ==="); C_NRM();
            unm_profile_list();
        }
        else if (!kstrcmp(argv[1],"save")) {
            unm_save_profile();
        }
        else if (!kstrcmp(argv[1],"static") && argc >= 5) {
            uint8_t ip[4]={0}, nm[4]={255,255,255,0}, gw[4]={0}, dns[4]={0};
            /* Parse argv[2]=IP argv[3]=GW argv[4]=DNS */
            {
                int o=0; uint32_t v=0;
                for (const char *s=argv[2];*s&&o<4;s++){
                    if(*s>='0'&&*s<='9') v=v*10+(uint32_t)(*s-'0');
                    else if(*s=='.'){ip[o++]=(uint8_t)(v&0xFF);v=0;}
                } ip[o]=(uint8_t)(v&0xFF);
            }
            {
                int o=0; uint32_t v=0;
                for (const char *s=argv[3];*s&&o<4;s++){
                    if(*s>='0'&&*s<='9') v=v*10+(uint32_t)(*s-'0');
                    else if(*s=='.'){gw[o++]=(uint8_t)(v&0xFF);v=0;}
                } gw[o]=(uint8_t)(v&0xFF);
            }
            {
                int o=0; uint32_t v=0;
                for (const char *s=argv[4];*s&&o<4;s++){
                    if(*s>='0'&&*s<='9') v=v*10+(uint32_t)(*s-'0');
                    else if(*s=='.'){dns[o++]=(uint8_t)(v&0xFF);v=0;}
                } dns[o]=(uint8_t)(v&0xFF);
            }
            unm_set_static(ip, nm, gw, dns);
        }
        else if (!kstrcmp(argv[1],"dns") && argc >= 3) {
            uint8_t out[4]={0};
            char buf[24];
            if (unm_dns_resolve(argv[2], out) == 0) {
                unm_ip_str(out, buf, sizeof(buf));
                C_OK(); cprintf("%s -> %s\n", argv[2], buf); C_NRM();
            } else {
                C_ERR(); cprintf("unm dns: cannot resolve '%s'\n", argv[2]); C_NRM();
            }
        }
        else {
            C_ERR(); cprintf("unm: unknown subcommand '%s'\n", argv[1]); C_NRM();
        }
    }

    /* ====== UNTUI — interactive network TUI ====== */
    else if (!kstrcmp(cmd,"untui") || !kstrcmp(cmd,"netui") || !kstrcmp(cmd,"nmtui")) {
        untui_run();
    }
    /* ====== DISKMGR — interactive disk partitioning TUI ====== */
    else if (!kstrcmp(cmd,"diskmgr") || !kstrcmp(cmd,"fdisk") || !kstrcmp(cmd,"cfdisk")) {
        if (argc>=2 && !kstrcmp(argv[1],"test")) {
            cprintf("cfdisk test: mbr-validation=%s\n",mbr_selftest()==0?"OK":"FAIL");
        } else diskmgr_run();
    }
    /* Config === extended in v10 */
    else if (!kstrcmp(cmd,"sysconf")) {
        if(argc<2){con_writeln("sysconf: get|set|save|show [section] [key] [val]");goto done;}
        if(!kstrcmp(argv[1],"get")&&argc>=4){const char*v=sysconf_get(argv[2],argv[3]);v?con_writeln(v):(void)(C_WRN(),con_writeln("(unset)"),C_NRM());}
        else if(!kstrcmp(argv[1],"set")&&argc>=5){sysconf_set(argv[2],argv[3],argv[4]);C_OK();con_writeln("OK");C_NRM();}
        else if(!kstrcmp(argv[1],"save")){sysconf_save();C_OK();con_writeln("Saved.");C_NRM();}
        else if(!kstrcmp(argv[1],"show")){
            for(int si=0;si<g_syscfg.count;si++){
                C_HDR();cprintf("[%s]\n",g_syscfg.sections[si].name);C_NRM();
                for(int ki=0;ki<g_syscfg.sections[si].count;ki++){
                    C_ACC();con_write("  ");con_write(g_syscfg.sections[si].entries[ki].key);
                    C_NRM();con_write(" = ");con_writeln(g_syscfg.sections[si].entries[ki].val);
                }
            }
        }
    }
    /* v7: config create wizard */
    else if (!kstrcmp(cmd,"config")) {
        if (argc>=2 && !kstrcmp(argv[1],"create")) {
            cmd_config_create();
        } else {
            con_writeln("config: subcommands: create");
            con_writeln("  config create   === interactive configuration wizard");
            con_writeln("  Tip: press Alt+C at any time to open this wizard");
        }
    }
    /* Packages */
    else if (!kstrcmp(cmd,"pkg")) {
        if(argc<2){con_writeln("pkg: install|info|list|remove|create");goto done;}
        if(!kstrcmp(argv[1],"list")){
            int any=0;
            for(int si=0;si<g_pkgcfg.count;si++){
                const char *inst=cfg_get(&g_pkgcfg,g_pkgcfg.sections[si].name,"installed");
                if(inst&&!kstrcmp(inst,"yes")){
                    const char *ver=cfg_get(&g_pkgcfg,g_pkgcfg.sections[si].name,"version");
                    const char *desc=cfg_get(&g_pkgcfg,g_pkgcfg.sections[si].name,"description");
                    C_ACC();cprintf("  %-20s",g_pkgcfg.sections[si].name);
                    C_NRM();cprintf("%-10s  %s\n",ver?ver:"?",desc?desc:"");any=1;
                }
            }
            if(!any){C_WRN();con_writeln("(====== ========================== ==============)");C_NRM();}
            goto done;
        }
        /* pkg remove <name> */
        if(!kstrcmp(argv[1],"remove")) {
            if(argc<3){C_ERR();con_writeln("pkg remove: ========== ====== ============");C_NRM();goto done;}
            tzst_remove(argv[2]);
            goto done;
        }
        /* pkg repo show | pkg repo set http://host/base configures only the
         * intentionally limited clear-text HTTP repository bootstrap. */
        if(!kstrcmp(argv[1],"repo")) {
            if(argc==2 || (argc==3 && !kstrcmp(argv[2],"show"))) {
                const char *repo=tzst_repo_url();
                if(repo)cprintf("pkg repo: %s (HTTP only; unsigned)\n",repo);
                else con_writeln("pkg repo: not configured");
            } else if(argc==4 && !kstrcmp(argv[2],"set")) {
                if(tzst_repo_set_url(argv[3])<0){C_ERR();con_writeln("pkg repo: expected bounded http://host/base URL");C_NRM();}
                else {C_WRN();con_writeln("pkg repo: HTTP bootstrap stored; TLS and signatures are unavailable");C_NRM();}
            } else {C_ERR();con_writeln("pkg repo: usage: pkg repo [show|set http://host/base]");C_NRM();}
            goto done;
        }
        /* pkg get <name> resolves <configured-repo>/<name>.atpk, then uses the
         * same bounded download, ATPK validation, cache and install path. */
        if(!kstrcmp(argv[1],"get")) {
            if(argc!=3){C_ERR();con_writeln("pkg get: usage: pkg get <native-package-name>");C_NRM();goto done;}
            int grc=tzst_repo_fetch_package(argv[2]);
            if(grc<0){C_ERR();cprintf("pkg get: repository fetch, validation, cache, or install failed (rc=%d)\n",grc);C_NRM();}
            else {C_OK();con_writeln("pkg get: validated ATPK cached and installed");C_NRM();}
            goto done;
        }
        /* pkg fetch <http-url> downloads only a bounded HTTP ATPK package,
         * validates/caches it, then uses the transactional native installer. */
        if(!kstrcmp(argv[1],"fetch")) {
            if(argc!=3){C_ERR();con_writeln("pkg fetch: usage: pkg fetch http://host/path/package.atpk");C_NRM();goto done;}
            if(kstrncmp(argv[2],"http://",7)!=0){C_ERR();con_writeln("pkg fetch: only clear-text HTTP is implemented; HTTPS/TLS is unavailable");C_NRM();goto done;}
            int frc=tzst_fetch_install_http(argv[2]);
            if(frc<0){C_ERR();cprintf("pkg fetch: download, validation, cache, or install failed (rc=%d)\n",frc);C_NRM();}
            else {C_OK();con_writeln("pkg fetch: validated ATPK cached and installed");C_NRM();}
            goto done;
        }
        /* pkg create <name> <elf_path> creates a native ATPK archive. */
        if(!kstrcmp(argv[1],"create")) {
            if(argc<4){C_ERR();con_writeln("pkg create: usage: pkg create <name> <elf_path>");C_NRM();goto done;}
            char ep[128]; build_abs(argv[3],ep);
            int fd2=vfs_open(ep,O_RDONLY, 0);
            if(fd2<0){C_ERR();cprintf("pkg create: %s ==== ============\n",argv[3]);C_NRM();goto done;}
            static uint8_t elf_buf[32768];
            int en=vfs_read(fd2,elf_buf,sizeof(elf_buf)); vfs_close(fd2);
            if(en<=0){C_ERR();con_writeln("pkg create: ============ ========");C_NRM();goto done;}
            static uint8_t tzst_out[TZST_MAX_UNPACKED + 4096];
            int cn=tzst_wrap_elf(elf_buf,(uint32_t)en,argv[2],"1.0.0",tzst_out,sizeof(tzst_out));
            if(cn<=0){C_ERR();con_writeln("pkg create: ============ ================");C_NRM();goto done;}
            char outname[96], outpath[128]; kstrcpy(outname,argv[2]); kstrcat(outname,".atpk"); build_abs(outname,outpath);
            int ofd=vfs_open(outpath,O_WRONLY|O_CREAT|O_TRUNC, 0644);
            if(ofd<0){C_ERR();cprintf("pkg create: ==== ============== ============== %s\n",outpath);C_NRM();goto done;}
            vfs_write(ofd,tzst_out,(uint32_t)cn); vfs_close(ofd);
            C_OK(); cprintf("ATPK created: %s (%d bytes)\n",outpath,cn); C_NRM();
            goto done;
        }
        /* pkg install / info */
        if(argc<3) goto done;
        char p[128]; build_abs(argv[2],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0){C_ERR();cprintf("pkg: %s ==== ============\n",argv[2]);C_NRM();goto done;}
        static uint8_t pb[32768]; int n=vfs_read(fd2,pb,sizeof(pb)); vfs_close(fd2);
        if(n<=0) goto done;
        tzst_pkg_t pkg2;
        int pkg_parse_rc=tzst_parse(&pkg2,pb,(uint32_t)n);
        if(pkg_parse_rc<0){char pc[16];C_ERR();terminal_write("ATPK parser rc=");if(pkg_parse_rc<0){terminal_putchar('-');kuitoa((uint32_t)(-pkg_parse_rc),pc,10);}else kuitoa((uint32_t)pkg_parse_rc,pc,10);terminal_writeln(pc);C_NRM();goto done;}
        if(!kstrcmp(argv[1],"info")) tzst_info(&pkg2);
        else if(!kstrcmp(argv[1],"install")) tzst_install(&pkg2);
    }
    else if (!kstrcmp(cmd,"mp3info")) {
        if(argc<2){C_ERR();con_writeln("mp3info: usage: mp3info <path>");C_NRM();goto done;}
        char p[128];build_abs(argv[1],p);int fd2=vfs_open(p,O_RDONLY,0);
        if(fd2<0){C_ERR();cprintf("mp3info: %s not found\n",argv[1]);C_NRM();goto done;}
        static uint8_t mb[ATM_MP3_SCAN_MAX];int n=vfs_read(fd2,mb,sizeof(mb));vfs_close(fd2);atm_mp3_info_t mi;
        if(n<=0||atm_mp3_probe(mb,(uint32_t)n,&mi)<0){C_ERR();con_writeln("mp3info: no supported MPEG Audio Layer III frame sequence in bounded prefix");C_NRM();goto done;}
        cprintf("MP3: MPEG-%u Layer III, %u Hz, %u kbps%s, %s, %u inspected frame(s), ~%u ms\n",mi.mpeg_version,mi.sample_rate_hz,mi.bitrate_kbps,mi.vbr?" VBR":"",mi.channels==1?"mono":"stereo",mi.frame_count,mi.duration_ms_estimate);
        if(g_hda.pcm_output_ready)con_writeln("Audio: PCM output path ready");
        else if(g_hda.controller_present)con_writeln("Audio: HDA controller detected; codec/DMA PCM playback is not implemented yet");
        else con_writeln("Audio: no HDA controller detected; MP3 support currently provides inspection only");
    }
    else if (!kstrcmp(cmd,"readelf")) {
        if(argc<2) goto done;
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0) goto done;
        static uint8_t eb[8192]; int n=vfs_read(fd2,eb,sizeof(eb)); vfs_close(fd2);
        if(n>0) elf_info(eb,(uint32_t)n);
    }
    else if (!kstrcmp(cmd,"exec")) {
        if(argc<2) goto done;
        char p[128]; build_abs(argv[1],p);
        int fd2=vfs_open(p,O_RDONLY, 0); if(fd2<0){C_ERR();cprintf("exec: %s not found\n",argv[1]);C_NRM();goto done;}
        static uint8_t xb[16384]; int n=vfs_read(fd2,xb,sizeof(xb)); vfs_close(fd2);
        if(n<=0 || !elf_validate(xb,(uint32_t)n)) {
            C_ERR(); con_writeln("exec: unsupported or invalid ELF image"); C_NRM();
        } else {
            /* The old path jumped into an ELF entry in ring 0.  That is not
             * POSIX exec and permits arbitrary kernel corruption.  ELF launch
             * stays deliberately blocked until ELF64 + ring 3 + copy_from_user
             * are available; use readelf for inspection in the meantime. */
            C_WRN(); con_writeln("exec: ELF recognised; user-space exec is not available yet"); C_NRM();
        }
    }
    /* SDK commands */
    else if (!kstrcmp(cmd,"sdk")) {
        if(argc<2){con_writeln("sdk: cmds|hooks|drivers|modules|env|info");goto done;}
        if(!kstrcmp(argv[1],"cmds"))    sdk_cmd_list();
        else if(!kstrcmp(argv[1],"modules")) sdk_module_list();
        
        else if(!kstrcmp(argv[1],"info")){
            cprintf("atmkoala SDK v%d.%d.%d\n",
                ATMKOALA_VERSION_MAJOR,ATMKOALA_VERSION_MINOR,ATMKOALA_VERSION_PATCH);
            con_writeln("Available: sdk_cmd_register, sdk_hook_register, sdk_theme_register,");
            con_writeln("           sdk_driver_register, sdk_irq_install, sdk_env_set,");
            con_writeln("           sdk_hexdump, sdk_cpuid, sdk_serial_write, sdk_sleep_ms");
        }
        else if(!kstrcmp(argv[1],"load")){
            if(argc<3){C_ERR();con_writeln("sdk load: missing path");C_NRM();}
            else sdk_module_load(argv[2]);
        }
    }
    else if (!kstrcmp(cmd,"serial")) {
        sdk_serial_init();
        if(argc<2){C_WRN();con_writeln("serial: type message:");C_NRM();char sb3[128];readline_v6(sb3,128);sdk_serial_write(sb3);sdk_serial_write("\n");}
        else{for(int i=1;i<argc;i++){sdk_serial_write(argv[i]);sdk_serial_write(" ");}sdk_serial_write("\n");}
        C_OK(); con_writeln("Sent via COM1."); C_NRM();
    }
    else if (!kstrcmp(cmd,"modinfo")) {
        if(argc<2) goto done;
        cprintf("Module: %s (use 'sdk load <path>')\n",argv[1]);
    }
    else if (!kstrcmp(cmd,"malloc")) {
        if(argc<2) goto done;
        uint32_t sz=(uint32_t)kstrtoi(argv[1]);
        void *ptr=kmalloc((size_t)sz);
        if(!ptr){C_ERR();con_writeln("malloc: FAILED");C_NRM();goto done;}
        uint8_t *b=(uint8_t*)ptr;
        for(uint32_t i=0;i<sz;i++) b[i]=(uint8_t)(i&0xFF);
        int ok=1; for(uint32_t i=0;i<sz;i++) if(b[i]!=(uint8_t)(i&0xFF)){ok=0;break;}
        C_OK(); cprintf("OK  ptr=0x%x  size=%u  verify=%s\n",(uint32_t)(uintptr_t)ptr,sz,ok?"PASS":"FAIL"); C_NRM();
        kfree(ptr);
    }
    else if (!kstrcmp(cmd,"which")) {
        if(argc<2) goto done;
        if(sdk_cmd_find(argv[1])){con_write(argv[1]);con_writeln(" (sdk registered command)");goto done;}
        const char *spaths[]={"/syls/bin","/bin","/uiu/bin",NULL};
        for(int i=0;spaths[i];i++){
            char p[128]; kstrcpy(p,spaths[i]); kstrcat(p,"/"); kstrcat(p,argv[1]);
            vfs_stat_t st; if(vfs_stat(p,&st)==0){con_writeln(p);goto done;}
        }
        con_write(argv[1]); con_writeln(" (built-in)");
    }
    else if (!kstrcmp(cmd,"man")) {
        if(argc<2){C_ERR();con_writeln("man: missing command");C_NRM();goto done;}
        char mp[128]; kstrcpy(mp,"/uiu/etc/man/"); kstrcat(mp,argv[1]);
        vfs_stat_t st;
        if(vfs_stat(mp,&st)==0) do_cat(mp);
        else cprintf("No manual entry for '%s'  (write to /uiu/etc/man/%s)\n",argv[1],argv[1]);
    }
    else if (!kstrcmp(cmd,"history")||!kstrcmp(cmd,"hist")) {
        for(int i=0;i<hist_count;i++){C_DIM();cprintf("%3d  ",i+1);C_NRM();con_writeln(history[i]);}
    }
    else if (!kstrcmp(cmd,"clear"))  con_clear();
    else if (!kstrcmp(cmd,"reset"))  { con_clear(); terminal_print_logo(); }
    else if (!kstrcmp(cmd,"logo"))   { if(use_vbe)vbe_draw_logo();else terminal_print_logo(); }
    else if (!kstrcmp(cmd,"atm-box") || !kstrcmp(cmd,"atmbox")) {
        atmbox_dispatch(argc, argv);
    }
    else if (!kstrcmp(cmd,"gfxinfo") || !kstrcmp(cmd,"mesa")) {
        atm_gfx_capabilities_t gfx;
        if(mesa_foundation_query(&gfx)<0){C_ERR();con_writeln("gfxinfo: capability query failed");C_NRM();goto done;}
        C_HDR(); cprintf("ATMKoala graphics foundation ABI v%u\n",gfx.abi_version); C_NRM();
        cprintf("  Display : %s\n",gfx.display_backend);
        cprintf("  Renderer: %s\n",gfx.renderer_backend);
        cprintf("  Surface : %ux%u %u bpp pitch=%u bytes=%u\n",gfx.width,gfx.height,gfx.bpp,gfx.pitch,(uint32_t)gfx.framebuffer_bytes);
        cprintf("  Caps    : framebuffer=%s software=%s fixed-triangles=%s\n",(gfx.capabilities&ATM_GFX_CAP_FRAMEBUFFER)?"yes":"no",(gfx.capabilities&ATM_GFX_CAP_SOFTWARE_RENDERER)?"yes":"no",(gfx.capabilities&ATM_GFX_CAP_FIXED_TRIANGLES)?"yes":"no");
        C_DIM(); con_writeln("  Hardware GPU acceleration, per-process GPU accounting and Mesa/Gallium/OpenGL/EGL/DRM ABI: not implemented."); C_NRM();
    }
    else if (!kstrcmp(cmd,"busybox")) {
        C_WRN(); con_writeln("BusyBox Linux binary ABI is not available in this kernel."); C_NRM();
        con_writeln("Native BusyBox-oriented portable profile: VFS POSIX subset + atm-box core applets.");
        con_writeln("Deferred: fork/exec/wait, pipes, Linux ioctl/socket/mount/module applets.");
        atmbox_print_applets();
    }
    else if (!kstrcmp(cmd,"echo"))   {
        int newline=1, start=1;
        if(argc>1&&!kstrcmp(argv[1],"-n")){newline=0;start=2;}
        for(int i=start;i<argc;i++){if(i>start)con_putchar(' ');con_write(argv[i]);}
        if(newline) con_putchar('\n');
    }
    else if (!kstrcmp(cmd,"printf")) {
        if(argc>=2) con_write(argv[1]);
        if(argc>=3) con_write(argv[2]);
        con_putchar('\n');
    }
    else if (!kstrcmp(cmd,"syscall")) {
        if(argc<2 || !kstrcmp(argv[1],"abi")){
            uint64_t abi=0,pid=0,uid=0,gid=0;
            __asm__ volatile("int $0x80" : "=a"(abi) : "a"((uint64_t)ATM_SYS_ABI_INFO) : "memory");
            __asm__ volatile("int $0x80" : "=a"(pid) : "a"((uint64_t)ATM_SYS_GETPID) : "memory");
            __asm__ volatile("int $0x80" : "=a"(uid) : "a"((uint64_t)ATM_SYS_GETUID) : "memory");
            __asm__ volatile("int $0x80" : "=a"(gid) : "a"((uint64_t)ATM_SYS_GETGID) : "memory");
            C_HDR(); cprintf("syscall ABI v%u (native ATMKoala)\n",(uint32_t)abi); C_NRM();
            cprintf("  getpid=%u getuid=%u getgid=%u\n",(uint32_t)pid,(uint32_t)uid,(uint32_t)gid);
            C_DIM(); con_writeln("  DPL3 gate and TSS are ready; read/write/open/fstat now use checked user-copy when a native task is bound."); C_NRM();
        } else con_writeln("syscall: abi");
    }
    else if (!kstrcmp(cmd,"posix")) {
        if(argc<2 || !kstrcmp(argv[1],"status")){
            uint32_t f=atm_posix_features();
            C_HDR(); con_writeln("ATMKoala POSIX foundation"); C_NRM();
            cprintf("  Files/paths : %s\n",(f&ATM_POSIX_FILES)?"open/read/write/lseek/stat/mkdir/unlink/rename":"unavailable");
            cprintf("  Descriptors : %s\n",(f&ATM_POSIX_FD)?"VFS-backed O_* and SEEK_*":"unavailable");
            cprintf("  Credentials : %s\n",(f&ATM_POSIX_UIDGID)?"UID/GID and permission bits":"unavailable");
            cprintf("  Metadata    : %s\n",(f&ATM_POSIX_META)?"stat/lstat/fstat chmod/chown":"unavailable");
            cprintf("  FDs/size    : %s\n",(f&ATM_POSIX_TRUNC)?"dup/dup2 truncate/ftruncate":"unavailable");
            cprintf("  I/O         : %s\n",(f&ATM_POSIX_IOV)?"pread/pwrite readv/writev (bounded native ABI)":"unavailable");
            cprintf("  Time        : %s\n",(f&ATM_POSIX_TIME)?"clock_gettime realtime/monotonic and gettimeofday; RTC/NTP timezone control unavailable":"unavailable");
            cprintf("  Select      : %s\n",(f&ATM_POSIX_SELECT)?"zero-timeout pipe readiness only; 64-fd sets, 16 watched descriptors":"unavailable");
            cprintf("  Sync        : %s\n",(f&ATM_POSIX_SYNC)?"fsync/fdatasync (backend-supported persistence)":"unavailable");
            cprintf("  Links       : %s\n",(f&ATM_POSIX_LINKS)?"hard links, symlinks and readlink":"unavailable");
            cprintf("  Runtime     : %s\n",(f&ATM_POSIX_CWD)?"cwd/chdir, relative paths, access, umask, directories, isatty":"unavailable");
            cprintf("  Sessions    : %s\n",(f&ATM_POSIX_SESSION)?"getpgid/getsid query only; no shell job control":"unavailable");
            cprintf("  Dir streams : task-owned opaque opendir/readdir/closedir handles (native ABI v7)\n");
            cprintf("  Poll        : zero-timeout pipe readiness only; blocking timeout and socket/VFS readiness unavailable\n");
            cprintf("  Address space: kernel map clone + user page window (CR3=0x%x)\n",(uint32_t)paging_kernel_cr3());
            cprintf("  Ring 3 gate   : %s (TSS rsp0 + DPL3 int 0x80)\n",usermode_gate_ready()?"ready":"closed");
            C_DIM(); con_writeln("  Native ABI v9 adds bounded pipes, descriptor/status fcntl flags, zero-timeout pipe poll/select, PIT-backed sleep/time, static process startup, and a restricted static-ET_EXEC execve with FD_CLOEXEC; fork, dynamic linking, full argv/envp and Linux binary ABI are not implemented."); C_NRM();
        } else if(!kstrcmp(argv[1],"test")) {
            sdk_serial_write("[posix] paging\n"); int pt=paging_selftest();
            sdk_serial_write("[posix] uaccess\n"); int ua=uaccess_selftest();
            sdk_serial_write("[posix] vfs\n"); int ps=atm_posix_selftest();
            sdk_serial_write("[posix] syscall\n"); int sc=atm_syscall_selftest();
            sdk_serial_write("[posix] fd\n"); int nf=native_fd_selftest();
            sdk_serial_write("[posix] dir\n"); int nd=native_dir_selftest();
            sdk_serial_write("[posix] native\n"); int na=native_app_selftest();
            sdk_serial_write("[posix] linux-l0\n"); int la=native_app_linux_abi_selftest();
            sdk_serial_write("[posix] linux-descriptor\n"); int ld=native_app_linux_descriptor_selftest();
            if(ld==0)sdk_serial_write("[linux] descriptor-ok\n");else sdk_serial_write("[linux] descriptor-fail\n");
            sdk_serial_write("[posix] linux-session\n"); int ls=native_app_linux_session_selftest();
            if(ls==0)sdk_serial_write("[linux] session-ok\n");else sdk_serial_write("[linux] session-fail\n");
            sdk_serial_write("[posix] linux-v22\n"); int lv22=native_app_linux_v22_selftest();
            if(lv22==0)sdk_serial_write("[linux] v22-ok\n");else sdk_serial_write("[linux] v22-fail\n");
            sdk_serial_write("[posix] linux-l1\n"); int l1=native_app_linux_l1_selftest();
            sdk_serial_write("[posix] linux-l3\n"); int l3=native_app_linux_l3_selftest();
            sdk_serial_write("[posix] exec\n"); int ex=native_app_exec_selftest();
            sdk_serial_write("[posix] cpl3-wait\n"); int cw=native_app_cpl3_wait_selftest();
            sdk_serial_write("[posix] cpl3-signal\n"); int csig=native_app_cpl3_signal_selftest();
            if(csig==0)sdk_serial_write("[native] cpl3-signal-ok\n");else sdk_serial_write("[native] cpl3-signal-fail\n");
            sdk_serial_write("[posix] libc\n"); int lc=native_app_libc_selftest();
            sdk_serial_write("[posix] pipe\n"); int ipc=native_app_pipe_ipc_selftest();
            sdk_serial_write("[posix] image\n"); int im=atm_image_selftest();
            sdk_serial_write("[vbe] fastpath\n"); int vf=vbe_fastpath_selftest();
            if(vf==0)sdk_serial_write("[vbe] fastpath-ok\n");else sdk_serial_write("[vbe] fastpath-fail\n");
            sdk_serial_write("[vbe] geometry\n"); int vg=vbe_geometry_selftest();
            if(vg==0)sdk_serial_write("[vbe] geometry-ok\n");else sdk_serial_write("[vbe] geometry-fail\n");
            sdk_serial_write("[mouse] packet\n"); int msp=mouse_packet_selftest();
            if(msp==0)sdk_serial_write("[mouse] packet-ok\n");else sdk_serial_write("[mouse] packet-fail\n");
            sdk_serial_write("[udp] parser\n"); int udp=net_udp_selftest();
            if(udp==0)sdk_serial_write("[udp] parser-ok\n");else sdk_serial_write("[udp] parser-fail\n");
            sdk_serial_write("[ntp] parser\n"); int ntp=ntp_selftest();
            if(ntp==0)sdk_serial_write("[ntp] parser-ok\n");else sdk_serial_write("[ntp] parser-fail\n");
            sdk_serial_write("[http] parser\n"); int hp=atm_http_selftest();
            if(hp==0)sdk_serial_write("[http] parser-ok\n");else sdk_serial_write("[http] parser-fail\n");
            sdk_serial_write("[mp3] parser\n"); int mp=atm_mp3_selftest();
            if(mp==0)sdk_serial_write("[mp3] parser-ok\n");else sdk_serial_write("[mp3] parser-fail\n");
            sdk_serial_write("[hda] detect\n"); int hd=hda_selftest();
            if(hd==0)sdk_serial_write("[hda] detect-ok\n");else sdk_serial_write("[hda] detect-fail\n");
            sdk_serial_write("[uhd600] detect\n"); int ug=i915_selftest();
            if(ug==0)sdk_serial_write("[uhd600] detect-ok\n");else sdk_serial_write("[uhd600] detect-fail\n");
            sdk_serial_write("[hardware] status\n"); int hs=hardware_status_selftest();
            if(hs==0)sdk_serial_write("[hardware] status-ok\n");else sdk_serial_write("[hardware] status-fail\n");
            sdk_serial_write("[installer] ui\n"); int ii=installer_selftest();
            if(ii==0)sdk_serial_write("[installer] ui-ok\n");else sdk_serial_write("[installer] ui-fail\n");
            sdk_serial_write("[init] runtime\n"); int ir=atminit_selftest();
            if(ir==0)sdk_serial_write("[init] runtime-ok\n");else sdk_serial_write("[init] runtime-fail\n");
            sdk_serial_write("[exp] utf8-layout\n"); int eu=exp_text_layout_selftest();
            if(eu==0)sdk_serial_write("[exp] utf8-layout-ok\n");else sdk_serial_write("[exp] utf8-layout-fail\n");
            sdk_serial_write("[time] timezone\n"); int tzr=atm_timezone_selftest();
            if(tzr==0)sdk_serial_write("[time] timezone-ok\n");else sdk_serial_write("[time] timezone-fail\n");
            sdk_serial_write("[rtc] writer\n"); int rtcw=rtc_write_selftest();
            if(rtcw==0)sdk_serial_write("[rtc] writer-ok\n");else sdk_serial_write("[rtc] writer-fail\n");
            sdk_serial_write("[tzif] parser\n"); int tzif=tzif_selftest();
            if(tzif==0)sdk_serial_write("[tzif] parser-ok\n");else sdk_serial_write("[tzif] parser-fail\n");
            sdk_serial_write("[pkg] repo\n"); int rp=tzst_repo_selftest();
            if(rp==0)sdk_serial_write("[pkg] repo-ok\n");else sdk_serial_write("[pkg] repo-fail\n");
            sdk_serial_write("[ixpy] parser-raw-source\n"); int xp=ixpy_selftest();
            if(xp==0)sdk_serial_write("[ixpy] parser-raw-source-ok\n");else sdk_serial_write("[ixpy] parser-raw-source-fail\n");
            cprintf("posix test: paging=%s uaccess=%s vfs-posix=%s syscall-usercopy=%s process-fd=%s native-dir=%s native-cpl3=%s linux-l0=%s linux-descriptor=%s linux-session=%s linux-v22=%s linux-l1=%s linux-l3=%s exec=%s cpl3-wait=%s cpl3-signal=%s static-libc=%s pipe-ipc=%s image-bmp=%s vbe-fastpath=%s vbe-geometry=%s udp-parser=%s ntp-parser=%s http-parser=%s mp3-parser=%s hda-detect=%s uhd600-detect=%s hardware-status=%s installer-ui=%s init-runtime=%s exp-utf8-layout=%s timezone=%s tzif-parser=%s pkg-repo=%s ixpy-parser=%s\n",pt==0?"OK":"FAIL",ua==0?"OK":"FAIL",ps==0?"OK":"FAIL",sc==0?"OK":"FAIL",nf==0?"OK":"FAIL",nd==0?"OK":"FAIL",na==0?"OK":"FAIL",la==0?"OK":"FAIL",ld==0?"OK":"FAIL",ls==0?"OK":"FAIL",lv22==0?"OK":"FAIL",l1==0?"OK":"FAIL",l3==0?"OK":"FAIL",ex==0?"OK":"FAIL",cw==0?"OK":"FAIL",csig==0?"OK":"FAIL",lc==0?"OK":"FAIL",ipc==0?"OK":"FAIL",im==0?"OK":"FAIL",vf==0?"OK":"FAIL",vg==0?"OK":"FAIL",udp==0?"OK":"FAIL",ntp==0?"OK":"FAIL",hp==0?"OK":"FAIL",mp==0?"OK":"FAIL",hd==0?"OK":"FAIL",ug==0?"OK":"FAIL",hs==0?"OK":"FAIL",ii==0?"OK":"FAIL",ir==0?"OK":"FAIL",eu==0?"OK":"FAIL",tzr==0?"OK":"FAIL",tzif==0?"OK":"FAIL",rp==0?"OK":"FAIL",xp==0?"OK":"FAIL");
        } else if(!kstrcmp(argv[1],"ring3")) {
            if(!session_is_privileged()){ C_ERR(); con_writeln("posix ring3: administrator privileges required"); C_NRM(); goto done; }
            C_WRN(); con_write("Type RING3 to enter destructive CPL 3 diagnostic: "); C_NRM();
            char confirm[16]; readline_v6(confirm,sizeof(confirm));
            if(kstrcmp(confirm,"RING3")){ con_writeln("posix ring3: cancelled"); goto done; }
            con_writeln("posix ring3: entering native CPL 3 self-test; inspect QEMU CS=0x1b, RAX=2, then reboot.");
            usermode_selftest_enter();
        } else if(!kstrcmp(argv[1],"api")) {
            con_writeln("open close read write pread pwrite readv writev lseek stat lstat fstat");
            con_writeln("dup dup2 truncate ftruncate fsync fdatasync chmod chown mkdir rmdir unlink");
            con_writeln("rename link symlink readlink getuid getgid chdir getcwd access umask directories isatty");
        } else con_writeln("posix: status | test | ring3 | api");
    }
    else if (!kstrcmp(cmd,"gui")) {
        if(!use_vbe || !exp_is_active()){C_ERR();con_writeln("gui: open Exp first with 'de'");C_NRM();goto done;}
        if(argc<2){cprintf("gui: %d registered native apps; use gui open <id>\n",exp_gui_count());goto done;}
        if(!kstrcmp(argv[1],"open")&&argc>=3){if(exp_gui_open(argv[2])<0){C_ERR();con_writeln("gui: unknown app or no free window slot");C_NRM();}}
        else con_writeln("gui: open <application-id>");
    }
    else if (!kstrcmp(cmd,"notepad") || !kstrcmp(cmd,"nano")) {
        if(!use_vbe || !exp_is_active()){C_ERR();con_writeln("editor: open Exp first with 'de'");C_NRM();goto done;}
        char edit_path[128];const char *target=NULL;
        if(argc>=2){build_abs(argv[1],edit_path);target=edit_path;}
        if(exp_open_app(APP_NOTEPAD,target)<0){C_ERR();con_writeln("editor: no free window slot");C_NRM();}
        if(!kstrcmp(cmd,"nano"))con_writeln("nano: native bounded Notepad compatibility frontend, not an upstream GNU nano port.");
    }
    else if (!kstrcmp(cmd,"calc") || !kstrcmp(cmd,"bc")) {
        if(!use_vbe || !exp_is_active()){C_ERR();con_writeln("calculator: open Exp first with 'de'");C_NRM();goto done;}
        if(exp_open_app(APP_CALCULATOR,NULL)<0){C_ERR();con_writeln("calculator: no free window slot");C_NRM();}
        if(!kstrcmp(cmd,"bc"))con_writeln("bc: native integer calculator launcher; this is not an upstream GNU bc port.");
    }
    else if (!kstrcmp(cmd,"cube") || !kstrcmp(cmd,"gears") || !kstrcmp(cmd,"glxgears")) {
        if(!use_vbe||!exp_is_active()){C_ERR();con_writeln("TinyGL: open Exp first with 'de'");C_NRM();goto done;}
        int scene=!kstrcmp(cmd,"cube")?0:1;
        if(!kstrcmp(cmd,"glxgears"))con_writeln("glxgears: GLX and Mesa APIs are unavailable; opening TinyGL-Lite software gears demo.");
        if(exp_open_tinygl_scene(scene)<0){C_ERR();con_writeln("TinyGL: no free desktop window");C_NRM();}
    }
    else if (!kstrcmp(cmd,"wallpaper")) {
        if(!use_vbe||!exp_is_active()){C_ERR();con_writeln("wallpaper: open Exp first with 'de'");C_NRM();goto done;}
        if(argc<2){cprintf("wallpaper: %s\n",exp_wallpaper_current());goto done;}
        char path[128];build_abs(argv[1],path);
        if(exp_wallpaper_apply(path)<0){C_ERR();con_writeln("wallpaper: apply failed; supported formats are PNG, JPEG and uncompressed 24/32-bit BMP");C_NRM();}
        else {C_OK();cprintf("wallpaper: applied %s\n",path);C_NRM();}
    }
    else if (!kstrcmp(cmd,"de")) {
        if(!use_vbe || boot_text_mode){C_ERR();con_writeln("de: unavailable in Text mode");C_NRM();goto done;}
        if(!vbe_desktop_supported()){C_ERR();cprintf("de: needs at least %ux%u; boot framebuffer is %ux%u. Select a higher graphical mode.\n",VBE_DESKTOP_MIN_WIDTH,VBE_DESKTOP_MIN_HEIGHT,vbe.width,vbe.height);C_NRM();goto done;}
        /* Apply desktop style before launching */
        const char *dstyle = sysconf_get("desktop","style");
        if (dstyle && dstyle[0]) {
            C_DIM(); cprintf("  Desktop style: %s\n", dstyle); C_NRM();
        }
        C_OK();con_writeln("Launching Exp... (Alt+F1 to return to shell)");C_NRM();
        /* Exp owns the next visible frame. Do not wait for PIT here: a timer
           issue on real hardware must not turn `de` into a shell freeze. */
        exp_run();
        con_clear(); terminal_print_logo();
    }
    else if (!kstrcmp(cmd,"install")) {
        C_WRN(); con_writeln("install: unavailable in the normal system. Reboot and choose 'ATMKoala [Disk Installer]' in GRUB."); C_NRM();
    }
    else if (!kstrcmp(cmd,"live")) { catfs_vfs_unmount(); live_mode=1; catfs_sync(); catfs.mounted=0; C_OK(); con_writeln("Live mode active."); C_NRM(); }
    else if (!kstrcmp(cmd,"modules")) {
        if(!g_mb2){con_writeln("No GRUB modules.");goto done;}
        const uint8_t *end = (const uint8_t *)g_mb2 + g_mb2->total_size;
        const mb2_tag_t *tag = (const mb2_tag_t *)(g_mb2 + 1);
        uint32_t count = 0;
        while ((const uint8_t *)tag + sizeof(mb2_tag_t) <= end) {
            if (tag->type == MB2_TAG_END) break;
            if (tag->type == MB2_TAG_MODULE && tag->size >= sizeof(mb2_tag_module_t)) {
                const mb2_tag_module_t *m = (const mb2_tag_module_t *)tag;
                cprintf("[%u] 0x%x..0x%x  %s\n", count++,
                        (uint32_t)m->mod_start, (uint32_t)m->mod_end,
                        m->string[0] ? m->string : "(none)");
            }
            const mb2_tag_t *next = mb2_next_tag(tag, end);
            if(!next) break;
            tag = next;
        }
        if(!count) con_writeln("No GRUB modules.");
    }
    else if (!kstrcmp(cmd,"aiy")) {
        C_HDR(); con_writeln("[aiy] atmkoala Assistant v7"); C_NRM();
        if(argc<2){con_writeln("aiy: status|help|diskinfo|netinfo|sched|hwinfo");goto done;}
        if(!kstrcmp(argv[1],"status")){C_OK();con_writeln("[OK] All systems nominal");C_NRM();}
        else if(!kstrcmp(argv[1],"help")) dispatch((char*)"help");
        else if(!kstrcmp(argv[1],"diskinfo")){dispatch((char*)"lsblk");dispatch((char*)"df");}
        else if(!kstrcmp(argv[1],"netinfo")){dispatch((char*)"ifconfig");dispatch((char*)"netstat");}
        else if(!kstrcmp(argv[1],"sched")) sched_print_tasks();
        else if(!kstrcmp(argv[1],"hwinfo")) do_hwinfo();
    }
    /* ====== v10 NEW COMMANDS ====== */
    else if (!kstrcmp(cmd,"snake"))  game_snake();
    else if (!kstrcmp(cmd,"flappy")) {
        if(!exp_is_active()) con_writeln("flappy: open Exp with 'de', then run flappy in its Terminal.");
        else if(exp_open_app(APP_FLAPPY,NULL)<0) con_writeln("flappy: no free Exp window slot.");
    }
    else if (!kstrcmp(cmd,"power")) {
        if(!exp_is_active()) con_writeln("power: open Exp with 'de', then run power in its Terminal.");
        else if(exp_open_app(APP_POWER,NULL)<0) con_writeln("power: no free Exp window slot.");
    }
    else if (!kstrcmp(cmd,"events")) {
        if(!exp_is_active()) con_writeln("events: open Exp with 'de', then run events in its Terminal.");
        else if(exp_open_app(APP_EVENTLOG,NULL)<0) con_writeln("events: no free Exp window slot.");
    }
    else if (!kstrcmp(cmd,"sysinfo")) {
        if(!exp_is_active()) con_writeln("sysinfo: open Exp with 'de', then run sysinfo in its Terminal.");
        else if(exp_open_app(APP_SYSINFO,NULL)<0) con_writeln("sysinfo: no free Exp window slot.");
    }
    else if (!kstrcmp(cmd,"tetris")) game_tetris();
    else if (!kstrcmp(cmd,"pong"))   game_pong();
    else if (!kstrcmp(cmd,"osbuilder")||!kstrcmp(cmd,"mkos")) {
        if (argc>=2&&!kstrcmp(argv[1],"show")) osbuilder_show();
        else if (argc>=2&&!kstrcmp(argv[1],"apply")) osbuilder_apply();
        else osbuilder_run();
    }
    else if (!kstrcmp(cmd,"btrfs")) {
        if(argc<2){con_writeln("btrfs: probe <drive> <part> | status | rw <on|off> | label <name> | clear");goto done;}
        if(!kstrcmp(argv[1],"probe")&&argc>=4){
            if(btrfs_probe_partition(kstrtoi(argv[2]),kstrtoi(argv[3]))<0){C_ERR();con_writeln("btrfs: no structurally valid superblock mirror");C_NRM();}
            else {C_OK();console_btrfs_status();C_NRM();}
        } else if(!kstrcmp(argv[1],"status")){console_btrfs_status();}
        else if(!kstrcmp(argv[1],"rw")&&argc>=3){
            int want=!kstrcmp(argv[2],"on");
            if(btrfs_set_write_enabled(want)<0){C_ERR();con_writeln("btrfs: write capability requires a selected CRC32C-valid Btrfs volume");C_NRM();}
            else {C_OK();cprintf("btrfs: label write %s; %s\n",want?"enabled":"disabled",btrfs_write_policy());C_NRM();}
        } else if(!kstrcmp(argv[1],"label")&&argc>=3){
            if(btrfs_label_set(argv[2])<0){C_ERR();cprintf("btrfs: label transaction rejected: %s (ata lba=%u st=%x err=%x)\n",btrfs_last_error(),disk_last_error_lba,disk_last_status,disk_last_error_reg);C_NRM();}
            else {C_OK();cprintf("btrfs: label committed as '%s'\n",btrfs.label);C_NRM();}
        } else if(!kstrcmp(argv[1],"clear")){btrfs_clear();con_writeln("btrfs: selection cleared");}
        else con_writeln("btrfs: probe <drive> <part> | status | rw <on|off> | label <name> | clear");
    }
    else if (!kstrcmp(cmd,"ext2")) {
        if(argc<2){con_writeln("ext2: mount <drive> <part> | info | status | rw <on|off> | write <path> <offset> <text> | ls [-l] [path] | stat <path> | readlink <path> | cat <path> | catrange <path> <byte> | umount");goto done;}
        if(!kstrcmp(argv[1],"mount")&&argc>=4){
            int d=kstrtoi(argv[2]),p=kstrtoi(argv[3]);
            if(ext2_mount_partition(d,p)<0){C_ERR();cprintf("ext2: mount failed: %s (MBR %02x %02x)\n",ext2_last_error(),ext2.mbr_sig0,ext2.mbr_sig1);C_NRM();}
            else {C_OK();cprintf("ext2: mounted read-only; %u-byte blocks, volume '%s'\n",ext2.block_size,ext2.volume_name);C_NRM();}
        } else if(!kstrcmp(argv[1],"umount")){ext2_unmount();con_writeln("ext2: unmounted");}
        else if(!kstrcmp(argv[1],"status")){if(!ext2.mounted)con_writeln("ext2: not mounted");else cprintf("ext2: hd%d part%d, block %u, groups %u, volume '%s', %s\n",ext2.drive,ext2.part,ext2.block_size,ext2.groups,ext2.volume_name,ext2.write_enabled?"guarded direct-block write":"read-only");}
        else if(!kstrcmp(argv[1],"info")){if(!ext2.mounted)con_writeln("ext2: not mounted");else cprintf("ext2 info: blocks %u (%u free), inodes %u (%u free), incompat 0x%x, state 0x%x, %s\n",ext2.blocks_count,ext2.free_blocks,ext2.inodes_count,ext2.free_inodes,ext2.feature_incompat,ext2.fs_state,ext2.write_enabled?"guarded write":"read-only");}
        else if(!kstrcmp(argv[1],"rw")&&argc>=3){int en=!kstrcmp(argv[2],"on");if((!en&&kstrcmp(argv[2],"off"))||ext2_set_write_enabled(en)<0){C_ERR();cprintf("ext2: write guard: %s\n",ext2_last_error());C_NRM();}else {C_OK();cprintf("ext2: %s\n",en?"guarded direct-block write enabled":"read-only guard restored");C_NRM();}}
        else if(!kstrcmp(argv[1],"write")&&argc>=5){size_t n=0;if(ext2_write_range(argv[2],(uint32_t)kstrtoi(argv[3]),(const uint8_t*)argv[4],kstrlen(argv[4]),&n)<0){C_ERR();cprintf("ext2: write denied: %s\n",ext2_last_error());C_NRM();}else {C_OK();cprintf("ext2: wrote %u bytes in-place\n",(uint32_t)n);C_NRM();}}
        else if(!kstrcmp(argv[1],"ls")){
            char list[4096];int long_form=argc>=3&&!kstrcmp(argv[2],"-l");const char *path=long_form?(argc>=4?argv[3]:"/"):(argc>=3?argv[2]:"/");
            int n=long_form?ext2_ls_long(path,list,sizeof(list)):ext2_readdir(path,list,sizeof(list));
            if(n<0){C_ERR();con_writeln("ext2: directory read failed");C_NRM();}else con_write(list);
        } else if(!kstrcmp(argv[1],"stat")&&argc>=3){
            ext2_inode_t st;char kind;if(ext2_stat(argv[2],&st,&kind)<0){C_ERR();con_writeln("ext2: stat failed");C_NRM();}
            else cprintf("ext2: inode %u, type %c, mode 0x%x, size %u, links %u, sectors %u\n",st.inode,kind,st.mode,st.size,st.links_count,st.blocks);
        } else if(!kstrcmp(argv[1],"readlink")&&argc>=3){
            char target[1024];if(ext2_readlink(argv[2],target,sizeof(target))<0){C_ERR();con_writeln("ext2: not a readable symlink");C_NRM();}else cprintf("%s\n",target);
        } else if(!kstrcmp(argv[1],"cat")&&argc>=3){
            static uint8_t data[16384];size_t n=0;
            if(ext2_readfile(argv[2],data,sizeof(data)-1,&n)<0){C_ERR();con_writeln("ext2: file read failed");C_NRM();}
            else {data[n]=0;con_write((char*)data);}
        } else if(!kstrcmp(argv[1],"catrange")&&argc>=4){
            static uint8_t data[129];size_t n=0;uint32_t off=(uint32_t)kstrtoi(argv[3]);
            if(ext2_read_range(argv[2],off,data,sizeof(data)-1,&n)<0){C_ERR();con_writeln("ext2: range read failed");C_NRM();}
            else {data[n]=0;con_write((char*)data);con_writeln("");}
        } else con_writeln("ext2: mount <drive> <part> | info | status | rw <on|off> | write <path> <offset> <text> | ls [-l] [path] | stat <path> | readlink <path> | cat <path> | catrange <path> <byte> | umount");
    }
    else if (!kstrcmp(cmd,"fat32")) {
        if (!fat32.mounted) {
            if (disk_count==0){C_ERR();con_writeln("fat32: ====== ==========");C_NRM();goto done;}
            if (fat32_mount(0)<0){C_ERR();con_writeln("fat32: ==== FAT32 ====== ============");C_NRM();goto done;}
        }
        if (argc<2||!kstrcmp(argv[1],"ls")) {
            static fat32_dirent_t ents[64]; int n2=fat32_readdir(fat32.bpb.root_cluster,ents,64);
            C_HDR();cprintf("FAT32 / (%d entries):\n",n2);C_NRM();
            for(int i=0;i<n2;i++){
                char nm[13]; int ni=0;
                for(int j=0;j<8&&ents[i].name[j]!=' ';j++) nm[ni++]=ents[i].name[j];
                if(ents[i].ext[0]!=' '){nm[ni++]='.';for(int j=0;j<3&&ents[i].ext[j]!=' ';j++)nm[ni++]=ents[i].ext[j];}
                nm[ni]=0;
                if(ents[i].attr&0x10){C_ACC();}else{C_NRM();}
                cprintf("  %-14s  %u bytes\n",nm,ents[i].size);
            }
            C_NRM();
        } else if (!kstrcmp(argv[1],"cat")&&argc>=3) {
            static fat32_dirent_t fe;
            if(fat32_find(argv[2],&fe)<0){C_ERR();cprintf("fat32: %s ==== ============\n",argv[2]);C_NRM();goto done;}
            uint32_t cl=((uint32_t)fe.cluster_hi<<16)|fe.cluster_lo;
            static uint8_t fb[4096]; int fn=fat32_readfile(cl,fe.size,fb,sizeof(fb)-1);
            if(fn>0){fb[fn]=0;con_write((char*)fb);}
        }
    }
    else if (!kstrcmp(cmd,"pong"))    game_pong();
    else if (!kstrcmp(cmd,"breakout")) game_breakout();
    else if (!kstrcmp(cmd,"desktop")) cmd_desktop(argc, argv);
    else if (!kstrcmp(cmd,"kernel")) cmd_kernel_cfg(argc, argv);

    else if (!kstrcmp(cmd,"store")||!kstrcmp(cmd,"shop")) {
        store_run();
    }
    else if (!kstrcmp(cmd,"games")||!kstrcmp(cmd,"gamelauncher")||!kstrcmp(cmd,"gl")) {
        gamelauncher_run();
    }
    else if (!kstrcmp(cmd,"cpuinfo")) {
        sdk_cpuid_t cpu; sdk_cpuid(&cpu);
        C_HDR(); con_writeln("=== CPU Detection ==="); C_NRM();
        cprintf("  Vendor:  %s\n", cpu.vendor);
        cprintf("  Brand:   %s\n", cpu.brand[0]?cpu.brand:"(unavailable)");
        cprintf("  Family:  %u  Model: %u  Stepping: %u\n", cpu.family, cpu.model, cpu.stepping);
        cprintf("  FPU: %s  MMX: %s  SSE: %s  SSE2: %s  APIC: %s\n",
            cpu.has_fpu?"yes":"no", cpu.has_mmx?"yes":"no",
            cpu.has_sse?"yes":"no", cpu.has_sse2?"yes":"no",
            cpu.has_apic?"yes":"no");
        cprintf("  RAM: %u MB\n", g_mem_upper/1024);
    }
    else if (!kstrcmp(cmd,"help")) {
        C_HDR(); con_writeln("atmkoala v0.5 === Command Reference"); C_NRM();
        con_writeln("  Run 'info' for system details, 'man <cmd>' for command help\n");
        const char *grps[][2] = {
            {"Files",   "ls ll la cat view less installer-log head tail file hd hexdump wc grep sort uniq cut tr tee dd"},
            {"Edit",    "write append touch rm cp mv mkdir rmdir tree find stat chmod chown ln"},
            {"Apps",    "de  gui open <id>  flappy|power|events|sysinfo (in Exp)  tetris  nano [path]  notepad  calc|bc  files  editor  monitor  settings  cube  gears  glxgears  wallpaper [path]"},
            {"Disk",    "lsblk swap df du mount [hda1] umount mkfs fsck [-y] hda1 sync live"},
            {"System",  "uname info hwinfo lscpu cpucompat gpu uptime mem free ps kill mouse dmesg date timezone which man modules"},
            {"Network", "ifconfig netstat net test|drivers ping arp route unm connect|disconnect|status|profiles|save untui"},
            {"Users",   "users adduser deluser usermod passwd login logout sudo su whoami id"},
            {"Services","openrc rc-status rc 1|3|5 rc-service rc-update"},
            {"Packages","pkg install|info|list|remove|create readelf exec"},
            {"POSIX",   "posix status  (compatibility API, not Linux ABI)"},
            {"Config",  "sysconf hostname env set unset export printenv"},
            {"History", "history  Tab=complete  PgUp/PgDn=scroll"},
            {NULL,NULL}
        };
        for(int i=0;grps[i][0];i++){
            C_HDR(); cprintf("  %-10s",grps[i][0]); C_NRM(); con_writeln(grps[i][1]);
        }
        if(sdk_cmd_find(NULL)!=NULL) sdk_cmd_list();
        con_writeln("\n  Scroll: PgUp/PgDn  |  Tab: complete  |  GUI: de\n");
    }
    else if (!kstrcmp(cmd,"reboot")) {
        con_writeln("Rebooting..."); atminit_shutdown(); catfs_sync();
        outb(0x64,(uint8_t)0xFE); while(1) cpu_hlt();
    }
    else if (!kstrcmp(cmd,"halt")||!kstrcmp(cmd,"poweroff")) {
        con_writeln("Halting..."); atminit_shutdown(); catfs_sync(); cpu_cli(); cpu_hlt();
        while(1) cpu_hlt();
    }

    else if (!kstrcmp(cmd,"kmod")) {
        if (argc<2) { kmod_list(); goto done; }
        if (!kstrcmp(argv[1],"load")) {
            if(argc<3){C_ERR();con_writeln("kmod load: path?");C_NRM();}
            else { char kp[128]; build_abs(argv[2],kp); kmod_load(kp); }
        } else if (!kstrcmp(argv[1],"unload")&&argc>=3) {
            kmod_unload(argv[2]);
        } else if (!kstrcmp(argv[1],"info")&&argc>=3) {
            kmod_info(argv[2]);
        } else { kmod_list(); }
    }
    else if (!kstrcmp(cmd,"abbr")) {
        if (argc<2||!kstrcmp(argv[1],"--list")||!kstrcmp(argv[1],"-l")) {
            fish_abbr_list();
        } else if (!kstrcmp(argv[1],"--add")&&argc>=4) {
            fish_abbr_add(argv[2],argv[3]);
            C_OK();cprintf("abbr: '%s' -> '%s'\n",argv[2],argv[3]);C_NRM();
        } else { con_writeln("abbr: --list | --add <abbr> <exp>"); }
    }
    else if (!kstrcmp(cmd,"edn")) {
        C_HDR();con_writeln("EDN warnings — non-fatal diagnostic notices");C_NRM();
        C_DIM();con_writeln("  Use test кот first to create controlled test notices.");C_NRM();
    }
    /* Hidden diagnostic gate.  It is intentionally omitted from help and the
     * legacy direct `panic` command is no longer an unauthenticated shortcut. */
    else if (!kstrcmp(cmd,"test")) {
        if(!session_is_privileged()){ C_ERR(); con_writeln("test: administrator privileges required"); C_NRM(); goto done; }
        if(!test_mode){
            if(argc==2 && !kstrcmp(argv[1],"кот")){ test_mode=1; C_OK(); con_writeln("test mode enabled; use: test list"); C_NRM(); }
            else { C_ERR(); con_writeln("test: access denied"); C_NRM(); }
            goto done;
        }
        if(argc<2 || !kstrcmp(argv[1],"list")){
            C_HDR(); con_writeln("test mode: oops | panic | exception | status | leave"); C_NRM(); goto done;
        }
        if(!kstrcmp(argv[1],"status")){ cprintf("test mode: enabled, display=%s, runlevel=%d\n",use_vbe?"VBE":"VGA",(int)atminit_runlevel()); goto done; }
        if(!kstrcmp(argv[1],"leave")){ test_mode=0; C_OK(); con_writeln("test mode disabled"); C_NRM(); goto done; }
        if(!kstrcmp(argv[1],"oops")){
            EDN("Synthetic non-fatal oops requested by test mode");
            kernel_oops("Synthetic non-fatal oops requested by test mode",__FILE__,__LINE__,__func__,NULL);
            C_WRN(); con_writeln("test: non-fatal oops recorded"); C_NRM(); goto done;
        }
        if(!kstrcmp(argv[1],"panic") || !kstrcmp(argv[1],"exception")){
            C_ERR(); con_write("Type PANIC to confirm destructive test: "); C_NRM();
            char confirm[16]; readline_v6(confirm,sizeof(confirm));
            if(kstrcmp(confirm,"PANIC")){ con_writeln("test: cancelled"); goto done; }
            PANIC(!kstrcmp(argv[1],"exception")?"Synthetic exception test requested by test mode":"Synthetic kernel panic requested by test mode");
        }
        C_ERR(); con_writeln("test: unknown scenario"); C_NRM();
    }
    else if (!kstrcmp(cmd,"panic")) {
        C_ERR(); con_writeln("panic: use the protected test mode"); C_NRM();
    }
    else {
        C_ERR(); cprintf("%s: command not found (type 'help')\n", cmd); C_NRM();
    }

done:
    if (redirect_fd >= 0) { vfs_close(redirect_fd); redirect_fd = -1; }
}

/* =================================================================================================================================================================================
 *  Kernel entry point
 * ================================================================================================================================================================================= */
/* ====== ============-====== == ============ (uptime ====== HH:MM:SS) ======================================= */
/* The graphical boot splash is intentionally small, self-contained and
 * bounded. It draws only after TTF/VBE setup and before Exp enables its
 * backbuffer, so it cannot be confused with idle or installer completion UI. */
static void boot_graphical_splash(void){
    if(!use_vbe||!vbe.active)return;
    int w=(int)vbe.width,h=(int)vbe.height,bw=w>440?360:w-80,bx=(w-bw)/2,by=h-54;
    const color32_t black=RGB(0x00,0x00,0x00),charcoal=RGB(0x18,0x18,0x18),line=RGB(0x86,0x86,0x86),title=RGB(0xF0,0xF0,0xF0),sub=RGB(0xB0,0xB0,0xB0),block=RGB(0xE0,0xE0,0xE0);
    vbe_clear(black);
    /* A restrained classic loading composition: title, divider and a single
     * travelling indicator. It is native ATMKoala artwork, not a copied logo. */
    vbe_fill_rect(w/2-190,h/2-74,380,2,charcoal);
    ttf_render_string_percent(w/2-145,h/2-46,"atmkoala",title,black,220);
    ttf_render_string_percent(w/2-82,h/2+12,"initialising desktop",sub,black,100);
    vbe_fill_rect(bx,by,bw,10,charcoal);vbe_draw_rect(bx-1,by-1,bw+2,12,line);
    sdk_serial_write("[boot] splash-shown\n");
    for(int step=0;step<=120;step++){int x=bx+(bw-50)*step/120;vbe_fill_rect(bx,by,bw,10,charcoal);vbe_fill_rect(x,by+2,50,6,block);vbe_present();pit_sleep(1);}
}

static void update_statusbar(void) {
    if (use_vbe) return;
    uint32_t secs = sched_uptime_ticks() / 100;
    uint32_t h = secs / 3600;
    uint32_t m = (secs % 3600) / 60;
    uint32_t s2 = secs % 60;
    char clk[16]; char tmp[8]; clk[0]=0;
    if(h<10)kstrcat(clk,"0"); kuitoa(h,tmp,10); kstrcat(clk,tmp); kstrcat(clk,":");
    if(m<10)kstrcat(clk,"0"); kuitoa(m,tmp,10); kstrcat(clk,tmp); kstrcat(clk,":");
    if(s2<10)kstrcat(clk,"0"); kuitoa(s2,tmp,10); kstrcat(clk,tmp);
    char right_buf[64];
    kstrcpy(right_buf, keyboard_ru() ? "[RU] " : "[EN] ");
    kstrcat(right_buf, clk);
    kstrcat(right_buf, "  help | fish | kmod");
    terminal_draw_statusbar(
        "atmkoala v0.5",
        right_buf);
}


void kernel_main(uint64_t mb_magic, uint64_t mbinfo_phys) {
    mb2_parse(mb_magic, mbinfo_phys);

    gdt_install();
    idt_install();
    pit_install(100);
    /* ======== ======== ========== BSS === ======================== ====== ============ ========== linker symbols */
    heap_init(HEAP_ADDR, HEAP_SIZE);
    paging_init();
    usermode_init();
    terminal_init();
    terminal_set_scheme(SCHEME_CARAMEL);

    /* ====== VBE: GRUB ====== ================== framebuffer ========== gfxpayload=keep ==================
     * ==== ================== multiboot info bit12 (framebuffer present).
     * ================== ========== bpp>=24 == ========== =======1 (RGB linear).
     * ================== serial-======== ====== ==============.
     * ========================================================================================================================================================================================================= */
    use_vbe = 0;
    /* The Multiboot2 header requests a framebuffer, so GRUB can still
     * provide a linear framebuffer for the Text menu entry.  In that case
     * legacy VGA memory at 0xB8000 is not visible.  Initialise the VBE
     * console as the renderer, while boot_text_mode below still suppresses
     * Exp and the mouse. */
    if (g_mb2_fb) {
        sdk_serial_init();
        /* ======== framebuffer info == serial ====== ============== */
        char dbg[64];
        sdk_serial_write("[vbe] type=");
        kuitoa(g_mb2_fb->fb_type, dbg, 10); sdk_serial_write(dbg);
        sdk_serial_write(" bpp=");
        kuitoa(g_mb2_fb->bpp, dbg, 10); sdk_serial_write(dbg);
        sdk_serial_write(" w=");
        kuitoa(g_mb2_fb->width, dbg, 10); sdk_serial_write(dbg);
        sdk_serial_write(" h=");
        kuitoa(g_mb2_fb->height, dbg, 10); sdk_serial_write(dbg);
        sdk_serial_write(" addr=0x");
        kuitoa((uint32_t)g_mb2_fb->addr, dbg, 16); sdk_serial_write(dbg);
        sdk_serial_write("\n");

        if (g_mb2_fb->fb_type == 1
            && (g_mb2_fb->bpp == 32 || g_mb2_fb->bpp == 24)
            && g_mb2_fb->addr > 0x1000u
            && g_mb2_fb->width  >= 320
            && g_mb2_fb->height >= 200
            && g_mb2_fb->pitch  > 0)
        {
            mb_fb_info_t fb;
            fb.addr_lo    = (uint32_t)g_mb2_fb->addr;
            fb.addr_hi    = (uint32_t)(g_mb2_fb->addr >> 32);
            fb.pitch      = g_mb2_fb->pitch;
            fb.width      = g_mb2_fb->width;
            fb.height     = g_mb2_fb->height;
            fb.bpp        = g_mb2_fb->bpp;
            fb.type       = g_mb2_fb->fb_type;
            fb.red_pos    = g_mb2_fb->red_pos;
            fb.red_mask   = g_mb2_fb->red_mask;
            fb.green_pos  = g_mb2_fb->green_pos;
            fb.green_mask = g_mb2_fb->green_mask;
            fb.blue_pos   = g_mb2_fb->blue_pos;
            fb.blue_mask  = g_mb2_fb->blue_mask;
            if (vbe_init(&fb) == 0) {
                use_vbe = 1;
                vbe_console_init();
                vbe_clear(RGB(0x0A,0x0B,0x0D));
                sdk_serial_write("[vbe] OK\n");
            } else {
                sdk_serial_write("[vbe] vbe_init failed\n");
            }
        } else {
            sdk_serial_write("[vbe] conditions not met, using VGA\n");
        }
    }
    if (!use_vbe) terminal_print_logo();

    /* Text mode uses only the keyboard.  Initialising the auxiliary PS/2
     * device there can reconfigure a marginal 8042 and steal console input.
     * Bring up keyboard polling first, then enable IRQs; mouse is GUI-only. */
    keyboard_init();
    cpu_sti();
    if (use_vbe && !boot_text_mode && vbe_desktop_supported()) mouse_init((int)vbe.width,(int)vbe.height);
    sdk_serial_init();
    serial_console_ready=1;

    vfs_init();
    vfs_mkdir("/syls", 0755); vfs_mkdir("/syls/bin", 0755); vfs_mkdir("/syls/lib", 0755);
    vfs_mkdir("/uiu", 0755);  vfs_mkdir("/uiu/etc", 0755); vfs_mkdir("/uiu/etc/man", 0755);
    vfs_mkdir("/data", 0755); vfs_mkdir("/tmp", 0755);
    image_fixtures_seed();
    init_proc_files();

    /* Safe discovery only: PCI configuration-space reads populate storage and
     * USB controller diagnostics without enabling or driving those devices. */
    pci_init(&g_pci);
    radio_detect(&g_radio,&g_pci);
    i915_detect(&g_i915,&g_pci);
    /* Preserve firmware's selected framebuffer on a detected Gemini Lake
     * UHD 600. This adopts no PCI/MMIO ownership and does not modeset. */
    if(g_i915.pci_present&&use_vbe)
        (void)i915_fb_init(&g_i915,(uint64_t)(uintptr_t)vbe.fb,vbe.width,vbe.height,vbe.pitch,vbe.bpp);
    disk_init();
    /* Preserve legacy whole-disk CatFS while also accepting the aligned primary
     * CatFS partition created by the standalone installer. */
    int mounted_catfs=0;
    for(int drive=0;drive<DISK_MAX_DRIVES&&!mounted_catfs;drive++){
        if(!disk_drives[drive].present)continue;
        if(catfs_mount(drive)==0)mounted_catfs=1;
        else {
            mbr_table_t table;
            if(mbr_read(drive,&table)==0&&mbr_validate_drive(drive,&table)==0){
                for(int part=0;part<PART_MAX_ENTRIES;part++){
                    mbr_entry_t *entry=&table.entries[part];
                    if(!entry->type||!entry->sector_count)continue;
                    if(!kstrcmp(mbr_probe_filesystem(drive,entry),"CatFS")&&catfs_mount_at(drive,entry->lba_start)==0){mounted_catfs=1;break;}
                }
            }
        }
    }
    if(mounted_catfs){
        if (catfs_vfs_mount("/data") == 0) live_mode = 0;
        else { catfs_sync(); catfs.mounted = 0; }
    }
    config_init();
    /* Apply saved settings */
    const char *hn = sysconf_get("system","hostname");
    if (hn) kstrcpy(hostname, hn);
    /* users.conf supersedes sudo_pin.  The legacy value is read solely to
     * initialise root during one-time migration on older installations. */
    const char *legacy_pin = sysconf_get("system","sudo_pin");
    if (user_init(legacy_pin) < 0) PANIC("users: cannot initialise root account");
    if(user_current()) cprintf("Hello, %s! Use 'login' to choose an account.\n",user_current()->name);
    atminit_init();
    if (atminit_boot() < 0) PANIC("atm-init: default runlevel failed");
    /* A VBE desktop promotes the standard multi-user boot to graphical.
     * Explicit single-user configurations remain untouched. */
    if (use_vbe && vbe_desktop_supported() && atminit_runlevel()==ATM_RUNLEVEL_MULTI)
        (void)atminit_set_runlevel(ATM_RUNLEVEL_GRAPHICAL);
    /* Apply kernel name if set */
    const char *kname = sysconf_get("kernel","kernel_name");
    if (kname && kname[0]) {
        sdk_personality_t pers;
        const sdk_personality_t *cur = sdk_personality_get();
        kstrcpy(pers.os_name,    kname);
        kstrcpy(pers.os_version, "0.5.0");
        kstrcpy(pers.os_codename,"");
        kstrcpy(pers.os_author,  cur->os_author);
        kstrcpy(pers.os_motd,    cur->os_motd);
        sdk_personality_set(&pers);
    }

    panic_init();
    kmod_init();
    fish_init();

    sdk_env_set("PATH",    "/syls/bin:/bin");
    sdk_env_set("HOME",    "/root");
    sdk_env_set("SHELL",   "/syls/bin/atsh");
    sdk_env_set("TERM",    "atmterm-256color");
    sdk_env_set("OS",      "atmkoala");
    sdk_env_set("VERSION", "0.5.0");
    sdk_env_set("USER",    "root");

    sdk_hook_fire(HOOK_BOOT_LATE);

    /* Retry VBE after all early subsystems are initialised.  This keeps the
     * framebuffer path robust even if an early optional subsystem declined it. */
    if (!use_vbe && g_mb2_fb) {
        mb_fb_info_t retry_fb;
        retry_fb.addr_lo    = (uint32_t)g_mb2_fb->addr;
        retry_fb.addr_hi    = (uint32_t)(g_mb2_fb->addr >> 32);
        retry_fb.pitch      = g_mb2_fb->pitch;
        retry_fb.width      = g_mb2_fb->width;
        retry_fb.height     = g_mb2_fb->height;
        retry_fb.bpp        = g_mb2_fb->bpp;
        retry_fb.type       = g_mb2_fb->fb_type;
        retry_fb.red_pos    = g_mb2_fb->red_pos;
        retry_fb.red_mask   = g_mb2_fb->red_mask;
        retry_fb.green_pos  = g_mb2_fb->green_pos;
        retry_fb.green_mask = g_mb2_fb->green_mask;
        retry_fb.blue_pos   = g_mb2_fb->blue_pos;
        retry_fb.blue_mask  = g_mb2_fb->blue_mask;
        if (vbe_init(&retry_fb) == 0) {
            use_vbe = 1;
            vbe_console_init();
            vbe_clear(RGB(0x0A,0x0B,0x0D));
        }
    }

    /* Font and native window server must be ready before the first Exp frame. */
    if (use_vbe) {
        ttf_init();
        const char *psf_path=sysconf_get("desktop","psf_font");
        const char *font_scale=sysconf_get("desktop","font_scale");
        if(font_scale) ttf_set_scale(kstrtoi(font_scale));
        if(psf_path&&psf_path[0]) (void)ttf_load_psf(psf_path);
        awm_init();
    }

    /* The formatter is reachable only through the dedicated GRUB installer mode. */
    if (installer_mode) {
        if(use_vbe && !boot_text_mode) installer_run();
        else con_writeln("Installer mode requires the VBE graphical GRUB entry.");
        con_clear();
        terminal_print_logo();
    } else if (use_vbe && !boot_text_mode) {
        if(!vbe_desktop_supported()){
            con_writeln("Graphical framebuffer is below Exp minimum 640x480; using VBE console.");
            cprintf("Boot framebuffer: %ux%u x%u. Select a higher graphical mode in the boot menu.\n",vbe.width,vbe.height,vbe.bpp);
        }else{
            boot_graphical_splash();
            exp_run();
            con_clear();
            terminal_print_logo();
        }
    } else if (boot_text_mode) {
        /* On GRUB implementations that retain a linear framebuffer for
         * gfxmode=text, this is rendered by vbe_console, not by 0xB8000. */
        con_writeln("  atmkoala v0.5  x86-64  [Text console]");
        con_writeln("  Type 'help' for commands.");
        con_writeln("");
    }

    if (!use_vbe)
        update_statusbar();

    char line[CMD_MAX];
    while (1) {
        print_prompt();
        readline_v6(line, CMD_MAX);
        if (!line[0]) continue;
        dispatch(line);
        proc_write("uptime"); proc_write("meminfo");
        if (!use_vbe)
            update_statusbar();
    }
}
