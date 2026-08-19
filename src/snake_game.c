#include "gamesdk.h"
#include "keyboard.h"
#include "util.h"
#include <stdint.h>

#define SN_W 32
#define SN_H 16
#define SN_MAX 128

typedef struct { int x,y; } sn_pt_t;
static uint32_t sn_rng=0x51A9E3u;
static uint32_t sn_rand(void){sn_rng=sn_rng*1103515245u+12345u;return sn_rng;}
static int occupied(sn_pt_t *body,int n,int x,int y){for(int i=0;i<n;i++)if(body[i].x==x&&body[i].y==y)return 1;return 0;}
static void sn_draw(sn_pt_t *body,int n,int fx,int fy,int score){
    game_clear(); game_puts(3,1,"SNAKE",GC_WHITE);
    char sc[20]; ksnprintf(sc,sizeof(sc),"Score: %d   Arrows: move   Q: quit",score); game_puts(3,2,sc,GC_DGREY);
    game_box(2,4,SN_W+2,SN_H+2,GC_LGREY);
    game_putc(3+fx,5+fy,'*',GC_YELLOW);
    for(int i=n-1;i>=0;i--) game_putc(3+body[i].x,5+body[i].y,i?'+':'@',i?GC_LGREEN:GC_WHITE);
}
void game_snake_modern(void){
    sn_pt_t body[SN_MAX]; int n=4,dx=1,dy=0,fx=20,fy=8,score=0,dead=0;
    for(int i=0;i<n;i++){body[i].x=10-i;body[i].y=8;}
    while(1){
        int k=game_key_poll();
        if(game_key_quit(k))break;
        if(k==KEY_LEFT&&dx!=1){dx=-1;dy=0;} else if(k==KEY_RIGHT&&dx!=-1){dx=1;dy=0;}
        else if(k==KEY_UP&&dy!=1){dx=0;dy=-1;} else if(k==KEY_DOWN&&dy!=-1){dx=0;dy=1;}
        int nx=body[0].x+dx,ny=body[0].y+dy;
        if(nx<0||nx>=SN_W||ny<0||ny>=SN_H||occupied(body,n,nx,ny)){dead=1;break;}
        int eat=(nx==fx&&ny==fy); if(eat&&n<SN_MAX)n++; for(int i=n-1;i>0;i--)body[i]=body[i-1];body[0].x=nx;body[0].y=ny;
        if(eat){score+=10; do{fx=(int)(sn_rand()%SN_W);fy=(int)(sn_rand()%SN_H);}while(occupied(body,n,fx,fy));game_beep(780,25);}
        sn_draw(body,n,fx,fy,score); game_wait_frame(8);
    }
    if(dead)game_screen_gameover(score); game_exit();
}
