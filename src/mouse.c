#include "mouse.h"
#include "idt.h"
#include "util.h"
#include <stdint.h>

#define PS2_DATA       0x60
#define PS2_STATUS     0x64
#define PS2_COMMAND    0x64
#define PS2_ST_OUTPUT  0x01
#define PS2_ST_INPUT   0x02
#define PS2_ST_AUX     0x20

#define PS2_CMD_READ_CFG   0x20
#define PS2_CMD_WRITE_CFG  0x60
#define PS2_CMD_ENABLE_AUX 0xA8
#define PS2_CMD_WRITE_AUX  0xD4

#define MOUSE_CMD_DEFAULTS 0xF6
#define MOUSE_CMD_ENABLE   0xF4
#define MOUSE_ACK          0xFA
#define MOUSE_RESEND       0xFE

enum { MOUSE_INIT_NONE=0, MOUSE_INIT_ENABLE_AUX, MOUSE_INIT_READ_CFG,
       MOUSE_INIT_WRITE_CFG, MOUSE_INIT_DEFAULTS, MOUSE_INIT_ENABLE,
       MOUSE_INIT_READY };

static mouse_state_t g_mouse;
static int g_screen_w=800, g_screen_h=600;
static uint8_t g_packet[3];
static int g_packet_pos;
/* Even outside updates, odd while the IRQ publishes a new x/y/button tuple. */
static volatile uint32_t g_state_seq;

static int mouse_packet_signs_ok(uint8_t flags,uint8_t x,uint8_t y){
    return (!!(flags&0x10)==!!(x&0x80)) && (!!(flags&0x20)==!!(y&0x80));
}

static int wait_input_clear(void) {
    for (int i=0;i<100000;i++) {
        if (!(inb(PS2_STATUS)&PS2_ST_INPUT)) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

/* Controller replies (such as the configuration byte) must not consume a
 * stale AUX packet.  Real controllers can retain boot-time bytes in OBF. */
static int wait_output_byte(uint8_t *out) {
    for (int i=0;i<100000;i++) {
        uint8_t status=inb(PS2_STATUS);
        if (status&PS2_ST_OUTPUT) {
            uint8_t value=inb(PS2_DATA);
            if (!(status&PS2_ST_AUX)) { *out=value; return 1; }
            g_mouse.controller_drained++;
        }
        __asm__ volatile("pause");
    }
    return 0;
}
static void drain_output_buffer(void){
    for(int i=0;i<32;i++){
        uint8_t status=inb(PS2_STATUS);
        if(!(status&PS2_ST_OUTPUT))break;
        (void)inb(PS2_DATA);g_mouse.controller_drained++;
    }
}

static int wait_aux_byte(uint8_t *out) {
    for (int i=0;i<100000;i++) {
        uint8_t status=inb(PS2_STATUS);
        if (status&PS2_ST_OUTPUT) {
            uint8_t value=inb(PS2_DATA);
            if (status&PS2_ST_AUX) { *out=value; return 1; }
            g_mouse.controller_drained++;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

static int controller_write(uint8_t value) {
    if (!wait_input_clear()) return 0;
    outb(PS2_COMMAND,value);
    return 1;
}

static int mouse_write(uint8_t value) {
    if (!controller_write(PS2_CMD_WRITE_AUX)) return 0;
    if (!wait_input_clear()) return 0;
    outb(PS2_DATA,value);
    return 1;
}

static int mouse_command(uint8_t value) {
    for(int attempt=0;attempt<3;attempt++){
        uint8_t reply=0;
        if(!mouse_write(value)||!wait_aux_byte(&reply))return 0;
        if(reply==MOUSE_ACK)return 1;
        if(reply!=MOUSE_RESEND)return 0;
    }
    return 0;
}

static void mouse_irq(registers_t *r) {
    (void)r;
    uint8_t status=inb(PS2_STATUS);
    if (!(status&PS2_ST_OUTPUT)) return;
    /* Always consume OBF inside IRQ12.  Some physical controllers briefly
     * lose the AUX status bit; leaving the byte unread wedges the PS/2 port. */
    uint8_t byte=inb(PS2_DATA);
    if (!(status&PS2_ST_AUX)) { g_mouse.controller_drained++; return; }
    g_mouse.irq_bytes++;

    /* Packet byte zero always has bit 3 set.  Re-synchronise after noise. */
    if (g_packet_pos==0 && !(byte&0x08)) { g_mouse.sync_losses++; return; }
    g_packet[g_packet_pos++]=byte;
    if (g_packet_pos<3) return;
    g_packet_pos=0;

    uint8_t flags=g_packet[0];
    if (flags&0xC0) { g_mouse.dropped_packets++; return; } /* X/Y overflow: discard this packet */
    /* A lost byte can still produce a superficially valid bit-3 header.
     * PS/2 sign bits must agree with movement-byte bit 7; reject a malformed
     * tuple rather than interpreting it as a large cursor movement. */
    if(!mouse_packet_signs_ok(flags,g_packet[1],g_packet[2])){g_mouse.dropped_packets++;return;}
    int dx=(int)g_packet[1]; if (flags&0x10) dx-=256;
    int dy=(int)g_packet[2]; if (flags&0x20) dy-=256;
    int nx=g_mouse.x+dx;
    int ny=g_mouse.y-dy; /* PS/2 Y grows upward; framebuffer Y downward */
    if (nx<0) nx=0; else if (nx>=g_screen_w) nx=g_screen_w-1;
    if (ny<0) ny=0; else if (ny>=g_screen_h) ny=g_screen_h-1;
    /* Publish the logical cursor as one coherent sample for the Exp frame. */
    g_state_seq++; __asm__ volatile("" ::: "memory");
    g_mouse.x=nx; g_mouse.y=ny;
    g_mouse.buttons=(uint8_t)(flags&0x07);
    g_mouse.packets++;
    __asm__ volatile("" ::: "memory"); g_state_seq++;
}

void mouse_init(int screen_w, int screen_h) {
    kmemset(&g_mouse,0,sizeof(g_mouse));
    g_packet_pos=0; g_state_seq=0;
    if (screen_w>0) g_screen_w=screen_w;
    if (screen_h>0) g_screen_h=screen_h;
    g_mouse.x=g_screen_w/2; g_mouse.y=g_screen_h/2;

    /* No controller reply must block boot: absence simply disables the cursor. */
    cpu_cli();
    /* Discard stale POST/controller bytes before issuing a read-config
     * command.  Treating one as the config byte can mask IRQ12 on hardware. */
    drain_output_buffer();
    if (!controller_write(PS2_CMD_ENABLE_AUX)) { g_mouse.init_status=MOUSE_INIT_ENABLE_AUX; cpu_sti(); return; }

    uint8_t cfg=0;
    /* Command-byte replies originate from the controller, not the auxiliary
     * device. Preserve its translation bit or keyboard scancodes change set. */
    if (!controller_write(PS2_CMD_READ_CFG) || !wait_output_byte(&cfg)) { g_mouse.init_status=MOUSE_INIT_READ_CFG; cpu_sti(); return; }
    cfg|=0x02;       /* enable IRQ12 */
    cfg&=(uint8_t)~0x20; /* enable auxiliary clock */
    if (!controller_write(PS2_CMD_WRITE_CFG) || !wait_input_clear()) { g_mouse.init_status=MOUSE_INIT_WRITE_CFG; cpu_sti(); return; }
    outb(PS2_DATA,cfg);
    if (!wait_input_clear()) { g_mouse.init_status=MOUSE_INIT_WRITE_CFG; cpu_sti(); return; }

    if (!mouse_command(MOUSE_CMD_DEFAULTS)) { g_mouse.init_status=MOUSE_INIT_DEFAULTS; cpu_sti(); return; }
    if (!mouse_command(MOUSE_CMD_ENABLE)) { g_mouse.init_status=MOUSE_INIT_ENABLE; cpu_sti(); return; }

    irq_install_handler(12,mouse_irq);
    /* Unmask cascade IRQ2 and mouse IRQ12 on the legacy PIC. */
    outb(0x21,(uint8_t)(inb(0x21)&(uint8_t)~0x04));
    outb(0xA1,(uint8_t)(inb(0xA1)&(uint8_t)~0x10));
    g_mouse.available=1;
    g_mouse.init_status=MOUSE_INIT_READY;
    cpu_sti();
}

const mouse_state_t *mouse_state(void) { return &g_mouse; }
int mouse_snapshot(mouse_state_t *out){
    if(!out)return 0;
    for(int attempt=0;attempt<8;attempt++){
        uint32_t before=g_state_seq;if(before&1)continue;
        __asm__ volatile("" ::: "memory");
        out->x=g_mouse.x;out->y=g_mouse.y;out->buttons=g_mouse.buttons;
        out->available=g_mouse.available;out->packets=g_mouse.packets;
        out->dropped_packets=g_mouse.dropped_packets;out->irq_bytes=g_mouse.irq_bytes;
        out->sync_losses=g_mouse.sync_losses;out->controller_drained=g_mouse.controller_drained;
        out->init_status=g_mouse.init_status;
        __asm__ volatile("" ::: "memory");
        if(before==g_state_seq)return 1;
    }
    return 0;
}
int mouse_packet_selftest(void){
    return mouse_packet_signs_ok(0x08,1,1)&&mouse_packet_signs_ok(0x38,0xff,0xff)&&
           !mouse_packet_signs_ok(0x08,0xff,1)&&!mouse_packet_signs_ok(0x08,1,0xff)?0:-1;
}
const char *mouse_status_string(void){
    switch(g_mouse.init_status){
    case MOUSE_INIT_READY:return "ready";
    case MOUSE_INIT_ENABLE_AUX:return "PS/2 controller did not enable auxiliary port";
    case MOUSE_INIT_READ_CFG:return "PS/2 controller configuration read timed out";
    case MOUSE_INIT_WRITE_CFG:return "PS/2 controller configuration write timed out";
    case MOUSE_INIT_DEFAULTS:return "mouse did not acknowledge defaults command";
    case MOUSE_INIT_ENABLE:return "mouse did not acknowledge streaming-enable command";
    default:return "not initialized";
    }
}
