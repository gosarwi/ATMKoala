#include "installer.h"
#include "vbe.h"
#include "ttf.h"
#include "keyboard.h"
#include "mouse.h"
#include "disk.h"
#include "partmgr.h"
#include "catfs.h"
#include "catfs_vfs.h"
#include "util.h"
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

/* MBR-compatible, 1 MiB alignment. The installer creates exactly one primary
 * CatFS partition and deliberately does not claim to create an EFI system
 * partition or install a bootloader. */
#define INSTALL_ALIGN_LBA 2048u
#define INSTALL_MIN_SECTORS 2048u
#define INSTALL_SIZE_STEP_SECTORS (64u*2048u)

static void r(int x,int y,int w,int h,color32_t c){vbe_fill_rect(x,y,w,h,c);}
static void box(int x,int y,int w,int h,color32_t c){vbe_draw_rect(x,y,w,h,c);}
static void text(int x,int y,const char*s,color32_t fg,color32_t bg){ttf_render_string(x,y,s,fg,bg);}
static int inside(int px,int py,int x,int y,int w,int h){return px>=x&&py>=y&&px<x+w&&py<y+h;}
static void button(int x,int y,int w,const char*label,int active){r(x,y,w,30,active?ACCENT:MUTED);box(x,y,w,30,active?ACCENT:LINE);text(x+12,y+8,label,active?PANEL:TEXT,active?ACCENT:MUTED);}

static int first_drive(void){for(int i=0;i<DISK_MAX_DRIVES;i++)if(disk_drives[i].present)return i;return -1;}
static int next_drive(int cur,int dir){for(int n=1;n<=DISK_MAX_DRIVES;n++){int i=(cur+dir*n+DISK_MAX_DRIVES)%DISK_MAX_DRIVES;if(disk_drives[i].present)return i;}return cur;}
static uint32_t drive_sectors(int drv){return drv>=0&&drv<DISK_MAX_DRIVES&&disk_drives[drv].present?disk_drives[drv].sectors:0;}
static void layout_default(int drv,uint32_t *start,uint32_t *sectors){
    uint32_t total=drive_sectors(drv);
    *start=INSTALL_ALIGN_LBA;
    *sectors=total>*start?total-*start:0;
    *sectors-=*sectors%INSTALL_ALIGN_LBA;
}
static void installer_preflight(int drv,char *out,size_t cap){
    mbr_table_t table;
    if(!out||!cap)return;
    if(drv<0||drv>=DISK_MAX_DRIVES||!disk_drives[drv].present){ksnprintf(out,cap,"No ATA target selected.");return;}
    if(mbr_read(drv,&table)<0||mbr_validate_drive(drv,&table)<0){ksnprintf(out,cap,"No valid MBR table: installer will create one CatFS partition.");return;}
    for(int i=0;i<PART_MAX_ENTRIES;i++){
        mbr_entry_t *e=&table.entries[i];
        if(!e->type||!e->sector_count)continue;
        ksnprintf(out,cap,"Existing hda%d: %s / %s (%u MiB) will be replaced.",i+1,part_type_name(e->type),mbr_probe_filesystem(drv,e),e->sector_count/2048u);
        return;
    }
    ksnprintf(out,cap,"Valid empty MBR table: installer will create one CatFS partition.");
}
static void layout_clamp(int drv,uint32_t *start,uint32_t *sectors){
    uint32_t total=drive_sectors(drv);
    if(total<=INSTALL_ALIGN_LBA+INSTALL_MIN_SECTORS){*start=0;*sectors=0;return;}
    if(*start<INSTALL_ALIGN_LBA)*start=INSTALL_ALIGN_LBA;
    *start-=*start%INSTALL_ALIGN_LBA;
    if(*start>=total-INSTALL_MIN_SECTORS)*start=total-INSTALL_MIN_SECTORS;
    *start-=*start%INSTALL_ALIGN_LBA;
    uint32_t max=total-*start;
    if(*sectors>max)*sectors=max;
    *sectors-=*sectors%INSTALL_ALIGN_LBA;
    if(*sectors<INSTALL_MIN_SECTORS)*sectors=INSTALL_MIN_SECTORS;
    if(*sectors>max){*start=INSTALL_ALIGN_LBA;*sectors=total-*start;*sectors-=*sectors%INSTALL_ALIGN_LBA;}
}
static int install_target_partition(int drv,uint32_t start,uint32_t sectors){
    mbr_table_t table;
    if(drv<0||drv>=DISK_MAX_DRIVES||!disk_drives[drv].present||!start||sectors<INSTALL_MIN_SECTORS)return -1;
    if((uint64_t)start+(uint64_t)sectors>(uint64_t)drive_sectors(drv))return -1;
    if(catfs_vfs_is_mounted()&&catfs_vfs_unmount()<0)return -1;
    mbr_init_empty(&table);
    if(mbr_add_partition(&table,PART_TYPE_CATFS,start,sectors,0)<0||mbr_validate_drive(drv,&table)<0)return -1;
    /* The dedicated installer is explicitly destructive. It creates a clean
     * MBR table, not a mixed layout, after the user types ERASE. */
    if(mbr_write(drv,&table,1)<0)return -1;
    if(catfs_format_at(drv,start,"atmkoala-root")<0)return -1;
    if(catfs_path_mkdir("/home")<0||catfs_path_mkdir("/syls")<0||catfs_path_mkdir("/syls/bin")<0||
       catfs_path_mkdir("/uiu")<0||catfs_path_mkdir("/uiu/etc")<0||catfs_path_mkdir("/data")<0||catfs_path_mkdir("/tmp")<0)return -1;
    if(catfs_sync()<0)return -1;
    return catfs_vfs_mount("/data");
}

static void frame(int step,int target,uint32_t start,uint32_t sectors,int erase_count,const char *status,const char *preflight){
    int w=(int)vbe.width,h=(int)vbe.height; int px=(w-620)/2,py=(h-410)/2;
    r(0,0,w,h,BG); r(px,py,620,410,PANEL); box(px,py,620,410,LINE);
    r(px,py,620,48,ACCENT); text(px+22,py+15,"ATMKoala Disk Installer",PANEL,ACCENT);
    text(px+22,py+70,"Standalone installer boot mode",TEXT,PANEL);
    text(px+22,py+91,"Creates one aligned MBR CatFS partition; bootloader installation is not included.",SUB,PANEL);
    for(int i=0;i<4;i++){int c=i<step?OK:(i==step?ACCENT:LINE);r(px+24+i*141,py+122,125,4,c);}
    if(step==0){
        text(px+22,py+154,"Welcome",TEXT,PANEL);
        text(px+22,py+182,"The installer writes only after an explicit typed ERASE confirmation.",SUB,PANEL);
        text(px+22,py+206,"Existing MBR primary entries on the selected target will be replaced.",WARN,PANEL);
        text(px+22,py+236,"Press Enter or click Continue to select an ATA target disk.",SUB,PANEL);
    }else if(step==1){
        text(px+22,py+154,"Select target disk",TEXT,PANEL);
        if(target<0) text(px+22,py+190,"No ATA PIO disk was detected. Attach an IDE/ATA writable target, then reboot.",WARN,PANEL);
        else {
            char b[128],s[96];ksnprintf(b,sizeof(b),"Selected: hd%c - %s",'a'+target,disk_drives[target].model[0]?disk_drives[target].model:"ATA PIO disk");
            ksnprintf(s,sizeof(s),"Capacity: %u MiB (%u sectors)",disk_capacity_mib(target),drive_sectors(target));
            text(px+22,py+190,b,TEXT,PANEL);text(px+22,py+214,s,SUB,PANEL);
            text(px+22,py+246,preflight?preflight:"",WARN,PANEL);
            text(px+22,py+268,"Use Left/Right or click Continue; disk discovery covers ATA PIO in this installer.",SUB,PANEL);
        }
    }else if(step==2){
        char l1[128],l2[128];
        text(px+22,py+154,"Plan one aligned CatFS partition",TEXT,PANEL);
        ksnprintf(l1,sizeof(l1),"Target: hda%d    Start LBA: %u (%u MiB)",target+1,start,start/2048u);
        ksnprintf(l2,sizeof(l2),"Size: %u MiB (%u sectors)    End LBA: %u",sectors/2048u,sectors,start+sectors-1u);
        text(px+22,py+185,l1,TEXT,PANEL);text(px+22,py+210,l2,TEXT,PANEL);
        text(px+22,py+246,"Left/Right move start by 1 MiB. Up/Down change size by 64 MiB.",SUB,PANEL);
        text(px+22,py+268,"Home restores the full usable disk layout. Press Enter to review destructive changes.",SUB,PANEL);
    }else if(step==3){
        char b[128],typed[32];
        text(px+22,py+154,"Confirm destructive MBR rewrite",WARN,PANEL);
        ksnprintf(b,sizeof(b),"hd%c1: CatFS, LBA %u..%u (%u MiB)",'a'+target,start,start+sectors-1u,sectors/2048u);
        text(px+22,py+186,b,WARN,PANEL);
        text(px+22,py+214,"The current primary MBR table will be replaced. No bootloader is installed.",WARN,PANEL);
        ksnprintf(typed,sizeof(typed),"Type ERASE to unlock installation: %d/5",erase_count);
        text(px+22,py+250,typed,erase_count==5?OK:SUB,PANEL);
        text(px+22,py+274,erase_count==5?"Press Enter to write the table and format CatFS.":"Keyboard confirmation is required; mouse click cannot start the erase.",SUB,PANEL);
    }else if(step==4){text(px+22,py+154,"Installing…",TEXT,PANEL);text(px+22,py+188,status,SUB,PANEL);
    }else {text(px+22,py+154,status,step==5?OK:WARN,PANEL);text(px+22,py+188,"The CatFS partition is mounted at /data for this live installer session.",SUB,PANEL);text(px+22,py+220,"Press Esc or Finish to return to the shell, then reboot to leave installer mode.",SUB,PANEL);}
    if(step<3){button(px+22,py+355,120,"Cancel",0);button(px+470,py+355,126,"Continue",target>=0);} else if(step==3)button(px+470,py+355,126,"Install",erase_count==5); else if(step>=5)button(px+470,py+355,126,"Finish",1);
    vbe_present();
}

void installer_run(void){
    int step=0,target=first_drive(),last_buttons=0,erase_count=0;uint32_t start=0,sectors=0;const char *status="";char preflight[144];
    int buffered=(vbe_double_buffer_enable()==0);
    if(target>=0)layout_default(target,&start,&sectors);
    installer_preflight(target,preflight,sizeof(preflight));
    for(;;){
        frame(step,target,start,sectors,erase_count,status,preflight);
        int k=keyboard_poll();const mouse_state_t*m=mouse_state();int click=m&&m->available&&(m->buttons&1)&&!(last_buttons&1);if(m)last_buttons=m->buttons;
        if(k==KEY_ESC){if(buffered)vbe_double_buffer_disable();return;}
        if(step==0&&(k=='\n'||k=='\r'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30)))){step=1;continue;}
        if(step==1){
            if(k==KEY_LEFT||k==KEY_UP){target=next_drive(target,-1);layout_default(target,&start,&sectors);installer_preflight(target,preflight,sizeof(preflight));}
            else if(k==KEY_RIGHT||k==KEY_DOWN){target=next_drive(target,1);layout_default(target,&start,&sectors);installer_preflight(target,preflight,sizeof(preflight));}
            else if((k=='\n'||k=='\r'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30)))&&target>=0){layout_default(target,&start,&sectors);step=2;}
            continue;
        }
        if(step==2){
            if(k==KEY_LEFT&&start>INSTALL_ALIGN_LBA){start-=INSTALL_ALIGN_LBA;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_RIGHT){start+=INSTALL_ALIGN_LBA;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_UP){if(sectors<=drive_sectors(target)-start-INSTALL_SIZE_STEP_SECTORS)sectors+=INSTALL_SIZE_STEP_SECTORS;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_DOWN){if(sectors>INSTALL_MIN_SECTORS+INSTALL_SIZE_STEP_SECTORS)sectors-=INSTALL_SIZE_STEP_SECTORS;else sectors=INSTALL_MIN_SECTORS;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_HOME){layout_default(target,&start,&sectors);}
            else if((k=='\n'||k=='\r'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30)))&&sectors>=INSTALL_MIN_SECTORS){erase_count=0;step=3;}
            continue;
        }
        if(step==3){
            static const char confirm[]="ERASE";
            if(k==KEY_BACKSPACE&&erase_count>0)erase_count--;
            else if(k>0&&k<0x80&&erase_count<5){char ch=(char)k;if(ch>='a'&&ch<='z')ch-=32;if(ch==confirm[erase_count])erase_count++;else erase_count=(ch==confirm[0])?1:0;}
            if((k=='\n'||k=='\r')&&erase_count==5){step=4;status="Writing MBR and formatting the selected CatFS partition…";frame(step,target,start,sectors,erase_count,status,preflight);step=install_target_partition(target,start,sectors)==0?5:6;status=step==5?"Installation completed successfully.":"Installation failed after the destructive confirmation.";}
            continue;
        }
        if(step==5||step==6){if(k=='\n'||k=='\r'||(click&&inside(m->x,m->y,(int)vbe.width/2+160,(int)vbe.height/2+150,126,30))){if(buffered)vbe_double_buffer_disable();return;}}
        __asm__ volatile("pause");
    }
}
