/*  vbe.c — VBE/LFB framebuffer driver for atmkoala OS v0.5
 *
 *  Supports 32bpp and 24bpp modes delivered by GRUB via Multiboot.
 *  Falls back to VGA text if no framebuffer is available.
 *  Includes 8x16 bitmap font, scrollable console, boot splash.
 */
#include "vbe.h"
#include "util.h"
#include "font.h"
#include "kmalloc.h"
#include <stdint.h>
#include <stddef.h>

vbe_state_t vbe = {0};

/* One reusable software surface for Exp.  Keeping it allocated avoids heap
 * churn when the desktop is closed and reopened. */
static uint8_t *vbe_backbuffer = NULL;
static uint32_t vbe_backbuffer_size = 0;
static int vbe_buffered = 0;

static uint8_t *vbe_draw_base(void) {
    return vbe_buffered && vbe_backbuffer ? vbe_backbuffer : (uint8_t *)vbe.fb;
}

int vbe_double_buffer_enable(void) {
    if (!vbe.active || !vbe.pitch || !vbe.height) return -1;
    uint32_t bytes=vbe.pitch*vbe.height;
    if (!vbe_backbuffer || vbe_backbuffer_size<bytes) {
        uint8_t *buf=(uint8_t *)kmalloc(bytes);
        if (!buf) return -1;
        vbe_backbuffer=buf;
        vbe_backbuffer_size=bytes;
    }
    uint8_t *front=(uint8_t *)vbe.fb;
    kmemcpy(vbe_backbuffer,front,bytes);
    vbe_buffered=1;
    return 0;
}

void vbe_present(void) {
    if (!vbe.active || !vbe_buffered || !vbe_backbuffer) return;
    uint32_t bytes=vbe.pitch*vbe.height;
    uint8_t *front=(uint8_t *)vbe.fb;
    kmemcpy(front,vbe_backbuffer,bytes);
}

void vbe_double_buffer_disable(void) {
    vbe_present();
    vbe_buffered=0;
}

/* ═══════════════════════════════════════════════════════════════
 *  8×16 bitmap font — partial ASCII (0x20–0x7E)
 *  Each character = 16 bytes (one byte per row, MSB = leftmost pixel)
 * ═══════════════════════════════════════════════════════════════ */
#define FONT_W 8
#define FONT_H 16
#define FONT_FIRST 0x20

static const uint8_t font8x16[][FONT_H] = {
/* 0x20 space */  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 0x21 !     */  {0,0,0x18,0x18,0x18,0x18,0x18,0x18,0,0x18,0,0,0,0,0,0},
/* 0x22 "     */  {0,0x66,0x66,0x66,0,0,0,0,0,0,0,0,0,0,0,0},
/* 0x23 #     */  {0,0x36,0x36,0x7f,0x36,0x36,0x7f,0x36,0x36,0,0,0,0,0,0,0},
/* 0x24 $     */  {0,0x0c,0x3e,0x6b,0x68,0x3e,0x0b,0x6b,0x3e,0x0c,0,0,0,0,0,0},
/* 0x25 %     */  {0,0,0x63,0x66,0x0c,0x18,0x33,0x67,0,0,0,0,0,0,0,0},
/* 0x26 &     */  {0,0,0x1c,0x36,0x1c,0x3b,0x6e,0x66,0x3b,0,0,0,0,0,0,0},
/* 0x27 '     */  {0,0,0x18,0x18,0x18,0,0,0,0,0,0,0,0,0,0,0},
/* 0x28 (     */  {0,0,0x0e,0x18,0x18,0x18,0x18,0x18,0x0e,0,0,0,0,0,0,0},
/* 0x29 )     */  {0,0,0x70,0x18,0x18,0x18,0x18,0x18,0x70,0,0,0,0,0,0,0},
/* 0x2A *     */  {0,0,0,0x66,0x3c,0xff,0x3c,0x66,0,0,0,0,0,0,0,0},
/* 0x2B +     */  {0,0,0,0x18,0x18,0x7e,0x18,0x18,0,0,0,0,0,0,0,0},
/* 0x2C ,     */  {0,0,0,0,0,0,0,0x38,0x38,0x70,0,0,0,0,0,0},
/* 0x2D -     */  {0,0,0,0,0,0x7e,0,0,0,0,0,0,0,0,0,0},
/* 0x2E .     */  {0,0,0,0,0,0,0,0x38,0x38,0,0,0,0,0,0,0},
/* 0x2F /     */  {0,0,0x03,0x06,0x0c,0x18,0x30,0x60,0,0,0,0,0,0,0,0},
/* 0x30 0     */  {0,0,0x3e,0x63,0x67,0x6f,0x7b,0x73,0x3e,0,0,0,0,0,0,0},
/* 0x31 1     */  {0,0,0x0c,0x1c,0x0c,0x0c,0x0c,0x0c,0x3f,0,0,0,0,0,0,0},
/* 0x32 2     */  {0,0,0x3e,0x63,0x03,0x0e,0x38,0x63,0x7f,0,0,0,0,0,0,0},
/* 0x33 3     */  {0,0,0x3e,0x63,0x03,0x1e,0x03,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x34 4     */  {0,0,0x06,0x0e,0x1e,0x36,0x7f,0x06,0x06,0,0,0,0,0,0,0},
/* 0x35 5     */  {0,0,0x7f,0x60,0x7e,0x03,0x03,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x36 6     */  {0,0,0x1e,0x30,0x60,0x7e,0x63,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x37 7     */  {0,0,0x7f,0x63,0x06,0x0c,0x18,0x18,0x18,0,0,0,0,0,0,0},
/* 0x38 8     */  {0,0,0x3e,0x63,0x63,0x3e,0x63,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x39 9     */  {0,0,0x3e,0x63,0x63,0x3f,0x03,0x06,0x3c,0,0,0,0,0,0,0},
/* 0x3A :     */  {0,0,0,0x18,0x18,0,0,0x18,0x18,0,0,0,0,0,0,0},
/* 0x3B ;     */  {0,0,0,0x18,0x18,0,0,0x18,0x18,0x30,0,0,0,0,0,0},
/* 0x3C <     */  {0,0,0x06,0x0c,0x18,0x30,0x18,0x0c,0x06,0,0,0,0,0,0,0},
/* 0x3D =     */  {0,0,0,0,0x7e,0,0,0x7e,0,0,0,0,0,0,0,0},
/* 0x3E >     */  {0,0,0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0,0,0,0,0,0,0},
/* 0x3F ?     */  {0,0,0x3e,0x63,0x03,0x0e,0x18,0,0x18,0,0,0,0,0,0,0},
/* 0x40 @     */  {0,0,0x3e,0x63,0x6f,0x6f,0x6e,0x60,0x3e,0,0,0,0,0,0,0},
/* 0x41 A     */  {0,0,0x1c,0x36,0x63,0x63,0x7f,0x63,0x63,0,0,0,0,0,0,0},
/* 0x42 B     */  {0,0,0x7e,0x33,0x33,0x3e,0x33,0x33,0x7e,0,0,0,0,0,0,0},
/* 0x43 C     */  {0,0,0x1e,0x33,0x60,0x60,0x60,0x33,0x1e,0,0,0,0,0,0,0},
/* 0x44 D     */  {0,0,0x7c,0x36,0x33,0x33,0x33,0x36,0x7c,0,0,0,0,0,0,0},
/* 0x45 E     */  {0,0,0x7f,0x60,0x60,0x7e,0x60,0x60,0x7f,0,0,0,0,0,0,0},
/* 0x46 F     */  {0,0,0x7f,0x60,0x60,0x7e,0x60,0x60,0x60,0,0,0,0,0,0,0},
/* 0x47 G     */  {0,0,0x1e,0x33,0x60,0x60,0x67,0x33,0x1e,0,0,0,0,0,0,0},
/* 0x48 H     */  {0,0,0x63,0x63,0x63,0x7f,0x63,0x63,0x63,0,0,0,0,0,0,0},
/* 0x49 I     */  {0,0,0x3f,0x0c,0x0c,0x0c,0x0c,0x0c,0x3f,0,0,0,0,0,0,0},
/* 0x4A J     */  {0,0,0x0f,0x03,0x03,0x03,0x63,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x4B K     */  {0,0,0x63,0x66,0x6c,0x78,0x6c,0x66,0x63,0,0,0,0,0,0,0},
/* 0x4C L     */  {0,0,0x60,0x60,0x60,0x60,0x60,0x60,0x7f,0,0,0,0,0,0,0},
/* 0x4D M     */  {0,0,0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0,0,0,0,0,0,0},
/* 0x4E N     */  {0,0,0x63,0x73,0x7b,0x6f,0x67,0x63,0x63,0,0,0,0,0,0,0},
/* 0x4F O     */  {0,0,0x3e,0x63,0x63,0x63,0x63,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x50 P     */  {0,0,0x7e,0x63,0x63,0x7e,0x60,0x60,0x60,0,0,0,0,0,0,0},
/* 0x51 Q     */  {0,0,0x3e,0x63,0x63,0x63,0x6b,0x66,0x3d,0,0,0,0,0,0,0},
/* 0x52 R     */  {0,0,0x7e,0x63,0x63,0x7e,0x6c,0x66,0x63,0,0,0,0,0,0,0},
/* 0x53 S     */  {0,0,0x3e,0x63,0x60,0x3e,0x03,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x54 T     */  {0,0,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0,0,0},
/* 0x55 U     */  {0,0,0x63,0x63,0x63,0x63,0x63,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x56 V     */  {0,0,0x63,0x63,0x63,0x36,0x36,0x1c,0x1c,0,0,0,0,0,0,0},
/* 0x57 W     */  {0,0,0x63,0x63,0x6b,0x6b,0x7f,0x77,0x63,0,0,0,0,0,0,0},
/* 0x58 X     */  {0,0,0x63,0x36,0x1c,0x1c,0x1c,0x36,0x63,0,0,0,0,0,0,0},
/* 0x59 Y     */  {0,0,0x66,0x66,0x3c,0x18,0x18,0x18,0x18,0,0,0,0,0,0,0},
/* 0x5A Z     */  {0,0,0x7f,0x03,0x06,0x0c,0x18,0x30,0x7f,0,0,0,0,0,0,0},
/* 0x5B [     */  {0,0,0x3e,0x30,0x30,0x30,0x30,0x30,0x3e,0,0,0,0,0,0,0},
/* 0x5C \     */  {0,0,0x60,0x30,0x18,0x0c,0x06,0x03,0,0,0,0,0,0,0,0},
/* 0x5D ]     */  {0,0,0x3e,0x06,0x06,0x06,0x06,0x06,0x3e,0,0,0,0,0,0,0},
/* 0x5E ^     */  {0,0x08,0x1c,0x36,0x63,0,0,0,0,0,0,0,0,0,0,0},
/* 0x5F _     */  {0,0,0,0,0,0,0,0,0,0x7f,0,0,0,0,0,0},
/* 0x60 `     */  {0,0,0x1c,0x18,0x0c,0,0,0,0,0,0,0,0,0,0,0},
/* 0x61 a     */  {0,0,0,0,0x3e,0x03,0x3f,0x63,0x3f,0,0,0,0,0,0,0},
/* 0x62 b     */  {0,0,0x60,0x60,0x7e,0x63,0x63,0x63,0x7e,0,0,0,0,0,0,0},
/* 0x63 c     */  {0,0,0,0,0x3e,0x63,0x60,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x64 d     */  {0,0,0x03,0x03,0x3f,0x63,0x63,0x63,0x3f,0,0,0,0,0,0,0},
/* 0x65 e     */  {0,0,0,0,0x3e,0x63,0x7f,0x60,0x3e,0,0,0,0,0,0,0},
/* 0x66 f     */  {0,0,0x1e,0x30,0x7c,0x30,0x30,0x30,0x30,0,0,0,0,0,0,0},
/* 0x67 g     */  {0,0,0,0,0x3f,0x63,0x63,0x3f,0x03,0x3e,0,0,0,0,0,0},
/* 0x68 h     */  {0,0,0x60,0x60,0x7e,0x63,0x63,0x63,0x63,0,0,0,0,0,0,0},
/* 0x69 i     */  {0,0,0x0c,0,0x1c,0x0c,0x0c,0x0c,0x1e,0,0,0,0,0,0,0},
/* 0x6A j     */  {0,0,0x06,0,0x06,0x06,0x06,0x06,0x66,0x3c,0,0,0,0,0,0},
/* 0x6B k     */  {0,0,0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0,0,0,0,0,0,0},
/* 0x6C l     */  {0,0,0x1c,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0,0,0,0,0,0,0},
/* 0x6D m     */  {0,0,0,0,0x7e,0x6b,0x6b,0x6b,0x63,0,0,0,0,0,0,0},
/* 0x6E n     */  {0,0,0,0,0x7e,0x63,0x63,0x63,0x63,0,0,0,0,0,0,0},
/* 0x6F o     */  {0,0,0,0,0x3e,0x63,0x63,0x63,0x3e,0,0,0,0,0,0,0},
/* 0x70 p     */  {0,0,0,0,0x7e,0x63,0x63,0x7e,0x60,0x60,0,0,0,0,0,0},
/* 0x71 q     */  {0,0,0,0,0x3f,0x63,0x63,0x3f,0x03,0x03,0,0,0,0,0,0},
/* 0x72 r     */  {0,0,0,0,0x6e,0x70,0x60,0x60,0x60,0,0,0,0,0,0,0},
/* 0x73 s     */  {0,0,0,0,0x3e,0x60,0x3e,0x03,0x3e,0,0,0,0,0,0,0},
/* 0x74 t     */  {0,0,0x18,0x18,0x7e,0x18,0x18,0x18,0x0e,0,0,0,0,0,0,0},
/* 0x75 u     */  {0,0,0,0,0x63,0x63,0x63,0x63,0x3f,0,0,0,0,0,0,0},
/* 0x76 v     */  {0,0,0,0,0x63,0x63,0x36,0x36,0x1c,0,0,0,0,0,0,0},
/* 0x77 w     */  {0,0,0,0,0x63,0x63,0x6b,0x7f,0x36,0,0,0,0,0,0,0},
/* 0x78 x     */  {0,0,0,0,0x63,0x36,0x1c,0x36,0x63,0,0,0,0,0,0,0},
/* 0x79 y     */  {0,0,0,0,0x63,0x63,0x3f,0x03,0x3e,0,0,0,0,0,0,0},
/* 0x7A z     */  {0,0,0,0,0x7f,0x06,0x0c,0x18,0x7f,0,0,0,0,0,0,0},
/* 0x7B {     */  {0,0,0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0,0,0,0,0,0,0},
/* 0x7C |     */  {0,0,0x18,0x18,0x18,0,0x18,0x18,0x18,0,0,0,0,0,0,0},
/* 0x7D }     */  {0,0,0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0,0,0,0,0,0,0},
/* 0x7E ~     */  {0,0,0x3b,0x6e,0,0,0,0,0,0,0,0,0,0,0,0},
};
#define FONT_COUNT (sizeof(font8x16)/sizeof(font8x16[0]))

/* ═══════════════════════════════════════════════════════════════
 *  Init
 * ═══════════════════════════════════════════════════════════════ */
int vbe_init(mb_fb_info_t *fi) {
    if (!fi || fi->type != 1) return -1;   /* need RGB framebuffer */
    if (fi->bpp != 32 && fi->bpp != 24) return -1;

    /* Build the full 64-bit physical address. On systems with >=4GB RAM
     * (or where firmware places the LFB above the 4GB boundary) addr_hi
     * is non-zero and MUST be combined with addr_lo, otherwise the
     * pointer silently truncates and every draw call writes into the
     * wrong physical page — this is what causes a black screen on real
     * hardware while still "working" under QEMU (where the LFB happens
     * to sit below 4GB by default). */
    uint64_t fb_phys = ((uint64_t)fi->addr_hi << 32) | (uint64_t)fi->addr_lo;
    vbe.fb     = (uint32_t *)(uintptr_t)fb_phys;
    vbe.width  = fi->width;
    vbe.height = fi->height;
    vbe.pitch  = fi->pitch;
    vbe.bpp    = fi->bpp;
    vbe.active = 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Pixel primitives
 * ═══════════════════════════════════════════════════════════════ */
void vbe_putpixel(int x, int y, color32_t c) {
    if (!vbe.active) return;
    if ((unsigned)x >= vbe.width || (unsigned)y >= vbe.height) return;
    uint8_t *row = vbe_draw_base() + y * vbe.pitch;
    if (vbe.bpp == 32) {
        ((uint32_t *)row)[x] = c;
    } else {
        row[x*3+0] = (uint8_t)(c);
        row[x*3+1] = (uint8_t)(c >> 8);
        row[x*3+2] = (uint8_t)(c >> 16);
    }
}

void vbe_fill_rect(int x, int y, int w, int h, color32_t c) {
    if(!vbe.active||w<=0||h<=0)return;
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x>=(int)vbe.width||y>=(int)vbe.height||w<=0||h<=0)return;
    if(w>(int)vbe.width-x)w=(int)vbe.width-x;
    if(h>(int)vbe.height-y)h=(int)vbe.height-y;
    uint8_t *base=vbe_draw_base();
    if(vbe.bpp==32){
        for(int row=0;row<h;row++){
            uint32_t *p=(uint32_t *)(base+(uint32_t)(y+row)*vbe.pitch)+(uint32_t)x;
            int col=0;for(;col+3<w;col+=4){p[col]=c;p[col+1]=c;p[col+2]=c;p[col+3]=c;}for(;col<w;col++)p[col]=c;
        }
    }else{
        uint8_t b=(uint8_t)c,g=(uint8_t)(c>>8),r=(uint8_t)(c>>16);
        for(int row=0;row<h;row++){uint8_t *p=base+(uint32_t)(y+row)*vbe.pitch+(uint32_t)x*3u;for(int col=0;col<w;col++){p[0]=b;p[1]=g;p[2]=r;p+=3;}}
    }
}

void vbe_blit_rgba_scaled(const uint8_t *rgba,int src_w,int src_h,int dst_x,int dst_y,int dst_w,int dst_h){
    if(!vbe.active||!rgba||src_w<=0||src_h<=0||dst_w<=0||dst_h<=0)return;
    int clip_l=dst_x<0?-dst_x:0,clip_t=dst_y<0?-dst_y:0;
    int clip_r=dst_x+dst_w>(int)vbe.width?(int)vbe.width-dst_x:dst_w;
    int clip_b=dst_y+dst_h>(int)vbe.height?(int)vbe.height-dst_y:dst_h;
    if(clip_l>=clip_r||clip_t>=clip_b)return;
    uint8_t *base=vbe_draw_base();
    for(int dy=clip_t;dy<clip_b;dy++){
        int sy=(int)((uint64_t)dy*(uint64_t)src_h/(uint64_t)dst_h);
        const uint8_t *srow=rgba+((uint32_t)sy*(uint32_t)src_w*4u);
        uint8_t *drow=base+(uint32_t)(dst_y+dy)*vbe.pitch;
        for(int dx=clip_l;dx<clip_r;dx++){
            int sx=(int)((uint64_t)dx*(uint64_t)src_w/(uint64_t)dst_w);
            const uint8_t *sp=srow+(uint32_t)sx*4u;uint8_t a=sp[3];
            if(vbe.bpp==32){
                uint32_t *dp=((uint32_t *)drow)+(uint32_t)(dst_x+dx);uint32_t old=*dp;
                if(a==255)*dp=RGB(sp[0],sp[1],sp[2]);
                else if(a){uint32_t inv=255u-a;*dp=RGB(((uint32_t)sp[0]*a+((old>>16)&255u)*inv)/255u,((uint32_t)sp[1]*a+((old>>8)&255u)*inv)/255u,((uint32_t)sp[2]*a+(old&255u)*inv)/255u);}
            }else{
                uint8_t *dp=drow+(uint32_t)(dst_x+dx)*3u;
                if(a==255){dp[0]=sp[2];dp[1]=sp[1];dp[2]=sp[0];}
                else if(a){uint32_t inv=255u-a;dp[0]=(uint8_t)(((uint32_t)sp[2]*a+(uint32_t)dp[0]*inv)/255u);dp[1]=(uint8_t)(((uint32_t)sp[1]*a+(uint32_t)dp[1]*inv)/255u);dp[2]=(uint8_t)(((uint32_t)sp[0]*a+(uint32_t)dp[2]*inv)/255u);}
            }
        }
    }
}

void vbe_draw_hline(int x, int y, int len, color32_t c) {
    vbe_fill_rect(x, y, len, 1, c);
}
void vbe_draw_vline(int x, int y, int len, color32_t c) {
    vbe_fill_rect(x, y, 1, len, c);
}
void vbe_draw_rect(int x, int y, int w, int h, color32_t c) {
    vbe_draw_hline(x,   y,       w, c);
    vbe_draw_hline(x,   y+h-1,   w, c);
    vbe_draw_vline(x,   y,       h, c);
    vbe_draw_vline(x+w-1, y,     h, c);
}

void vbe_clear(color32_t c) {
    vbe_fill_rect(0, 0, (int)vbe.width, (int)vbe.height, c);
}

/* ═══════════════════════════════════════════════════════════════
 *  Font rendering
 * ═══════════════════════════════════════════════════════════════ */
void vbe_putchar(int x, int y, char ch, color32_t fg, color32_t bg) {
    uint32_t cp = (uint32_t)(unsigned char)ch;
    vbe_putchar_cp(x, y, cp, fg, bg);
}

void vbe_putchar_cp(int x, int y, uint32_t codepoint, color32_t fg, color32_t bg) {
    if (!vbe.active) return;
    const uint8_t *glyph = font_get_glyph(codepoint);
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            color32_t c = (bits & (0x80 >> col)) ? fg : bg;
            vbe_putpixel(x + col, y + row, c);
        }
    }
}

void vbe_puts(int x, int y, const char *s, color32_t fg, color32_t bg) {
    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint32_t cp = utf8_decode(&p);
        vbe_putchar_cp(x, y, cp, fg, bg);
        x += FONT_W;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  VBE Console (scrollable text console over LFB)
 * ═══════════════════════════════════════════════════════════════ */
#define CONS_COLS  (vbe.width  / FONT_W)
#define CONS_ROWS  (vbe.height / FONT_H)

static int  cons_cx = 0, cons_cy = 0;
static color32_t cons_fg = 0xFFD9A0;   /* caramel */
static color32_t cons_bg = 0x0D1117;   /* near-black */

void vbe_console_init(void) {
    cons_cx = cons_cy = 0;
    cons_fg = RGB(0xFF, 0xD9, 0xA0);
    cons_bg = RGB(0x0D, 0x11, 0x17);
    vbe_clear(cons_bg);
}

void vbe_console_set_color(color32_t fg, color32_t bg) {
    cons_fg = fg;
    cons_bg = bg;
}

static void vbe_console_scroll(void) {
    /* Blit rows [1..CONS_ROWS-1] → [0..CONS_ROWS-2] */
    uint32_t line_bytes = (uint32_t)FONT_H * vbe.pitch;
    uint8_t *dst = (uint8_t *)vbe.fb;
    uint8_t *src = dst + line_bytes;
    uint32_t copy_bytes = ((uint32_t)CONS_ROWS - 1) * line_bytes;
    for (uint32_t i = 0; i < copy_bytes; i++) dst[i] = src[i];
    /* clear last row */
    vbe_fill_rect(0, (int)((CONS_ROWS - 1) * FONT_H),
                  (int)vbe.width, FONT_H, cons_bg);
    cons_cy = (int)CONS_ROWS - 1;
}

/* Draw cursor (underscore) */
static void draw_cursor(int show) {
    color32_t c = show ? cons_fg : cons_bg;
    vbe_draw_hline(cons_cx * FONT_W,
                   cons_cy * FONT_H + FONT_H - 2,
                   FONT_W, c);
}

/* UTF-8 state for multi-byte sequences */
static uint8_t  utf8_buf[4];
static int      utf8_cnt = 0;
static int      utf8_need = 0;

static void vbe_con_emit_cp(uint32_t cp) {
    draw_cursor(0);
    if (cp == '\n') {
        cons_cx = 0;
        if (++cons_cy >= (int)CONS_ROWS) vbe_console_scroll();
    } else if (cp == '\r') {
        cons_cx = 0;
    } else if (cp == '\b') {
        if (cons_cx > 0) {
            --cons_cx;
            vbe_putchar_cp(cons_cx*FONT_W, cons_cy*FONT_H, ' ', cons_fg, cons_bg);
        }
    } else if (cp == '\t') {
        int next = (cons_cx + 4) & ~3;
        while (cons_cx < next && cons_cx < (int)CONS_COLS)
            vbe_putchar_cp(cons_cx++*FONT_W, cons_cy*FONT_H, ' ', cons_fg, cons_bg);
    } else {
        vbe_putchar_cp(cons_cx*FONT_W, cons_cy*FONT_H, cp, cons_fg, cons_bg);
        if (++cons_cx >= (int)CONS_COLS) {
            cons_cx = 0;
            if (++cons_cy >= (int)CONS_ROWS) vbe_console_scroll();
        }
    }
    draw_cursor(1);
}


/* Exp terminal capture hook.  The declaration remains weak for text-only
 * builds; when Exp is linked, its strong implementation receives all bytes. */
extern void exp_capture_char(char c) __attribute__((weak));
extern int exp_is_active(void) __attribute__((weak));

void vbe_console_putchar(char c) {
    if (!vbe.active) return;
    if (exp_is_active && exp_is_active()) {
        if (exp_capture_char) exp_capture_char(c);
        return;
    }
    uint8_t b = (uint8_t)c;
    if (utf8_need > 0) {
        if ((b & 0xC0) == 0x80) {
            utf8_buf[utf8_cnt++] = b;
            if (utf8_cnt == utf8_need) {
                /* Decode */
                const uint8_t *p = utf8_buf;
                uint32_t cp;
                if (utf8_need == 2) cp = ((utf8_buf[0]&0x1F)<<6)|(utf8_buf[1]&0x3F);
                else if (utf8_need == 3) cp = ((utf8_buf[0]&0x0F)<<12)|((utf8_buf[1]&0x3F)<<6)|(utf8_buf[2]&0x3F);
                else cp = '?'; (void)p;
                utf8_cnt = utf8_need = 0;
                vbe_con_emit_cp(cp);
            }
            return;
        } else { utf8_cnt = utf8_need = 0; }
    }
    if ((b & 0x80) == 0) {
        vbe_con_emit_cp((uint32_t)b);
    } else if ((b & 0xE0) == 0xC0) {
        utf8_buf[0]=b; utf8_cnt=1; utf8_need=2;
    } else if ((b & 0xF0) == 0xE0) {
        utf8_buf[0]=b; utf8_cnt=1; utf8_need=3;
    } else if ((b & 0xF8) == 0xF0) {
        utf8_buf[0]=b; utf8_cnt=1; utf8_need=4;
    } else {
        vbe_con_emit_cp('?');
    }
}

void vbe_console_write(const char *s) {
    while (*s) vbe_console_putchar(*s++);
}

void vbe_console_clear(void) {
    vbe_clear(cons_bg);
    cons_cx = cons_cy = 0;
}

void vbe_console_get_cursor(int *row, int *col) {
    if (row) *row = cons_cy;
    if (col) *col = cons_cx;
}

void vbe_console_set_cursor(int row, int col) {
    if (!vbe.active) return;
    int rows = (int)CONS_ROWS, cols = (int)CONS_COLS;
    if (rows < 1 || cols < 1) return;
    if (row < 0) row = 0;
    if (row >= rows) row = rows - 1;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;
    draw_cursor(0);
    cons_cy = row;
    cons_cx = col;
    draw_cursor(1);
}

void vbe_console_erase_eol(void) {
    if (!vbe.active) return;
    draw_cursor(0);
    for (int col = cons_cx; col < (int)CONS_COLS; col++)
        vbe_putchar_cp(col * FONT_W, cons_cy * FONT_H, ' ', cons_fg, cons_bg);
    draw_cursor(1);
}

/* ═══════════════════════════════════════════════════════════════
 *  Boot splash / logo
 * ═══════════════════════════════════════════════════════════════ */
void vbe_draw_logo(void) {
    if (!vbe.active) return;

    int cx = (int)vbe.width  / 2;
    int cy = (int)vbe.height / 2;

    /* Dark background gradient-ish */
    for (int y = 0; y < (int)vbe.height; y++) {
        uint8_t shade = (uint8_t)(10 + y * 6 / (int)vbe.height);
        vbe_draw_hline(0, y, (int)vbe.width, RGB(shade, shade/2, 0));
    }

    /* Centered box */
    int bw = 500, bh = 160;
    int bx = cx - bw/2, by = cy - bh/2;
    vbe_fill_rect(bx, by, bw, bh, RGB(0x12, 0x0A, 0x00));
    vbe_draw_rect(bx-2, by-2, bw+4, bh+4, RGB(0xFF, 0xA0, 0x00));

    /* Logo text */
    color32_t gold  = RGB(0xFF, 0xC0, 0x30);
    color32_t white = RGB(0xFF, 0xFF, 0xFF);
    color32_t grey  = RGB(0xAA, 0xAA, 0xAA);

    vbe_puts(bx + 40, by + 20,  " ____                          _  _   _  _  ___", gold, RGB(0x12,0x0A,0x00));
    vbe_puts(bx + 40, by + 38,  "/ ___|  __ _ _ __ __ _ _ __ _| || | | || ||  /", gold, RGB(0x12,0x0A,0x00));
    vbe_puts(bx + 40, by + 54,  "| |    / _` | '__/ _` | '_ (_  .  _)_  .|  /", gold, RGB(0x12,0x0A,0x00));
    vbe_puts(bx + 40, by + 70,  "| |___| (_| | | | (_| | | | || || |  | ||  \\", gold, RGB(0x12,0x0A,0x00));
    vbe_puts(bx + 40, by + 86,  " \\____|\\__,_|_|  \\__,_|_| |_||_||_|  |_||_|_|", gold, RGB(0x12,0x0A,0x00));

    vbe_puts(bx + 140, by + 110, "OS v3.0  —  x86 Protected Mode", white, RGB(0x12,0x0A,0x00));
    vbe_puts(bx + 130, by + 126, "GDT/IDT/VBE/VFS/ELF/Sched/Net", grey,  RGB(0x12,0x0A,0x00));

    /* Color bar */
    static const color32_t bar[] = {
        0xFF0000,0xFF7700,0xFFFF00,0x00FF00,
        0x00FFFF,0x0000FF,0xFF00FF,0xFFFFFF
    };
    for (int i = 0; i < 8; i++)
        vbe_fill_rect(bx + 40 + i*52, by + bh - 18, 48, 12, bar[i]);
}
