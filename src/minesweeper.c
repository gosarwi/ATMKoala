#include "gamesdk.h"
#include "keyboard.h"
#include "util.h"
#include <stdint.h>

#define MW_W 10
#define MW_H 10
#define MW_MINES 14

static uint8_t mine[MW_H][MW_W], open_[MW_H][MW_W], flag_[MW_H][MW_W];
static uint32_t mw_rng=0x4D595DF4u;
static uint32_t rnd(void){ mw_rng=mw_rng*1664525u+1013904223u; return mw_rng; }
static int inside(int x,int y){ return x>=0&&x<MW_W&&y>=0&&y<MW_H; }
static int around(int x,int y){
    int n=0; for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)
        if((dx||dy)&&inside(x+dx,y+dy)&&mine[y+dy][x+dx])n++;
    return n;
}
static void reveal(int x,int y){
    if(!inside(x,y)||open_[y][x]||flag_[y][x])return;
    open_[y][x]=1;
    if(!mine[y][x]&&!around(x,y)) for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) if(dx||dy) reveal(x+dx,y+dy);
}
static void draw(int cx,int cy){
    game_clear();
    game_puts(3,1,"MINESWEEPER",GC_WHITE);
    game_puts(3,2,"Arrows: move   Enter: reveal   Space: flag   Q: quit",GC_DGREY);
    for(int y=0;y<MW_H;y++) for(int x=0;x<MW_W;x++){
        char c='#'; int col=GC_LGREY;
        if(open_[y][x]){
            int n=around(x,y); c=mine[y][x]?'*':(n?(char)('0'+n):'.');
            col=mine[y][x]?GC_LRED:(n?GC_LCYAN:GC_DGREY);
        } else if(flag_[y][x]) { c='F'; col=GC_YELLOW; }
        if(x==cx&&y==cy){ game_fill(3+x*2,5+y,2,1,' ',GC_WHITE); game_putc(3+x*2,5+y,c,GC_BLACK); }
        else game_putc(3+x*2,5+y,c,col);
    }
}
void game_minesweeper(void){
    kmemset(mine,0,sizeof(mine)); kmemset(open_,0,sizeof(open_)); kmemset(flag_,0,sizeof(flag_));
    int placed=0; while(placed<MW_MINES){int x=(int)(rnd()%MW_W),y=(int)(rnd()%MW_H);if(!mine[y][x]){mine[y][x]=1;placed++;}}
    int x=0,y=0,dead=0;
    while(1){
        draw(x,y);
        int k=game_key_wait(); if(game_key_quit(k))break;
        if(k==KEY_LEFT&&x)x--; else if(k==KEY_RIGHT&&x<MW_W-1)x++; else if(k==KEY_UP&&y)y--; else if(k==KEY_DOWN&&y<MW_H-1)y++;
        else if(k==' ')flag_[y][x]^=1;
        else if(k=='\n'||k=='\r'){reveal(x,y);if(mine[y][x]){dead=1;break;}}
        int left=0;for(int yy=0;yy<MW_H;yy++)for(int xx=0;xx<MW_W;xx++)if(!mine[yy][xx]&&!open_[yy][xx])left++;
        if(!left){game_screen_win(1000);break;}
    }
    if(dead){for(int yy=0;yy<MW_H;yy++)for(int xx=0;xx<MW_W;xx++)if(mine[yy][xx])open_[yy][xx]=1;draw(x,y);game_screen_gameover(0);}
    game_exit();
}
