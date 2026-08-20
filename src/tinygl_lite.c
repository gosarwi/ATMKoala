#include "tinygl_lite.h"
#include "kmalloc.h"
#include "util.h"
#include <stdint.h>

static int edge(const tgl_vertex_t *a,const tgl_vertex_t *b,int x,int y){
    return (x-a->x)*(b->y-a->y)-(y-a->y)*(b->x-a->x);
}
static int min3(int a,int b,int c){int r=a;if(b<r)r=b;if(c<r)r=c;return r;}
static int max3(int a,int b,int c){int r=a;if(b>r)r=b;if(c>r)r=c;return r;}
/* Integer sine approximation with a 1024-unit turn and amplitude 256.
 * The renderer remains usable under ATMKoala's -msoft-float toolchain. */
static int wave(int a){
    a%=1024;if(a<0)a+=1024;
    if(a<256)return a;
    if(a<512)return 512-a;
    if(a<768)return -(a-512);
    return -(1024-a);
}
static int tgl_project(int x,int y,int z,int cx,int cy,int scale,tgl_vertex_t *out,uint32_t color){
    if(z<16)return -1;
    out->x=cx+(x*scale)/z;out->y=cy-(y*scale)/z;out->z=z;out->color=color;return 0;
}

int tgl_init(tgl_context_t *ctx,int width,int height){
    if(!ctx||width<1||height<1)return -1;uint32_t need=(uint32_t)width*(uint32_t)height;
    if(!ctx->zbuffer||ctx->zcount<need){uint16_t *z=(uint16_t*)kmalloc(need*sizeof(uint16_t));if(!z)return -1;ctx->zbuffer=z;ctx->zcount=need;}
    ctx->ready=1;return 0;
}
void tgl_reset(tgl_context_t *ctx){if(ctx)ctx->ready=0;}
void tgl_begin(tgl_context_t *ctx,int x,int y,int width,int height,uint32_t clear_color){
    if(!ctx||!ctx->ready)return;if(x<0){width+=x;x=0;}if(y<0){height+=y;y=0;}if(x+width>(int)vbe.width)width=(int)vbe.width-x;if(y+height>(int)vbe.height)height=(int)vbe.height-y;
    if(width<1||height<1)return;ctx->x=x;ctx->y=y;ctx->width=width;ctx->height=height;uint32_t used=(uint32_t)width*(uint32_t)height;if(used>ctx->zcount)return;
    vbe_fill_rect(x,y,width,height,clear_color);for(uint32_t i=0;i<used;i++)ctx->zbuffer[i]=0xffff;ctx->frame++;
}
void tgl_triangle(tgl_context_t *ctx,const tgl_vertex_t *a,const tgl_vertex_t *b,const tgl_vertex_t *c){
    if(!ctx||!ctx->ready||!a||!b||!c)return;int area=edge(a,b,c->x,c->y);if(!area)return;int sign=area<0?-1:1;
    int x0=min3(a->x,b->x,c->x),x1=max3(a->x,b->x,c->x),y0=min3(a->y,b->y,c->y),y1=max3(a->y,b->y,c->y);
    if(x0<ctx->x)x0=ctx->x;if(y0<ctx->y)y0=ctx->y;if(x1>=ctx->x+ctx->width)x1=ctx->x+ctx->width-1;if(y1>=ctx->y+ctx->height)y1=ctx->y+ctx->height-1;
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        int w0=edge(b,c,x,y)*sign,w1=edge(c,a,x,y)*sign,w2=edge(a,b,x,y)*sign;if(w0<0||w1<0||w2<0)continue;
        int64_t zn=(int64_t)w0*a->z+(int64_t)w1*b->z+(int64_t)w2*c->z;int z=(int)(zn/(area*sign));if(z<1)z=1;if(z>65535)z=65535;
        uint32_t idx=(uint32_t)(y-ctx->y)*(uint32_t)ctx->width+(uint32_t)(x-ctx->x);if((uint16_t)z>=ctx->zbuffer[idx])continue;ctx->zbuffer[idx]=(uint16_t)z;
        /* Flat-face colour is intentional in this first compact TinyGL subset. */
        vbe_putpixel(x,y,a->color);
    }
}
void tgl_draw_cube(tgl_context_t *ctx,int x,int y,int width,int height,int angle){
    if(!ctx)return;if(tgl_init(ctx,width,height)<0)return;tgl_begin(ctx,x,y,width,height,RGB(0x10,0x10,0x10));
    int s=wave(angle),c=wave(angle+256),s2=wave(angle/2+96),c2=wave(angle/2+352);int raw[8][3]={{-90,-90,-90},{90,-90,-90},{90,90,-90},{-90,90,-90},{-90,-90,90},{90,-90,90},{90,90,90},{-90,90,90}};
    tgl_vertex_t v[8];int cx=x+width/2,cy=y+height/2;
    for(int i=0;i<8;i++){int rx=(raw[i][0]*c+raw[i][2]*s)/256,rz=(-raw[i][0]*s+raw[i][2]*c)/256;int ry=(raw[i][1]*c2-rz*s2)/256;rz=(raw[i][1]*s2+rz*c2)/256;tgl_project(rx,ry,rz+360,cx,cy,250,&v[i],RGB(0xf0,0xf0,0xf0));}
    static const uint8_t faces[6][4]={{0,1,2,3},{1,5,6,2},{5,4,7,6},{4,0,3,7},{3,2,6,7},{4,5,1,0}};
    static const uint32_t cols[6]={0xffb8b8b8,0xff969696,0xffd0d0d0,0xff686868,0xffececec,0xff808080};
    for(int f=0;f<6;f++){tgl_vertex_t a=v[faces[f][0]],b=v[faces[f][1]],c3=v[faces[f][2]],d=v[faces[f][3]];a.color=b.color=c3.color=d.color=cols[f];tgl_triangle(ctx,&a,&b,&c3);tgl_triangle(ctx,&a,&c3,&d);}
    vbe_draw_rect(x,y,width,height,RGB(0x3b,0x3b,0x3b));
}
const char *tgl_renderer_name(void){return "TinyGL-Lite fixed-point software renderer";}

static void tgl_draw_one_gear(tgl_context_t *ctx,int cx,int cy,int radius,int angle,uint32_t color,int z,uint32_t background){
    tgl_vertex_t center={cx,cy,z,color},prev,cur;
    for(int i=0;i<=16;i++){
        int a=angle+i*64;
        int r=(i&3)<2?radius:(radius*3)/4;
        cur.x=cx+(wave(a+256)*r)/256;cur.y=cy-(wave(a)*r)/256;cur.z=z;cur.color=color;
        if(i){tgl_triangle(ctx,&center,&prev,&cur);}prev=cur;
    }
    /* A nearer background-coloured hub gives the gear a real hole while the
     * surrounding teeth retain their depth-buffered software shading. */
    center.z=z-1;center.color=background;
    int hole=radius/3;
    for(int i=0;i<=12;i++){
        int a=angle+i*(1024/12);
        cur.x=cx+(wave(a+256)*hole)/256;cur.y=cy-(wave(a)*hole)/256;cur.z=z-1;cur.color=background;
        if(i){tgl_triangle(ctx,&center,&prev,&cur);}prev=cur;
    }
}

void tgl_draw_gears(tgl_context_t *ctx,int x,int y,int width,int height,int angle){
    const uint32_t bg=RGB(0x10,0x10,0x10);
    if(!ctx||width<96||height<72)return;
    if(tgl_init(ctx,width,height)<0)return;
    tgl_begin(ctx,x,y,width,height,bg);
    int r1=(width<height?width:height)/5;if(r1<18)r1=18;
    int r2=(r1*3)/4,r3=(r1*2)/3;
    tgl_draw_one_gear(ctx,x+width/3,y+height/2,r1,angle,RGB(0xC9,0xEF,0x68),440,bg);
    tgl_draw_one_gear(ctx,x+width/2+r1/2,y+height/2-r1/2,r2,-angle*2,RGB(0x79,0xC9,0xFF),360,bg);
    tgl_draw_one_gear(ctx,x+width/2+r1/2,y+height/2+r1,r3,angle*3+160,RGB(0xFF,0xBF,0x69),280,bg);
    vbe_draw_rect(x,y,width,height,RGB(0x3B,0x3B,0x3B));
}
