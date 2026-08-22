#include "installer.h"
#include "vbe.h"
#include "ttf.h"
#include "keyboard.h"
#include "mouse.h"
#include "disk.h"
#include "partmgr.h"
#include "catfs.h"
#include "catfs_vfs.h"
#include "config.h"
#include "users.h"
#include "vfs.h"
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

/* MBR-compatible, 1 MiB alignment. The installer creates exactly one primary
 * CatFS partition and deliberately does not claim to create an EFI system
 * partition or install a bootloader. */
#define INSTALL_ALIGN_LBA 2048u
#define INSTALL_MIN_SECTORS 2048u
#define INSTALL_SIZE_STEP_SECTORS (64u*2048u)
#define INSTALL_PANEL_W 620
#define INSTALL_PANEL_H 410
#define INSTALL_PRIMARY_X 470
#define INSTALL_PRIMARY_Y 355
#define INSTALL_PRIMARY_W 126
#define INSTALL_BUTTON_H 30

/* Presets cover the common global regions. The selected IANA-style identifier
 * is persisted, may later be changed with the timezone command, and is
 * converted from an UTC RTC by the shared civil-time layer. NTP synchronization
 * and a complete historic TZif database remain absent. */
static const char *const installer_timezones[]={
    "UTC","Europe/London","Europe/Dublin","Atlantic/Reykjavik","Europe/Lisbon","Europe/Madrid","Europe/Paris","Europe/Brussels","Europe/Amsterdam","Europe/Berlin","Europe/Copenhagen","Europe/Oslo","Europe/Stockholm","Europe/Rome","Europe/Vienna","Europe/Prague","Europe/Warsaw","Europe/Budapest","Europe/Athens","Europe/Bucharest","Europe/Helsinki","Europe/Kyiv","Europe/Riga","Europe/Vilnius","Europe/Moscow","Europe/Istanbul",
    "Africa/Casablanca","Africa/Algiers","Africa/Lagos","Africa/Cairo","Africa/Johannesburg","Africa/Nairobi","Africa/Addis_Ababa",
    "Asia/Jerusalem","Asia/Riyadh","Asia/Dubai","Asia/Tehran","Asia/Kabul","Asia/Karachi","Asia/Tashkent","Asia/Yekaterinburg","Asia/Omsk","Asia/Novosibirsk","Asia/Krasnoyarsk","Asia/Irkutsk","Asia/Yakutsk","Asia/Vladivostok","Asia/Magadan","Asia/Kamchatka","Asia/Kolkata","Asia/Kathmandu","Asia/Colombo","Asia/Dhaka","Asia/Yangon","Asia/Bangkok","Asia/Ho_Chi_Minh","Asia/Jakarta","Asia/Kuala_Lumpur","Asia/Singapore","Asia/Manila","Asia/Shanghai","Asia/Hong_Kong","Asia/Taipei","Asia/Ulaanbaatar","Asia/Seoul","Asia/Tokyo",
    "Australia/Perth","Australia/Darwin","Australia/Adelaide","Australia/Brisbane","Australia/Sydney","Australia/Hobart","Pacific/Port_Moresby","Pacific/Guam","Pacific/Fiji","Pacific/Auckland","Pacific/Honolulu","Pacific/Pago_Pago",
    "America/St_Johns","America/Halifax","America/Toronto","America/New_York","America/Detroit","America/Indiana/Indianapolis","America/Chicago","America/Winnipeg","America/Mexico_City","America/Guatemala","America/Costa_Rica","America/Panama","America/Bogota","America/Lima","America/Caracas","America/La_Paz","America/Santiago","America/Asuncion","America/Montevideo","America/Argentina/Buenos_Aires","America/Sao_Paulo","America/Phoenix","America/Denver","America/Edmonton","America/Los_Angeles","America/Vancouver","America/Anchorage"
};
#define INSTALL_TZ_COUNT ((int)(sizeof(installer_timezones)/sizeof(installer_timezones[0])))
#define INSTALL_LOG_CAP 2048u

static char installer_log[INSTALL_LOG_CAP];
static uint32_t installer_log_len;

static void installer_log_reset(void){installer_log_len=0;installer_log[0]=0;}
static void installer_log_add(const char *message){
    if(!message||!message[0])return;
    uint32_t n=(uint32_t)kstrlen(message);
    if(n+2>=INSTALL_LOG_CAP)return;
    if(installer_log_len+n+2>=INSTALL_LOG_CAP){
        uint32_t keep=INSTALL_LOG_CAP/2u;
        kmemmove(installer_log,installer_log+installer_log_len-keep,keep);
        installer_log_len=keep;
    }
    kmemcpy(installer_log+installer_log_len,message,n);installer_log_len+=n;installer_log[installer_log_len++]='\n';installer_log[installer_log_len]=0;
}
static void installer_log_save(void){
    if(!catfs_vfs_is_mounted()||!installer_log_len)return;
    (void)vfs_mkdir("/data/uiu",0755);(void)vfs_mkdir("/data/uiu/var",0755);(void)vfs_mkdir("/data/uiu/var/log",0755);
    int fd=vfs_open("/data/uiu/var/log/installer.log",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){(void)vfs_write(fd,(const uint8_t*)installer_log,installer_log_len);vfs_close(fd);}
}
/* The visible strip retains only the newest two complete records. The full
 * bounded log remains reachable with L and persists after CatFS is mounted. */
static void installer_log_line_copy(char *out,size_t cap,const char *line){
    size_t n=0;if(!out||!cap)return;if(!line){out[0]=0;return;}
    while(line[n]&&line[n]!='\n'&&n+1<cap){out[n]=line[n];n++;}out[n]=0;
}
static void installer_log_latest(char *previous,size_t previous_cap,char *latest,size_t latest_cap){
    const char *p=installer_log;
    if(previous&&previous_cap)previous[0]=0;if(latest&&latest_cap)latest[0]=0;
    while(*p){
        const char *line=p;while(*p&&*p!='\n')p++;
        if(line!=p){if(latest&&latest[0])installer_log_line_copy(previous,previous_cap,latest);installer_log_line_copy(latest,latest_cap,line);}
        if(*p=='\n')p++;
    }
}

static void r(int x,int y,int w,int h,color32_t c){vbe_fill_rect(x,y,w,h,c);}
static void box(int x,int y,int w,int h,color32_t c){vbe_draw_rect(x,y,w,h,c);}
static void text(int x,int y,const char*s,color32_t fg,color32_t bg){ttf_render_string(x,y,s,fg,bg);}
static int inside(int px,int py,int x,int y,int w,int h){return px>=x&&py>=y&&px<x+w&&py<y+h;}
static void button(int x,int y,int w,const char*label,int active){r(x,y,w,INSTALL_BUTTON_H,active?ACCENT:MUTED);box(x,y,w,INSTALL_BUTTON_H,active?ACCENT:LINE);text(x+12,y+8,label,active?PANEL:TEXT,active?ACCENT:MUTED);}
static int primary_button_hit(int mx,int my,int px,int py){return inside(mx,my,px+INSTALL_PRIMARY_X,py+INSTALL_PRIMARY_Y,INSTALL_PRIMARY_W,INSTALL_BUTTON_H);}
static int confirm_feed(int erase_count,int k){static const char confirm[]="ERASE";if(k==KEY_BACKSPACE)return erase_count>0?erase_count-1:0;if(k>0&&k<0x80&&erase_count<5){char ch=(char)k;if(ch>='a'&&ch<='z')ch-=32;return ch==confirm[erase_count]?erase_count+1:(ch==confirm[0]?1:0);}return erase_count;}
static void disk_icon(int x,int y,color32_t c){box(x,y,28,18,c);r(x+3,y+4,22,7,c);r(x+4,y+13,4,2,PANEL);r(x+20,y+13,3,2,OK);}
static void installer_cursor(void){
    mouse_state_t sample;const mouse_state_t*m=mouse_snapshot(&sample)?&sample:NULL;if(!m||!m->available)return;
    /* Same outlined arrow and (x,y) tip as Exp; the tip remains the shared
     * click hotspot used by every installer hitbox. */
    static const uint16_t shape[12]={0x800,0xC00,0xE00,0xF00,0xF80,0xFC0,0xFE0,0xFF0,0xF80,0xD80,0x0C0,0x060};
    for(int py=0;py<12;py++)for(int px=0;px<12;px++)if(shape[py]&(uint16_t)(0x800>>px)){int x=m->x+px,y=m->y+py;vbe_putpixel(x+1,y+1,TEXT);vbe_putpixel(x,y,PANEL);}
}

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
        ksnprintf(out,cap,"Existing hd%c%d: %s / %s (%u MiB) will be replaced.",'a'+drv,i+1,part_type_name(e->type),mbr_probe_filesystem(drv,e),e->sector_count/2048u);
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
static void installer_completion_splash(void){
    int w=(int)vbe.width,h=(int)vbe.height;
    r(0,0,w,h,RGB(0x00,0x00,0x00));
    ttf_render_string_percent(w/2-145,h/2-24,"atmkoala",RGB(0xB8,0xB8,0xB8),RGB(0x00,0x00,0x00),220);
    ttf_render_string_percent(w/2-92,h/2+28,"installation complete",RGB(0x68,0x68,0x68),RGB(0x00,0x00,0x00),100);
    vbe_present();
    uint32_t start=pit_get_ticks();while((uint32_t)(pit_get_ticks()-start)<200u)pit_sleep(1);
}

static void installer_event_strip(int x,int y){
    char previous[72],latest[72],row[96];
    installer_log_latest(previous,sizeof(previous),latest,sizeof(latest));
    r(x,y,576,42,MUTED);box(x,y,576,42,LINE);
    ksnprintf(row,sizeof(row),"Event: %s",latest[0]?latest:"Waiting for input");text(x+8,y+7,row,TEXT,MUTED);
    ksnprintf(row,sizeof(row),"Previous: %s",previous[0]?previous:"none");text(x+8,y+24,row,SUB,MUTED);
}

static int install_target_partition(int drv,uint32_t start,uint32_t sectors,const char *timezone,const char *root_password,const char *account_name,const char *account_password){
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
    installer_log_add("MBR written; formatting CatFS partition");
    if(catfs_path_mkdir("/home")<0||catfs_path_mkdir("/syls")<0||catfs_path_mkdir("/syls/bin")<0||
       catfs_path_mkdir("/uiu")<0||catfs_path_mkdir("/uiu/etc")<0||catfs_path_mkdir("/uiu/var")<0||catfs_path_mkdir("/uiu/var/log")<0||catfs_path_mkdir("/data")<0||catfs_path_mkdir("/tmp")<0)return -1;
    if(catfs_sync()<0)return -1;
    if(catfs_vfs_mount("/data")<0)return -1;
    installer_log_add("CatFS mounted at /data");
    /* The account implementation hashes and persists the selected root
     * password in /data/uiu/etc/users.conf. No clear-text password is stored. */
    if(!timezone||!timezone[0]||!root_password||user_set_password("root",root_password)<0)return -1;
    if(sysconf_set("system","timezone",timezone)<0)return -1;
    sysconf_save();
    installer_log_add("Timezone and hashed root password saved");
    if(account_name&&account_name[0]){
        if(!account_password||!user_name_is_valid(account_name)||kstrlen(account_password)<4||user_find(account_name)||user_add(account_name,ROLE_USER,account_password)<0)return -1;
        installer_log_add("Optional standard user created with hashed password");
    }
    installer_log_add("Installation completed successfully");
    installer_log_save();
    return 0;
}

static void frame(int step,int target,uint32_t start,uint32_t sectors,int tz_idx,const char *root_password,const char *account_name,const char *account_password,int setup_field,int erase_count,const char *status,const char *preflight,int show_log){
    int w=(int)vbe.width,h=(int)vbe.height; int px=(w-INSTALL_PANEL_W)/2,py=(h-INSTALL_PANEL_H)/2;
    r(0,0,w,h,BG); r(px,py,INSTALL_PANEL_W,INSTALL_PANEL_H,PANEL); box(px,py,INSTALL_PANEL_W,INSTALL_PANEL_H,LINE);
    r(px,py,INSTALL_PANEL_W,48,ACCENT); text(px+22,py+15,"ATMKoala Disk Installer",PANEL,ACCENT);
    installer_event_strip(px+22,py+60);
    for(int i=0;i<5;i++){int shown=step>5?5:step;int c=i<shown?OK:(i==shown?ACCENT:LINE);r(px+24+i*112,py+122,96,4,c);}
    if(show_log){
        text(px+22,py+154,"Installer log (read-only)",TEXT,PANEL);
        const char *p=installer_log;int ly=py+182;
        while(*p&&ly<py+330){char line[92];int n=0;while(*p&&*p!='\n'&&n<(int)sizeof(line)-1)line[n++]=*p++;line[n]=0;if(*p=='\n')p++;text(px+22,ly,line,SUB,PANEL);ly+=18;}
        text(px+22,py+334,"Press L or Esc to return to the installer.",SUB,PANEL);
        button(px+INSTALL_PRIMARY_X,py+INSTALL_PRIMARY_Y,INSTALL_PRIMARY_W,"Close log",1);installer_cursor();vbe_present();return;
    }
    if(step==0){
        text(px+22,py+154,"Welcome",TEXT,PANEL);
        text(px+22,py+182,"The installer writes only after an explicit typed ERASE confirmation.",SUB,PANEL);
        text(px+22,py+206,"Existing MBR primary entries on the selected target will be replaced.",WARN,PANEL);
        text(px+22,py+236,"Press Enter or click Continue to select an ATA target disk. L views logs.",SUB,PANEL);
    }else if(step==1){
        text(px+22,py+154,"Select target disk",TEXT,PANEL);
        if(target<0) text(px+22,py+190,"No ATA PIO disk was detected. Attach an IDE/ATA writable target, then reboot.",WARN,PANEL);
        else {
            char b[128],s[96];ksnprintf(b,sizeof(b),"Selected: hd%c - %s",'a'+target,disk_drives[target].model[0]?disk_drives[target].model:"ATA PIO disk");
            ksnprintf(s,sizeof(s),"Capacity: %u MiB (%u sectors)",disk_capacity_mib(target),drive_sectors(target));
            disk_icon(px+22,py+184,ACCENT);text(px+60,py+190,b,TEXT,PANEL);text(px+60,py+214,s,SUB,PANEL);
            text(px+22,py+246,preflight?preflight:"",WARN,PANEL);
            text(px+22,py+268,"Mouse: Previous/Next changes drive; Continue selects it.",SUB,PANEL);
        button(px+22,py+300,120,"Previous",1);button(px+154,py+300,120,"Next",1);
        }
    }else if(step==2){
        char l1[128],l2[128];
        text(px+22,py+154,"Plan one aligned CatFS partition",TEXT,PANEL);
        ksnprintf(l1,sizeof(l1),"Target: hd%c1    Start LBA: %u (%u MiB)",'a'+target,start,start/2048u);
        ksnprintf(l2,sizeof(l2),"Size: %u MiB (%u sectors)    End LBA: %u",sectors/2048u,sectors,start+sectors-1u);
        text(px+22,py+185,l1,TEXT,PANEL);text(px+22,py+210,l2,TEXT,PANEL);
        text(px+22,py+246,"Mouse or keys: move start by 1 MiB; change size by 64 MiB.",SUB,PANEL);
        text(px+22,py+268,"Home restores full usable layout. Continue opens first-boot setup.",SUB,PANEL);
        r(px+22,py+286,430,10,MUTED);if(drive_sectors(target)){uint32_t begin=(start*430u)/drive_sectors(target),span=(sectors*430u)/drive_sectors(target);if(span<3)span=3;r(px+22+(int)begin,py+286,(int)span,10,ACCENT);}disk_icon(px+466,py+280,ACCENT);
        button(px+22,py+304,100,"Start -",start>INSTALL_ALIGN_LBA);button(px+132,py+304,100,"Start +",1);button(px+242,py+304,100,"Size -",sectors>INSTALL_MIN_SECTORS);button(px+352,py+304,100,"Size +",1);
    }else if(step==3){
        char line[160],root_masked[65],account_masked[65];int n=(int)kstrlen(root_password);if(n>60)n=60;for(int i=0;i<n;i++)root_masked[i]='*';root_masked[n]=0;n=(int)kstrlen(account_password);if(n>60)n=60;for(int i=0;i<n;i++)account_masked[i]='*';account_masked[n]=0;
        text(px+22,py+154,"First-boot locale and accounts",TEXT,PANEL);
        ksnprintf(line,sizeof(line),"Timezone: %s%s",installer_timezones[tz_idx],setup_field==0?"  < selected":"");text(px+22,py+178,line,setup_field==0?ACCENT:TEXT,PANEL);
        ksnprintf(line,sizeof(line),"Root password: %s%s",root_masked[0]?root_masked:"(minimum 4 characters)",setup_field==1?"  < selected":"");text(px+22,py+202,line,setup_field==1?ACCENT:TEXT,PANEL);
        ksnprintf(line,sizeof(line),"Local user (optional): %s%s",account_name&&account_name[0]?account_name:"(skip for root only)",setup_field==2?"  < selected":"");text(px+22,py+226,line,setup_field==2?ACCENT:TEXT,PANEL);
        ksnprintf(line,sizeof(line),"User password: %s%s",account_masked[0]?account_masked:"(minimum 4 characters if user set)",setup_field==3?"  < selected":"");text(px+22,py+250,line,setup_field==3?ACCENT:TEXT,PANEL);
        text(px+22,py+274,"Tab/click selects a field; arrows select timezone. Passwords are stored only as hashes after Install.",SUB,PANEL);
        button(px+22,py+298,120,"Timezone -",1);button(px+154,py+298,120,"Timezone +",1);
        text(px+22,py+326,status&&status[0]?status:"Enter continues; local user is optional and cannot be an administrator.",status&&status[0]?WARN:SUB,PANEL);
    }else if(step==4){
        char b[128],typed[32];
        text(px+22,py+154,"Confirm destructive MBR rewrite",WARN,PANEL);
        ksnprintf(b,sizeof(b),"hd%c1: CatFS, LBA %u..%u (%u MiB)",'a'+target,start,start+sectors-1u,sectors/2048u);
        text(px+22,py+186,b,WARN,PANEL);
        text(px+22,py+214,"The current primary MBR table will be replaced. No bootloader is installed.",WARN,PANEL);
        ksnprintf(typed,sizeof(typed),"Type ERASE to unlock installation: %d/5",erase_count);
        text(px+22,py+250,typed,erase_count==5?OK:SUB,PANEL);
        text(px+22,py+274,erase_count==5?"Press Enter or click Install to write the table and format CatFS.":"Type ERASE exactly; Install remains locked until keyboard confirmation is complete.",SUB,PANEL);
    }else if(step==5){text(px+22,py+154,"Installing…",TEXT,PANEL);text(px+22,py+188,status,SUB,PANEL);
    }else {text(px+22,py+154,status,step==6?OK:WARN,PANEL);if(step==6){text(px+22,py+188,"CatFS is mounted at /data; timezone, root and optional local user were saved.",SUB,PANEL);}else text(px+22,py+188,"Installation failed; do not rely on the target until inspected.",WARN,PANEL);text(px+22,py+220,"Press Esc or Finish to return to the shell, then reboot to leave installer mode.",SUB,PANEL);}
    if(step<4){button(px+22,py+INSTALL_PRIMARY_Y,120,"Cancel",0);button(px+INSTALL_PRIMARY_X,py+INSTALL_PRIMARY_Y,INSTALL_PRIMARY_W,"Continue",target>=0);} else if(step==4)button(px+INSTALL_PRIMARY_X,py+INSTALL_PRIMARY_Y,INSTALL_PRIMARY_W,"Install",erase_count==5); else if(step>=6)button(px+INSTALL_PRIMARY_X,py+INSTALL_PRIMARY_Y,INSTALL_PRIMARY_W,"Finish",1);
    installer_cursor();vbe_present();
}

int installer_selftest(void){
    char previous[64],latest[64];
    installer_log_reset();installer_log_add("Installer started");installer_log_add("Next slide: select target disk");
    installer_log_latest(previous,sizeof(previous),latest,sizeof(latest));
    if(kstrcmp(previous,"Installer started")||kstrcmp(latest,"Next slide: select target disk"))return -1;
    /* Button rectangles use half-open geometry, preventing an edge click from
     * activating two adjacent controls. These checks have no disk side effects. */
    if(!inside(22,304,22,304,100,30)||inside(122,304,22,304,100,30))return -1;
    if(!inside(121,304,22,304,100,30)||inside(22,334,22,304,100,30))return -1;
    if(!primary_button_hit(INSTALL_PRIMARY_X,INSTALL_PRIMARY_Y,0,0)||primary_button_hit(INSTALL_PRIMARY_X+INSTALL_PRIMARY_W,INSTALL_PRIMARY_Y,0,0))return -1;
    int erase=0;const char *word="ERASE";for(int i=0;word[i];i++)erase=confirm_feed(erase,word[i]);if(erase!=5||confirm_feed(erase,'\r')!=5)return -1;
    if(!user_name_is_valid("new-user")||user_name_is_valid("9invalid")||user_name_is_valid("bad name"))return -1;
    /* A layout strip remains bounded for the smallest valid visual span. */
    uint32_t total=10000,start=2048,sectors=2048,begin=(start*430u)/total,span=(sectors*430u)/total;
    if(begin>=430u)return -1;if(span<3u)span=3u;
    return begin+span<=433u?0:-1;
}

void installer_run(void){
    int step=0,target=first_drive(),last_buttons=0,erase_count=0,tz_idx=0,setup_field=0,show_log=0;uint32_t start=0,sectors=0;const char *status="";char preflight[144],root_password[65],account_name[USER_NAME_MAX+1],account_password[65];kmemset(root_password,0,sizeof(root_password));kmemset(account_name,0,sizeof(account_name));kmemset(account_password,0,sizeof(account_password));installer_log_reset();installer_log_add("Installer started");
    int buffered=(vbe_double_buffer_enable()==0);
    if(target>=0)layout_default(target,&start,&sectors);
    installer_preflight(target,preflight,sizeof(preflight));
    for(;;){
        frame(step,target,start,sectors,tz_idx,root_password,account_name,account_password,setup_field,erase_count,status,preflight,show_log);
        int k=keyboard_poll();mouse_state_t mouse_sample;const mouse_state_t*m=mouse_snapshot(&mouse_sample)?&mouse_sample:NULL;int click=m&&m->available&&(m->buttons&1)&&!(last_buttons&1);if(m)last_buttons=m->buttons;
        int px=((int)vbe.width-INSTALL_PANEL_W)/2,py=((int)vbe.height-INSTALL_PANEL_H)/2;
        if((k=='l'||k=='L')){show_log=!show_log;continue;}
        if(show_log&&click&&primary_button_hit(m->x,m->y,px,py)){show_log=0;continue;}
        if(k==KEY_ESC&&show_log){show_log=0;continue;}
        if(k==KEY_ESC || (click&&step<4&&inside(m->x,m->y,px+22,py+355,120,30))){installer_log_add("Installer cancelled");kmemset(root_password,0,sizeof(root_password));kmemset(account_password,0,sizeof(account_password));if(buffered)vbe_double_buffer_disable();return;}
        if(step==0&&(k=='\n'||k=='\r'||(click&&primary_button_hit(m->x,m->y,px,py)))){installer_log_add("Next slide: select target disk");step=1;continue;}
        if(step==1){
            if(k==KEY_LEFT||k==KEY_UP||(click&&inside(m->x,m->y,px+22,py+300,120,30))){target=next_drive(target,-1);layout_default(target,&start,&sectors);installer_preflight(target,preflight,sizeof(preflight));installer_log_add("Target disk changed: previous available drive");}
            else if(k==KEY_RIGHT||k==KEY_DOWN||(click&&inside(m->x,m->y,px+154,py+300,120,30))){target=next_drive(target,1);layout_default(target,&start,&sectors);installer_preflight(target,preflight,sizeof(preflight));installer_log_add("Target disk changed: next available drive");}
            else if((k=='\n'||k=='\r'||(click&&primary_button_hit(m->x,m->y,px,py)))&&target>=0){layout_default(target,&start,&sectors);installer_log_add("Next slide: plan CatFS partition layout");step=2;}
            continue;
        }
        if(step==2){
            if((k==KEY_LEFT&&start>INSTALL_ALIGN_LBA)||(click&&inside(m->x,m->y,px+22,py+304,100,30)&&start>INSTALL_ALIGN_LBA)){start-=INSTALL_ALIGN_LBA;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_RIGHT||(click&&inside(m->x,m->y,px+132,py+304,100,30))){start+=INSTALL_ALIGN_LBA;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_UP||(click&&inside(m->x,m->y,px+352,py+304,100,30))){if(sectors<=drive_sectors(target)-start-INSTALL_SIZE_STEP_SECTORS)sectors+=INSTALL_SIZE_STEP_SECTORS;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_DOWN||(click&&inside(m->x,m->y,px+242,py+304,100,30))){if(sectors>INSTALL_MIN_SECTORS+INSTALL_SIZE_STEP_SECTORS)sectors-=INSTALL_SIZE_STEP_SECTORS;else sectors=INSTALL_MIN_SECTORS;layout_clamp(target,&start,&sectors);}
            else if(k==KEY_HOME){layout_default(target,&start,&sectors);}
            else if((k=='\n'||k=='\r'||(click&&primary_button_hit(m->x,m->y,px,py)))&&sectors>=INSTALL_MIN_SECTORS){setup_field=0;installer_log_add("Next slide: first-boot locale and accounts");step=3;}
            continue;
        }
        if(step==3){
            size_t root_len=kstrlen(root_password),name_len=kstrlen(account_name),account_len=kstrlen(account_password);
            if(click&&inside(m->x,m->y,px+22,py+170,420,22)){setup_field=0;status="";}
            else if(click&&inside(m->x,m->y,px+22,py+194,420,22)){setup_field=1;status="";}
            else if(click&&inside(m->x,m->y,px+22,py+218,420,22)){setup_field=2;status="";}
            else if(click&&inside(m->x,m->y,px+22,py+242,420,22)){setup_field=3;status="";}
            else if(k==KEY_TAB){setup_field=(setup_field+1)%4;status="";}
            else if((setup_field==0&&(k==KEY_LEFT||k==KEY_UP))||(click&&inside(m->x,m->y,px+22,py+298,120,30))){tz_idx=(tz_idx+INSTALL_TZ_COUNT-1)%INSTALL_TZ_COUNT;}
            else if((setup_field==0&&(k==KEY_RIGHT||k==KEY_DOWN))||(click&&inside(m->x,m->y,px+154,py+298,120,30))){tz_idx=(tz_idx+1)%INSTALL_TZ_COUNT;}
            else if(k==KEY_BACKSPACE){char *field=setup_field==1?root_password:(setup_field==2?account_name:(setup_field==3?account_password:0));if(field){size_t n=kstrlen(field);if(n)field[n-1]=0;}}
            else if(k>=0x21&&k<0x7f){char *field=setup_field==1?root_password:(setup_field==2?account_name:(setup_field==3?account_password:0));size_t cap=setup_field==2?sizeof(account_name):(setup_field?sizeof(root_password):0);if(field&&kstrlen(field)+1<cap){size_t n=kstrlen(field);field[n]=(char)k;field[n+1]=0;}}
            else if(k=='\n'||k=='\r'||(click&&primary_button_hit(m->x,m->y,px,py))){
                if(root_len<4)status="Root password must have at least 4 characters.";
                else if(name_len&&(!user_name_is_valid(account_name)||user_find(account_name)))status="Local user must be a new name: letters first, then letters/digits/_/-.";
                else if(name_len&&account_len<4)status="Local user password must have at least 4 characters.";
                else {erase_count=0;status="";installer_log_add("Next slide: destructive ERASE confirmation");step=4;}
            }
            continue;
        }
        if(step==4){
            /* Enter is an activation key, not an ERASE character. Evaluate the
             * already unlocked action before accepting any printable input. */
            if(erase_count==5&&(k=='\n'||k=='\r'||(click&&primary_button_hit(m->x,m->y,px,py)))){int result;step=5;status="Writing MBR, CatFS, timezone and local accounts…";installer_log_add(status);frame(step,target,start,sectors,tz_idx,root_password,account_name,account_password,setup_field,erase_count,status,preflight,0);result=install_target_partition(target,start,sectors,installer_timezones[tz_idx],root_password,account_name,account_password);if(result==0){installer_completion_splash();step=6;status="Installation completed successfully.";}else{step=7;status="Installation failed after the destructive confirmation.";installer_log_add(status);installer_log_save();}}
            else if(k=='\n'||k=='\r')status="Type ERASE exactly before activation.";
            else {int before=erase_count;erase_count=confirm_feed(erase_count,k);if(erase_count==5&&before!=5)installer_log_add("ERASE accepted: Install button unlocked");}
            continue;
        }
        if(step==6||step==7){if(k=='\n'||k=='\r'||(click&&primary_button_hit(m->x,m->y,px,py))){installer_log_add("Installer session finished");installer_log_save();kmemset(root_password,0,sizeof(root_password));kmemset(account_password,0,sizeof(account_password));if(buffered)vbe_double_buffer_disable();return;}}
        __asm__ volatile("pause");
    }
}
