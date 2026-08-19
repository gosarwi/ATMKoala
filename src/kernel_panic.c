/* kernel_panic.c — atmkoala v0.5
 *
 * Full-screen kernel panic display:
 *   VBE mode: graphical panic screen, dark background for visibility
 *   VGA mode: red-on-black full-screen text panic
 */
#include "kernel_panic.h"
#include "vga.h"
#include "vbe.h"
#include "util.h"
#include "pit.h"
#include "keyboard.h"
#include "kmalloc.h"
#include "exp.h"
#include <stdint.h>
#include <stddef.h>

extern vbe_state_t vbe;

/* ── EDN log (non-fatal warnings) ────────────────────────── */
#define EDN_MAX 32
static struct {
    char msg[64];
    char file[32];
    int  line;
} edn_log[EDN_MAX];
static int edn_count = 0;

/* Retain the last CPU exception for serial/QEMU diagnostics. */
volatile uint32_t panic_last_int = 0xFFFFFFFFu;
volatile uint32_t panic_last_err = 0;
volatile uint64_t panic_last_rip = 0;
volatile uint64_t panic_last_cr2 = 0;

void edn_warn(const char *msg, const char *file, int line) {
    if (edn_count < EDN_MAX) {
        kstrncpy(edn_log[edn_count].msg,  msg,  63);
        kstrncpy(edn_log[edn_count].file, file, 31);
        edn_log[edn_count].line = line;
        edn_count++;
    }
    /* Also print to terminal */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_write("[EDN] "); terminal_write(msg);
    terminal_write(" ("); terminal_write(file);
    terminal_putchar(':');
    char lb[8]; kitoa(line, lb, 10); terminal_writeln(lb);
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ── VGA panic screen ─────────────────────────────────────── */
static void panic_vga(const char *msg, const char *file, int line,
                      const char *func, registers_t *regs) {
    /* Fill screen red */
    for (int i = 0; i < 80*25; i++) {
        ((volatile uint16_t *)0xB8000)[i] =
            (uint16_t)(' ') | (uint16_t)(0x4F << 8); /* white on red */
    }

    /* Helper: write at position */
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    #define PVGA(row, col, s, attr) do { \
        const char *_s = (s); int _c = (col); \
        while (*_s && _c < 80) \
            vga[(row)*80+(_c++)] = (uint16_t)(unsigned char)*_s++ | ((uint16_t)(attr)<<8); \
    } while(0)

    uint8_t W_RED  = 0x4F; /* white on red */
    uint8_t Y_RED  = 0x4E; /* yellow on red */
    uint8_t B_RED  = 0x4C; /* bright red on red (dim) */
    (void)B_RED;

    PVGA(1, 2, "atmkoala v0.5  --  KERNEL PANIC", Y_RED);
    PVGA(2, 2, "================================================================", W_RED);
    PVGA(4, 2, "Reason:", W_RED);
    PVGA(4, 10, msg, Y_RED);

    char locbuf[128];
    ksnprintf(locbuf, sizeof(locbuf), "%s:%d  in %s()", file, line, func ? func : "?");
    PVGA(5, 2, "Location:", W_RED);
    PVGA(5, 12, locbuf, W_RED);

    PVGA(7, 2, "================================================================", W_RED);

    if (regs) {
        char rb[64];
        PVGA(8, 2, "Register dump:", Y_RED);
        ksnprintf(rb,sizeof(rb),"EIP=0x%08x  ESP=0x%08x  EBP=0x%08x",
                  regs->rip, regs->rsp, regs->rbp);
        PVGA(9, 4, rb, W_RED);
        ksnprintf(rb,sizeof(rb),"EAX=0x%08x  EBX=0x%08x  ECX=0x%08x  EDX=0x%08x",
                  regs->rax, regs->rbx, regs->rcx, regs->rdx);
        PVGA(10,4, rb, W_RED);
        ksnprintf(rb,sizeof(rb),"CS=0x%04x  DS=0x%04x  EFLAGS=0x%08x  ERR=0x%08x",
                  regs->cs, regs->cs, regs->rflags, regs->err_code);
        PVGA(11,4, rb, W_RED);
        ksnprintf(rb,sizeof(rb),"INT=%u (%s)",regs->int_no,
                  regs->int_no<20?"CPU Exception":"Unknown");
        PVGA(12,4, rb, W_RED);
    }

    /* Memory state */
    PVGA(14,2, "Memory:", Y_RED);
    char mbuf[64];
    ksnprintf(mbuf,sizeof(mbuf),"Heap used: %u B  free: %u B",
              heap_used_bytes(), heap_free_bytes());
    PVGA(14,10, mbuf, W_RED);

    /* EDN log */
    if (edn_count > 0) {
        PVGA(16,2, "Recent EDN warnings:", Y_RED);
        int start = edn_count > 4 ? edn_count-4 : 0;
        for (int i = start; i < edn_count && (i-start) < 4; i++) {
            char eb[80];
            ksnprintf(eb,sizeof(eb),"  [EDN] %s (%s:%d)",
                      edn_log[i].msg, edn_log[i].file, edn_log[i].line);
            PVGA(17+(i-start), 2, eb, W_RED);
        }
    }

    PVGA(22,2, "================================================================", W_RED);
    PVGA(23,2, "System halted. Press any key to reboot.", Y_RED);
    #undef PVGA
}

/* ── VBE panic screen ─────────────────────────────────────── */
static void panic_vbe(const char *msg, const char *file, int line,
                      const char *func, registers_t *regs) {
    /* Dark green-tinted background for high contrast against red text */
    vbe_clear(RGB(0x0A,0x1A,0x0F));

    /* Top stripe */
    vbe_fill_rect(0,0,800,4,RGB(0x2D,0xBF,0x50));

    /* Title area */
    vbe_fill_rect(0,4,800,40,RGB(0x0F,0x28,0x15));
    vbe_puts(8,10,"atmkoala v0.5  ──  KERNEL PANIC",
             RGB(0x2D,0xBF,0x50), RGB(0x0F,0x28,0x15));

    /* Separator */
    vbe_fill_rect(0,44,800,2,RGB(0x1A,0x50,0x25));

    /* Panic reason */
    int y=56;
    vbe_fill_rect(4,y-2,792,22,RGB(0x15,0x30,0x1A));
    vbe_puts(8,y,"PANIC:",RGB(0xFF,0x55,0x55),RGB(0x15,0x30,0x1A));
    vbe_puts(60,y,msg,RGB(0xFF,0xFF,0x80),RGB(0x15,0x30,0x1A));
    y+=26;

    /* Location */
    char locbuf[128];
    ksnprintf(locbuf,sizeof(locbuf),"at %s : line %d  in %s()",
              file, line, func?func:"?");
    vbe_puts(8,y,locbuf,RGB(0xA0,0xD0,0xA0),RGB(0x0A,0x1A,0x0F));
    y+=20;

    /* Separator */
    vbe_draw_hline(0,y,800,RGB(0x1A,0x50,0x25)); y+=8;

    /* Registers */
    if (regs) {
        vbe_puts(8,y,"Register dump:",RGB(0x2D,0xBF,0x50),RGB(0x0A,0x1A,0x0F));
        y+=18;
        char rb[128];
        ksnprintf(rb,sizeof(rb),"  EIP=0x%08x  ESP=0x%08x  EBP=0x%08x  EFLAGS=0x%08x",
                  regs->rip,regs->rsp,regs->rbp,regs->rflags);
        vbe_puts(8,y,rb,RGB(0xCC,0xFF,0xCC),RGB(0x0A,0x1A,0x0F)); y+=16;
        ksnprintf(rb,sizeof(rb),"  EAX=0x%08x  EBX=0x%08x  ECX=0x%08x  EDX=0x%08x",
                  regs->rax,regs->rbx,regs->rcx,regs->rdx);
        vbe_puts(8,y,rb,RGB(0xCC,0xFF,0xCC),RGB(0x0A,0x1A,0x0F)); y+=16;
        ksnprintf(rb,sizeof(rb),"  CS=0x%04x  DS=0x%04x  INT=%u  ERR=0x%08x",
                  regs->cs,regs->cs,regs->int_no,regs->err_code);
        vbe_puts(8,y,rb,RGB(0xCC,0xFF,0xCC),RGB(0x0A,0x1A,0x0F)); y+=16;

        /* Exception name */
        static const char *exc[]={
            "Division by Zero","Debug","NMI","Breakpoint",
            "Overflow","Bounds","Invalid Opcode","No FPU",
            "Double Fault","FPU Segment","Bad TSS","Segment Not Present",
            "Stack Fault","General Protection Fault","Page Fault","Unknown",
            "FPU Error","Alignment Check","Machine Check","SIMD FP"
        };
        if (regs->int_no < 20) {
            char xb[64];
            ksnprintf(xb,sizeof(xb),"  CPU Exception: %s",exc[regs->int_no]);
            vbe_puts(8,y,xb,RGB(0xFF,0x80,0x80),RGB(0x0A,0x1A,0x0F)); y+=16;
        }
    }

    vbe_draw_hline(0,y,800,RGB(0x1A,0x50,0x25)); y+=8;

    /* Memory */
    char mbuf[128];
    ksnprintf(mbuf,sizeof(mbuf),"Memory:  heap used %u B  free %u B",
              heap_used_bytes(), heap_free_bytes());
    vbe_puts(8,y,mbuf,RGB(0xA0,0xD0,0xA0),RGB(0x0A,0x1A,0x0F)); y+=18;

    /* EDN history */
    if (edn_count > 0) {
        vbe_puts(8,y,"Recent warnings (EDN):",RGB(0x2D,0xBF,0x50),RGB(0x0A,0x1A,0x0F));
        y+=18;
        int start = edn_count > 5 ? edn_count-5 : 0;
        for (int i = start; i < edn_count; i++) {
            char eb[96];
            ksnprintf(eb,sizeof(eb),"  [!] %s  (%s:%d)",
                      edn_log[i].msg, edn_log[i].file, edn_log[i].line);
            vbe_puts(8,y,eb,RGB(0xFF,0xE0,0x80),RGB(0x0A,0x1A,0x0F));
            y+=14;
        }
    }

    /* Bottom bar */
    vbe_fill_rect(0,576,800,24,RGB(0x0F,0x28,0x15));
    vbe_draw_hline(0,576,800,RGB(0x2D,0xBF,0x50));
    vbe_puts(8,580,"System halted.  Press any key to reboot.",
             RGB(0xA0,0xD0,0xA0),RGB(0x0F,0x28,0x15));
    vbe_puts(600,580,"atmkoala v0.5",
             RGB(0x2D,0xBF,0x50),RGB(0x0F,0x28,0x15));

    /* Bottom stripe */
    vbe_fill_rect(0,596,800,4,RGB(0x2D,0xBF,0x50));
}

/* ── Unified crash/oops diagnostic screens ────────────────── */
static void diag_vga_put(int row,int col,const char *s,uint8_t attr){
    volatile uint16_t *vga=(volatile uint16_t *)0xB8000;
    while(*s && col<80)
        vga[row*80+col++]=(uint16_t)(unsigned char)*s++|((uint16_t)attr<<8);
}

static void diag_dump_line(char *out,const uint8_t *p,uint32_t addr){
    static const char hex[]="0123456789ABCDEF";
    int n=0;
    for(int sh=28;sh>=0;sh-=4) out[n++]=hex[(addr>>sh)&15];
    out[n++]=':'; out[n++]=' ';
    for(int i=0;i<16;i++){
        uint8_t b=p[i]; out[n++]=hex[b>>4]; out[n++]=hex[b&15];
        out[n++]=(i==7)?' ':' ';
    }
    out[n]=0;
}

static void diag_vga_screen(int fatal,const char *msg,const char *file,int line,
                            const char *func,registers_t *regs){
    uint8_t attr=fatal?0x0F:0xF0;       /* white-on-black / black-on-white */
    uint8_t accent=fatal?0x0C:0xF4;
    volatile uint16_t *vga=(volatile uint16_t *)0xB8000;
    for(int i=0;i<80*25;i++) vga[i]=(uint16_t)' '|((uint16_t)attr<<8);
    diag_vga_put(1,2,fatal?"ATMKOALA KERNEL PANIC":"ATMKOALA KERNEL OOPS",accent);
    diag_vga_put(3,2,"Reason:",attr); diag_vga_put(3,11,msg,accent);
    char b[128];
    ksnprintf(b,sizeof(b),"Location: %s:%d in %s()",file,line,func?func:"?");
    diag_vga_put(4,2,b,attr);
    if(regs){
        ksnprintf(b,sizeof(b),"RIP=%08x%08x RSP=%08x%08x ERR=%08x",
                  (uint32_t)(regs->rip>>32),(uint32_t)regs->rip,
                  (uint32_t)(regs->rsp>>32),(uint32_t)regs->rsp,(uint32_t)regs->err_code);
        diag_vga_put(6,2,b,attr);
        ksnprintf(b,sizeof(b),"RAX=%08x RBX=%08x RCX=%08x RDX=%08x",
                  (uint32_t)regs->rax,(uint32_t)regs->rbx,
                  (uint32_t)regs->rcx,(uint32_t)regs->rdx);
        diag_vga_put(7,2,b,attr);
    }
    ksnprintf(b,sizeof(b),"Memory: heap used %u B, free %u B",heap_used_bytes(),heap_free_bytes());
    diag_vga_put(9,2,b,attr);
    diag_vga_put(11,2,"Memory dump (EDN log snapshot):",accent);
    const uint8_t *p=(const uint8_t *)edn_log;
    for(int r=0;r<3;r++){ diag_dump_line(b,p+r*16,(uint32_t)(uintptr_t)(p+r*16)); diag_vga_put(12+r,2,b,attr); }
    diag_vga_put(23,2,fatal?"System halted. Press any key to reboot.":"Oops is non-fatal. Press any key to return.",accent);
}

static void diag_vbe_screen(int fatal,const char *msg,const char *file,int line,
                            const char *func,registers_t *regs){
    int sw=(int)vbe.width, sh=(int)vbe.height;
    color32_t bg=fatal?RGB(0x00,0x00,0x00):RGB(0xFF,0xFF,0xFF);
    color32_t fg=fatal?RGB(0xF2,0xF2,0xF2):RGB(0x10,0x10,0x10);
    color32_t dim=fatal?RGB(0xA8,0xA8,0xA8):RGB(0x44,0x44,0x44);
    color32_t accent=fatal?RGB(0xE6,0x4E,0x57):RGB(0x1F,0x4F,0xA3);
    vbe_clear(bg);
    vbe_fill_rect(0,0,sw,4,accent);
    vbe_fill_rect(0,4,sw,38,fatal?RGB(0x10,0x10,0x10):RGB(0xE8,0xEC,0xF2));
    vbe_puts(8,12,fatal?"ATMKOALA v0.5  --  KERNEL PANIC":"ATMKOALA v0.5  --  KERNEL OOPS",accent,fatal?RGB(0x10,0x10,0x10):RGB(0xE8,0xEC,0xF2));
    int y=56; char b[144];
    vbe_puts(8,y,fatal?"PANIC:":"OOPS:",accent,bg);
    vbe_puts(64,y,msg,fg,bg); y+=22;
    ksnprintf(b,sizeof(b),"Location: %s:%d in %s()",file,line,func?func:"?");
    vbe_puts(8,y,b,dim,bg); y+=22;
    vbe_draw_hline(0,y,sw,dim); y+=10;
    vbe_puts(8,y,"Register dump:",accent,bg); y+=18;
    if(regs){
        ksnprintf(b,sizeof(b),"RIP=0x%08x%08x  RSP=0x%08x%08x  RFLAGS=0x%08x",
                  (uint32_t)(regs->rip>>32),(uint32_t)regs->rip,
                  (uint32_t)(regs->rsp>>32),(uint32_t)regs->rsp,(uint32_t)regs->rflags);
        vbe_puts(8,y,b,fg,bg); y+=16;
        ksnprintf(b,sizeof(b),"RAX=%08x RBX=%08x RCX=%08x RDX=%08x  INT=%u ERR=%08x",
                  (uint32_t)regs->rax,(uint32_t)regs->rbx,(uint32_t)regs->rcx,
                  (uint32_t)regs->rdx,(uint32_t)regs->int_no,(uint32_t)regs->err_code);
        vbe_puts(8,y,b,fg,bg); y+=16;
    } else { vbe_puts(8,y,"No interrupt frame was supplied.",dim,bg); y+=16; }
    ksnprintf(b,sizeof(b),"Memory: heap used %u B  free %u B",heap_used_bytes(),heap_free_bytes());
    vbe_puts(8,y,b,fg,bg); y+=20;
    vbe_puts(8,y,"Memory dump (EDN log snapshot):",accent,bg); y+=18;
    const uint8_t *p=(const uint8_t *)edn_log;
    for(int r=0;r<4 && y+16<sh-28;r++){ diag_dump_line(b,p+r*16,(uint32_t)(uintptr_t)(p+r*16)); vbe_puts(8,y,b,fg,bg); y+=16; }
    vbe_fill_rect(0,sh-24,sw,24,fatal?RGB(0x10,0x10,0x10):RGB(0xE8,0xEC,0xF2));
    vbe_draw_hline(0,sh-24,sw,accent);
    vbe_puts(8,sh-20,fatal?"System halted. Press any key to reboot.":"Oops is non-fatal. Press any key to return to Exp.",fg,fatal?RGB(0x10,0x10,0x10):RGB(0xE8,0xEC,0xF2));
}

void kernel_oops(const char *msg,const char *file,int line,const char *func,registers_t *regs){
    int restore_exp=exp_is_active();
    if(vbe.active){ vbe_double_buffer_disable(); diag_vbe_screen(0,msg,file,line,func,regs); }
    else diag_vga_screen(0,msg,file,line,func,regs);
    keyboard_init();
    keyboard_getkey();
    if(restore_exp && vbe.active){ (void)vbe_double_buffer_enable(); exp_request_full_redraw(); }
}

/* ── Main panic entry ─────────────────────────────────────── */
__attribute__((noreturn))
void kernel_panic(const char *msg, const char *file, int line,
                  const char *func, registers_t *regs) {
    /* Disable interrupts immediately */
    __asm__ volatile("cli");

    /* Exp may currently own a software back buffer.  A fatal diagnostic
     * must draw directly to the physical framebuffer. */
    if (vbe.active) {
        vbe_double_buffer_disable();
        diag_vbe_screen(1,msg,file,line,func,regs);
    } else {
        diag_vga_screen(1,msg,file,line,func,regs);
    }

    /* Wait for keypress then reboot */
    keyboard_init(); /* re-init to clear state */
    keyboard_getkey();

    /* Reboot via keyboard controller */
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    while(1) __asm__ volatile("hlt");
}

/* ── Exception panic (from IDT) ──────────────────────────── */
__attribute__((noreturn))
void kernel_panic_exception(registers_t *regs) {
    panic_last_int = regs ? regs->int_no : 0xFFFFFFFFu;
    panic_last_err = regs ? regs->err_code : 0;
    panic_last_rip = regs ? regs->rip : 0;
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    panic_last_cr2 = cr2;
    static const char *exc_names[] = {
        "Division by Zero",       "Debug Exception",
        "Non-Maskable Interrupt", "Breakpoint",
        "Overflow",               "Bound Range Exceeded",
        "Invalid Opcode",         "Device Not Available",
        "Double Fault",           "Coprocessor Segment Overrun",
        "Invalid TSS",            "Segment Not Present",
        "Stack-Segment Fault",    "General Protection Fault",
        "Page Fault",             "Reserved",
        "x87 FPU Error",          "Alignment Check",
        "Machine Check",          "SIMD Floating-Point",
    };
    const char *name = (regs->int_no < 20) ?
        exc_names[regs->int_no] : "Unknown Exception";

    char msg[64];
    ksnprintf(msg, sizeof(msg), "CPU Exception #%u: %s",
              regs->int_no, name);

    kernel_panic(msg, "cpu_exception", (int)regs->int_no,
                 "isr_handler", regs);
}

void panic_init(void) {
    /* Hook is already installed via idt.c — nothing extra needed.
     * isr_handler() calls kernel_panic_exception() for CPU exceptions. */
    edn_count = 0;
}
