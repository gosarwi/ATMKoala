#include "gui_demo.h"
#include "exp.h"
#include "util.h"

static int demo_clicks=0;
static char demo_status[48]="Ready";

static void demo_draw(exp_gui_context_t *ctx){
    exp_gui_fill(ctx,0,0,ctx->width,ctx->height,ctx->bg);
    exp_gui_fill(ctx,12,14,ctx->width-24,44,RGB(0x31,0x66,0x87));
    exp_gui_frame(ctx,12,14,ctx->width-24,44,RGB(0xA0,0xC7,0xDB));
    exp_gui_text(ctx,26,27,"External GUI ABI v1",RGB(0xFF,0xFF,0xFF),RGB(0x31,0x66,0x87));
    exp_gui_fill(ctx,22,82,72,72,RGB(0xD8,0x8B,0x33));
    exp_gui_frame(ctx,22,82,72,72,RGB(0x78,0x42,0x18));
    exp_gui_fill(ctx,36,96,44,12,RGB(0xFF,0xE8,0xA7));
    exp_gui_fill(ctx,36,118,44,12,RGB(0xFF,0xE8,0xA7));
    exp_gui_text(ctx,112,88,"Third-party native client",ctx->fg,ctx->bg);
    exp_gui_text(ctx,112,110,"Click anywhere or press +",ctx->fg,ctx->bg);
    char b[32];kstrcpy(b,"Clicks: ");char n[12];kitoa(demo_clicks,n,10);kstrcat(b,n);
    exp_gui_text(ctx,112,132,b,ctx->fg,ctx->bg);
    exp_gui_fill(ctx,22,ctx->height-44,ctx->width-44,26,RGB(0xE8,0xE8,0xDF));
    exp_gui_frame(ctx,22,ctx->height-44,ctx->width-44,26,RGB(0x92,0x92,0x88));
    exp_gui_text(ctx,30,ctx->height-37,demo_status,RGB(0x20,0x20,0x20),RGB(0xE8,0xE8,0xDF));
}
static void demo_key(exp_gui_context_t *ctx,int key,int ctrl,int alt){
    (void)ctx;(void)ctrl;(void)alt;
    if(key=='+'||key=='='||key=='\n'){demo_clicks++;kstrcpy(demo_status,"Keyboard event accepted");}
    else if(key=='r'||key=='R'){demo_clicks=0;kstrcpy(demo_status,"Counter reset");}
}
static void demo_pointer(exp_gui_context_t *ctx,int x,int y,uint32_t buttons){
    (void)ctx;(void)x;(void)y;(void)buttons;demo_clicks++;kstrcpy(demo_status,"Pointer event accepted");
}
static void demo_open(exp_gui_context_t *ctx){(void)ctx;kstrcpy(demo_status,"Opened through Exp GUI ABI");}
static const exp_gui_app_t demo_app={
    EXP_GUI_ABI_MAJOR,EXP_GUI_ABI_MINOR,"org.atmkoala.sdk-demo","SDK GUI Demo","Development",
    440,260,RGB(0x31,0x66,0x87),NULL,demo_draw,demo_key,demo_pointer,demo_open,NULL
};
void gui_demo_register(void){(void)exp_gui_register(&demo_app);}
