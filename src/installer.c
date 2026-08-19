#include "installer.h"
#include "vbe.h"
#include "ttf.h"
#include "keyboard.h"
#include "mouse.h"
#include "disk.h"
#include "catfs.h"
#include "catfs_vfs.h"
#include "util.h"
#include "pit.h"
#include <stdint.h>

#define BG RGB(0xFA,0xFA,0xF7)
#define PANEL RGB(0xFF,0xFF,0xFC)
#define LINE RGB(0xC4,0xC4,0xBA)
#define TEXT RGB(0x1E,0x1E,0x1B)
#define SUB RGB(0x4F,0x4F,0x49)
#define ACCENT RGB(0x2F,0x57,0x6B)
#define OK RGB(0x3B,0x6D,0x3F)
#define WARN RGB(0x9E,0x35,0x35)
#define MUTED RGB(0xE7,0xE7,0xE0)

static void r(int x,int y,int w,int h,color32_t c){vbe_fill_rect(x,y,w,h,c);}
static void box(int x,int y,int w,int h,color32_t c){vbe_draw_rect(x,y,w,h,c);}
static void text(int x,int y,const char*s,color32_t fg,color32_t bg){ttf_render_string(x,y,s,fg,bg);}
static int inside(int px,int py,int x,int y,int w,int h){return px>=x&&py>=y&&px<x+w&&py<y+h;}
static void button(int x,int y,int w,const char*label,int active){r(x,y,w,30,active?ACCENT:MUTED);box(x,y,w,30,active?ACCENT:LINE);text(x+12,y+8,label,active?PANEL:TEXT,active?ACCENT:MUTED);}

static int first_drive(void){for(int i=0;i<DISK_MAX_DRIVES;i++)if(disk_drives[i].present)return i;return -1;}
static int next_drive(int cur,int dir){for(int n=1;n<=DISK_MAX_DRIVES;n++){int i=(cur+dir*n+DISK_MAX_DRIVES)%DISK_MAX_DRIVES;if(disk_drives[i].present)return i;}return cur;}
static int install_target(int drv){
    if(drv<0||drv>=DISK_MAX_DRIVES||!disk_drives[drv].present)return -1;
    if(catfs_vfs_is_mounted()&&catfs_vfs_unmount()<0)return -1;
    if(catfs_format(drv,"atmkoala-root")<0)return -1;
    if(catfs_path_mkdir("/home")<0||catfs_path_mkdir("/syls")<0||catfs_path_mkdir("/syls/bin")<0||
       catfs_path_mkdir("/uiu")<0||catfs_path_mkdir("/uiu/etc")<0||catfs_path_mkdir("/data")<0||catfs_path_mkdir("/tmp")<0)return -1;
    if(catfs_sync()<0)return -1;
    return catfs_vfs_mount("/data");
}

static void frame(int step,int target,const char *status){
    int w=(int)vbe.width,h=(int)vbe.height; int px=(w-620)/2,py=(h-410)/2;
    r(0,0,w,h,BG); r(px,py,620,410,PANEL); box(px,py,620,410,LINE);
    r(px,py,620,48,ACCENT); text(px+22,py+15,"ATMKoala Disk Installer",PANEL,ACCENT);
    text(px+22,py+70,"Standalone installer boot mode",TEXT,PANEL);
    text(px+22,py+91,"This mode is intentionally unavailable from the normal desktop.",SUB,PANEL);
    for(int i=0;i<3;i++){int c=i<step?OK:(i==step?ACCENT:LINE);r(px+24+i*188,py+122,170,4,c);}
    if(step==0){
        text(px+22,py+154,"Welcome",TEXT,PANEL);
        text(px+22,py+182,"The installer formats exactly one selected ATA disk as CatFS.",SUB,PANEL);
        text(px+22,py+202,"No partition table, bootloader, Btrfs migration, or file recovery is performed.",WARN,PANEL);
        text(px+22,py+232,"Press Enter or click Continue to select the target disk.",SUB,PANEL);
    }else if(step==1){
        text(px+22,py+154,"Select target disk",TEXT,PANEL);
        if(target<0) text(px+22,py+190,"No ATA disks were detected. Shut down and attach a writable disk.",WARN,PANEL);
        else {char b[96];kstrcpy(b,"Selected: hd");b[12]=(char)('a'+target);b[13]=0;kstrcat(b," — ");kstrcat(b,disk_drives[target].model);text(px+22,py+190,b,TEXT,PANEL);char s[48];kstrcpy(s,"Capacity: ");char n[16];kuitoa(disk_drives[target].sectors/2048u,n,10);kstrcat(s,n);kstrcat(s," MiB");text(px+22,py+214,s,SUB,PANEL);text(px+22,py+246,"Use Left/Right or click a disk row, then Continue.",SUB,PANEL);}
    }else if(step==2){
        text(px+22,py+154,"Confirm destructive operation",WARN,PANEL);
        char b[48];kstrcpy(b,"All data on hd");b[15]=(char)('a'+target);b[16]=0;kstrcat(b," will be erased.");text(px+22,py+187,b,WARN,PANEL);
        text(px+22,py+215,"Click Install or press I to format and create the ATMKoala CatFS layout.",SUB,PANEL);
    }else if(step==3){text(px+22,py+154,"Installing…",TEXT,PANEL);text(px+22,py+188,status,SUB,PANEL);
    }else {text(px+22,py+154,status,TEXT,PANEL);text(px+22,py+188,"The CatFS target is mounted at /data for this live installer session.",SUB,PANEL);text(px+22,py+220,"Press Esc to return to the shell, then reboot to leave installer mode.",SUB,PANEL);}
    if(step<3){button(px+22,py+355,120,"Cancel",0);button(px+470,py+355,126,step==2?"Install":"Continue",1);} else if(step==4)button(px+470,py+355,126,"Finish",1);
    vbe_present();
}

void installer_run(void){
    int step=0,target=first_drive(),last_buttons=0;const char *status="";
    int buffered=(vbe_double_buffer_enable()==0);
    for(;;){
        frame(step,target,status);
        int k=keyboard_poll();const mouse_state_t*m=mouse_state();int click=m&&m->available&&(m->buttons&1)&&!(last_buttons&1);if(m)last_buttons=m->buttons;
        if(k==KEY_ESC){if(buffered)vbe_double_buffer_disable();return;}
        if(step==0&&(k=='\n'||k=='\r'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30)))){step=1;continue;}
        if(step==1){if(k==KEY_LEFT||k==KEY_UP)target=next_drive(target,-1);else if(k==KEY_RIGHT||k==KEY_DOWN)target=next_drive(target,1);else if((k=='\n'||k=='\r'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30)))&&target>=0)step=2;continue;}
        if(step==2){if(k=='i'||k=='I'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30))){step=3;status="Formatting selected target and writing CatFS layout…";frame(step,target,status);step=install_target(target)==0?4:5;status=step==4?"Installation completed successfully.":"Installation failed; no normal desktop was started.";continue;} if(k==KEY_LEFT||k==KEY_RIGHT)step=1;continue;}
        if(step==4||step==5){if(k=='\n'||k=='\r'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30))){if(buffered)vbe_double_buffer_disable();return;}}
        __asm__ volatile("pause");
    }
}
