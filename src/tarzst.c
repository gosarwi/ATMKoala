#include "tarzst.h"
#include "vfs.h"
#include "config.h"
#include "http_client.h"
#include "kmalloc.h"
#include "util.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define ZSTD_MAGIC 0xFD2FB528u
#define TAR_BLOCK 512u
#define ATPK_STAGE_SUFFIX ".atpk-stage"

static uint8_t tzst_workspace[TZST_MAX_UNPACKED];
static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void wr64(uint8_t *p,uint64_t v){for(int i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t align512(uint32_t n){return (n+TAR_BLOCK-1u)&~(TAR_BLOCK-1u);}

/* Packages emitted by ATMKoala use raw frames. Compressed blocks are rejected,
 * avoiding a large decompressor in the kernel's trusted installer path. */
static int zstd_unpack_raw(const uint8_t *src,uint32_t size,uint8_t *out,uint32_t cap,uint32_t *outsz){
    if(!src||size<6||rd32(src)!=ZSTD_MAGIC)return -1;
    uint32_t ip=4,op=0;uint8_t desc=src[ip++];uint8_t fc=(uint8_t)(desc>>6),single=(uint8_t)((desc>>5)&1),dict=(uint8_t)(desc&3);
    uint32_t ds=dict==0?0:(dict==1?1:(dict==2?2:4));if(ip+ds>size)return -2;ip+=ds;
    uint32_t fs=fc==0?(single?1:0):(fc==1?2:(fc==2?4:8));if(ip+fs>size)return -3;uint64_t declared=0;
    for(uint32_t i=0;i<fs;i++)declared|=(uint64_t)src[ip++]<<(8*i);
    for(;;){if(ip+3>size)return -4;uint32_t bh=(uint32_t)src[ip]|((uint32_t)src[ip+1]<<8)|((uint32_t)src[ip+2]<<16);ip+=3;uint32_t last=bh&1u,type=(bh>>1)&3u,bs=bh>>3;
        if(type==0){if(ip+bs>size||op+bs>cap)return -5;kmemcpy(out+op,src+ip,bs);ip+=bs;op+=bs;}
        else if(type==1){if(ip>=size||op+bs>cap)return -6;uint8_t v=src[ip++];for(uint32_t i=0;i<bs;i++)out[op++]=v;}
        else return -7;
        if(last)break;
    }
    if(declared&&declared!=op)return -8;*outsz=op;return 0;
}

static int tar_zero(const uint8_t *p){for(uint32_t i=0;i<TAR_BLOCK;i++)if(p[i])return 0;return 1;}
static uint32_t tar_octal(const uint8_t *p,uint32_t n){uint32_t v=0;for(uint32_t i=0;i<n&&p[i];i++){if(p[i]<'0'||p[i]>'7')break;v=(v<<3)+(uint32_t)(p[i]-'0');}return v;}
static void tar_octal_write(uint8_t *p,uint32_t n,uint32_t v){for(uint32_t i=0;i<n;i++)p[i]=0;if(!n)return;p[n-1]=0;for(int i=(int)n-2;i>=0;i--){p[i]=(uint8_t)('0'+(v&7u));v>>=3;}}
static int tar_safe_name(const char *name){if(!name||!name[0]||name[0]=='/')return 0;for(const char*p=name;*p;p++)if(p[0]=='.'&&p[1]=='.'&&(p==name||p[-1]=='/')&&(p[2]=='/'||p[2]==0))return 0;return 1;}
static int tar_next(const uint8_t *tar,uint32_t total,uint32_t *pos,const char **name,const uint8_t **data,uint32_t *size,uint8_t *type){
    if(*pos+TAR_BLOCK>total)return 0;const uint8_t*h=tar+*pos;if(tar_zero(h))return 0;
    if(h[257]!='u'||h[258]!='s'||h[259]!='t'||h[260]!='a'||h[261]!='r')return -1;uint32_t fsize=tar_octal(h+124,12),payload=*pos+TAR_BLOCK;
    if(payload+fsize>total)return -2;*name=(const char*)h;*data=tar+payload;*size=fsize;*type=h[156];*pos=payload+align512(fsize);return 1;
}
static uint32_t crc32(const uint8_t *p,uint32_t n){uint32_t c=0xFFFFFFFFu;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^((c&1)?0xEDB88320u:0);}return ~c;}

static int ensure_parent(const char *path){char dir[128],build[128];int len=(int)kstrlen(path);if(len<=1||len>=(int)sizeof(dir))return -1;kstrcpy(dir,path);char*last=dir;for(char*p=dir;*p;p++)if(*p=='/')last=p;if(last==dir)return 0;*last=0;build[0]=0;char*p=dir;while(*p){while(*p=='/')p++;if(!*p)break;char*start=p;while(*p&&*p!='/')p++;char saved=*p;*p=0;kstrcat(build,"/");kstrcat(build,start);vfs_mkdir(build,0755);*p=saved;}return 0;}
static int map_legacy_path(const char *name,char *dest,uint32_t cap){if(!tar_safe_name(name)||!dest||cap<2)return -1;if(kstrncmp(name,"bin/",4)==0)kstrcpy(dest,"/syls/bin/");else if(kstrncmp(name,"etc/",4)==0)kstrcpy(dest,"/uiu/etc/");else if(kstrncmp(name,"share/",6)==0)kstrcpy(dest,"/uiu/share/");else return -1;const char*tail=name;while(*tail&&*tail!='/')tail++;if(*tail=='/')tail++;if(!tail[0]||kstrlen(dest)+kstrlen(tail)+1>=cap)return -1;kstrcat(dest,tail);return 0;}
static int map_atpk_path(const char *name,char *dest,uint32_t cap){
    if(!tar_safe_name(name)||kstrncmp(name,"data/",5)!=0)return -1;const char*tail=name+5;if(!tail[0])return -1;
    if(kstrncmp(tail,"apps/",5)==0)kstrcpy(dest,"/uiu/apps/");
    else if(kstrncmp(tail,"share/",6)==0)kstrcpy(dest,"/uiu/share/");
    else if(kstrncmp(tail,"fonts/",6)==0)kstrcpy(dest,"/uiu/share/fonts/");
    else if(kstrncmp(tail,"bin/",4)==0)kstrcpy(dest,"/syls/bin/");
    else return -1;
    while(*tail&&*tail!='/')tail++;if(*tail=='/')tail++;if(!tail[0]||kstrlen(dest)+kstrlen(tail)+1>=cap)return -1;kstrcat(dest,tail);return 0;
}
static int copy_line_value(const uint8_t *data,uint32_t size,const char *key,char *out,uint32_t cap){
    uint32_t kl=(uint32_t)kstrlen(key),p=0;if(!data||!out||cap<2)return -1;out[0]=0;
    while(p<size){uint32_t st=p;while(p<size&&data[p]!='\n'&&data[p]!='\r')p++;uint32_t len=p-st;while(p<size&&(data[p]=='\n'||data[p]=='\r'))p++;
        if(len>kl+1&&kmemcmp(data+st,key,kl)==0&&data[st+kl]==':'){uint32_t q=st+kl+1;while(q<st+len&&(data[q]==' '||data[q]=='\t'))q++;uint32_t n=st+len-q;if(n>=cap)n=cap-1;kmemcpy(out,data+q,n);out[n]=0;return 0;}
    }return -1;
}
static int valid_pkg_name(const char*s){if(!s||!s[0]||kstrlen(s)>=TZST_NAME_MAX)return 0;for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='+'||*s=='-'||*s=='.'))return 0;return 1;}
static uint32_t dec_u32(const char*s){uint32_t v=0;for(;*s>='0'&&*s<='9';s++)v=v*10u+(uint32_t)(*s-'0');return v;}
static int hex_u32(const char*s,uint32_t*out){uint32_t v=0;int n=0;for(;*s&&*s!=' '&&*s!='\t';s++){int d;if(*s>='0'&&*s<='9')d=*s-'0';else if(*s>='a'&&*s<='f')d=*s-'a'+10;else if(*s>='A'&&*s<='F')d=*s-'A'+10;else return -1;if(n++>=8)return -1;v=(v<<4)|(uint32_t)d;}if(n!=8)return -1;*out=v;return 0;}
/* Manifest line: crc32-hex SP decimal-size SP data/relative/path */
static int manifest_find(const uint8_t *data,uint32_t size,const char *path,uint32_t *want_crc,uint32_t *want_size){uint32_t p=0;while(p<size){uint32_t st=p;while(p<size&&data[p]!='\n'&&data[p]!='\r')p++;uint32_t end=p;while(p<size&&(data[p]=='\n'||data[p]=='\r'))p++;if(end<=st)continue;char line[192];uint32_t n=end-st;if(n>=sizeof(line))return -1;kmemcpy(line,data+st,n);line[n]=0;char*a=line;char*b=a;while(*b&&*b!=' ')b++;if(!*b)continue;*b++=0;while(*b==' ')b++;char*c=b;while(*c&&*c!=' ')c++;if(!*c)continue;*c++=0;while(*c==' ')c++;if(kstrcmp(c,path)!=0)continue;if(hex_u32(a,want_crc)<0)return -1;*want_size=dec_u32(b);return 0;}return -1;}
static int archive_name_count(const tzst_pkg_t *pkg,const char *wanted){uint32_t pos=0,count=0;for(;;){const char*n;const uint8_t*d;uint32_t sz;uint8_t t;int r=tar_next(pkg->tar,pkg->tar_size,&pos,&n,&d,&sz,&t);if(r<=0)return count;if(!kstrcmp(n,wanted))count++;}}

int tzst_parse(tzst_pkg_t *pkg,const uint8_t *buf,uint32_t size){
    if(!pkg)return -1;kmemset(pkg,0,sizeof(*pkg));uint32_t unpacked=0;if(zstd_unpack_raw(buf,size,tzst_workspace,sizeof(tzst_workspace),&unpacked)<0||unpacked<TAR_BLOCK)return -2;
    pkg->tar=tzst_workspace;pkg->tar_size=unpacked;kstrcpy(pkg->package_name,"package");kstrcpy(pkg->architecture,"atmkoala-x86_64");
    const uint8_t*control=0,*manifest=0;uint32_t control_sz=0,manifest_sz=0,pos=0;
    for(;;){const char*n;const uint8_t*d;uint32_t sz;uint8_t t;int r=tar_next(pkg->tar,pkg->tar_size,&pos,&n,&d,&sz,&t);if(r<=0){if(r<0)return -3;break;}if(!tar_safe_name(n))return -4;if(t==0||t=='0')pkg->file_count++;
        if(!kstrcmp(n,"ATPK/control")){if(control)return -5;control=d;control_sz=sz;pkg->atpk=1;}
        else if(!kstrcmp(n,"ATPK/manifest")){if(manifest)return -6;manifest=d;manifest_sz=sz;pkg->manifest_present=1;pkg->atpk=1;}
        else if(!kstrcmp(n,"META/name")&&sz){uint32_t cp=sz>=sizeof(pkg->package_name)?sizeof(pkg->package_name)-1:sz;kmemcpy(pkg->package_name,d,cp);pkg->package_name[cp]=0;for(uint32_t i=0;pkg->package_name[i];i++)if(pkg->package_name[i]=='\n'||pkg->package_name[i]=='\r'){pkg->package_name[i]=0;break;}}
        if(kstrncmp(n,"data/",5)==0&&(t==0||t=='0'))pkg->payload_count++;
    }
    if(pkg->atpk){if(!control||!manifest||pkg->payload_count==0)return -7;copy_line_value(control,control_sz,"Package",pkg->package_name,sizeof(pkg->package_name));copy_line_value(control,control_sz,"Version",pkg->version,sizeof(pkg->version));copy_line_value(control,control_sz,"Architecture",pkg->architecture,sizeof(pkg->architecture));copy_line_value(control,control_sz,"Description",pkg->description,sizeof(pkg->description));if(!valid_pkg_name(pkg->package_name)||!pkg->version[0]||(kstrcmp(pkg->architecture,"atmkoala-x86_64")&&kstrcmp(pkg->architecture,"all")))return -8;}
    return pkg->file_count?0:-9;
}

void tzst_info(const tzst_pkg_t *pkg){if(!pkg)return;terminal_set_color(VGA_LIGHT_CYAN,VGA_BLACK);terminal_writeln(pkg->atpk?"ATPK native package":"legacy .tar.zst package");terminal_set_color(VGA_LIGHT_GREY,VGA_BLACK);terminal_write("Name: ");terminal_writeln(pkg->package_name);if(pkg->atpk){terminal_write("Version: ");terminal_writeln(pkg->version);terminal_write("Architecture: ");terminal_writeln(pkg->architecture);terminal_write("Payload entries: ");}else terminal_write("Files: ");char n[12];kuitoa(pkg->atpk?pkg->payload_count:pkg->file_count,n,10);terminal_writeln(n);terminal_writeln("Codec: Zstandard raw/RLE blocks");}

static int install_legacy(const tzst_pkg_t *pkg){uint32_t pos=0,installed=0;for(;;){const char*n;const uint8_t*d;uint32_t sz;uint8_t t;int r=tar_next(pkg->tar,pkg->tar_size,&pos,&n,&d,&sz,&t);if(r<=0){if(r<0)return -2;break;}if(kstrncmp(n,"META/",5)==0)continue;char dest[128];if(map_legacy_path(n,dest,sizeof(dest))<0){terminal_write("pkg: skipped unsafe or unsupported path: ");terminal_writeln(n);continue;}if(t=='5'){vfs_mkdir(dest,0755);continue;}if(t!=0&&t!='0')continue;ensure_parent(dest);int fd=vfs_open(dest,O_WRONLY|O_CREAT|O_TRUNC,0755);if(fd<0||vfs_write(fd,d,sz)!=(int64_t)sz){if(fd>=0)vfs_close(fd);return -3;}vfs_close(fd);installed++;}
    if(!installed)return -4;cfg_set(&g_pkgcfg,pkg->package_name,"format","legacy-tar.zst");cfg_set(&g_pkgcfg,pkg->package_name,"installed","yes");cfg_save(&g_pkgcfg,CFG_ROOT "/packages.conf");return 0;}

static int install_atpk(const tzst_pkg_t *pkg){
    char dests[TZST_MAX_FILES][128],stages[TZST_MAX_FILES][128];uint32_t count=0,pos=0;
    /* Preflight: every data file must be represented exactly once in manifest,
     * checksum-valid, mappable and conflict-free before a single final path moves. */
    for(;;){const char*n;const uint8_t*d;uint32_t sz,wc,ws;uint8_t t;int r=tar_next(pkg->tar,pkg->tar_size,&pos,&n,&d,&sz,&t);if(r<=0){if(r<0)return -10;break;}if(kstrncmp(n,"data/",5)!=0)continue;if(t=='5')continue;if(t!=0&&t!='0')return -11;if(count>=TZST_MAX_FILES||archive_name_count(pkg,n)!=1)return -12;if(map_atpk_path(n,dests[count],sizeof(dests[count]))<0)return -13;if(manifest_find(NULL,0,"",&wc,&ws)){} /* keep compiler from treating helper as unused */
        const uint8_t*mf=0;uint32_t mfsz=0,p2=0;for(;;){const char*mn;const uint8_t*md;uint32_t mz;uint8_t mt;int mr=tar_next(pkg->tar,pkg->tar_size,&p2,&mn,&md,&mz,&mt);if(mr<=0)break;if(!kstrcmp(mn,"ATPK/manifest")){mf=md;mfsz=mz;break;}}
        if(!mf||manifest_find(mf,mfsz,n,&wc,&ws)<0||ws!=sz||wc!=crc32(d,sz))return -14;vfs_stat_t st;if(vfs_stat(dests[count],&st)==0)return -15;
        if(kstrlen(dests[count])+kstrlen(ATPK_STAGE_SUFFIX)+1>=sizeof(stages[count]))return -16;kstrcpy(stages[count],dests[count]);kstrcat(stages[count],ATPK_STAGE_SUFFIX);if(vfs_stat(stages[count],&st)==0)return -17;count++;
    }
    if(!count)return -18;
    /* Stage next to final destination, preserving same-filesystem rename semantics. */
    pos=0;uint32_t i=0;for(;;){const char*n;const uint8_t*d;uint32_t sz;uint8_t t;int r=tar_next(pkg->tar,pkg->tar_size,&pos,&n,&d,&sz,&t);if(r<=0)break;if(kstrncmp(n,"data/",5)!=0||!(t==0||t=='0'))continue;ensure_parent(stages[i]);int fd=vfs_open(stages[i],O_WRONLY|O_CREAT|O_EXCL,0755);if(fd<0||vfs_write(fd,d,sz)!=(int64_t)sz){if(fd>=0)vfs_close(fd);for(uint32_t j=0;j<=i;j++)vfs_unlink(stages[j]);return -19;}vfs_close(fd);i++;}
    /* Commit. Existing files were prohibited by preflight. If a rename fails,
     * delete both staged and already committed files, restoring pre-install state. */
    for(i=0;i<count;i++)if(vfs_rename(stages[i],dests[i])<0){for(uint32_t j=0;j<count;j++)vfs_unlink(stages[j]);for(uint32_t j=0;j<i;j++)vfs_unlink(dests[j]);return -20;}
    cfg_set(&g_pkgcfg,pkg->package_name,"format","atpk-1");cfg_set(&g_pkgcfg,pkg->package_name,"version",pkg->version);cfg_set(&g_pkgcfg,pkg->package_name,"architecture",pkg->architecture);cfg_set(&g_pkgcfg,pkg->package_name,"description",pkg->description[0]?pkg->description:"-");cfg_set(&g_pkgcfg,pkg->package_name,"installed","yes");cfg_save(&g_pkgcfg,CFG_ROOT "/packages.conf");terminal_write("atpk: installed ");terminal_writeln(pkg->package_name);return 0;
}

int tzst_install(const tzst_pkg_t *pkg){if(!pkg||!pkg->tar)return -1;return pkg->atpk?install_atpk(pkg):install_legacy(pkg);}
int tzst_remove(const char *name){const char*inst=cfg_get(&g_pkgcfg,name,"installed");if(!inst||kstrcmp(inst,"yes"))return -1;cfg_set(&g_pkgcfg,name,"installed","no");cfg_save(&g_pkgcfg,CFG_ROOT "/packages.conf");terminal_write("pkg: removed registry metadata for ");terminal_writeln(name);return 0;}

static int package_cache_prepare(void){
    vfs_stat_t st;
    if(vfs_mkdir("/uiu",0755)<0&&vfs_stat("/uiu",&st)<0)return -1;
    if(vfs_mkdir("/uiu/cache",0755)<0&&vfs_stat("/uiu/cache",&st)<0)return -1;
    if(vfs_mkdir("/uiu/cache/packages",0755)<0&&vfs_stat("/uiu/cache/packages",&st)<0)return -1;
    return 0;
}
/* Remote package intake deliberately differs from local compatibility install:
 * it accepts only ATPK with control+manifest. HTTP remains transport only;
 * integrity inside the archive is verified by tzst_parse/tzst_install before
 * payload paths are committed. */
int tzst_fetch_install_http(const char *url){
    uint8_t *body=(uint8_t *)kmalloc(ATM_HTTP_RESPONSE_MAX);atm_http_response_t response;tzst_pkg_t pkg;
    char stage[128],final[128];vfs_stat_t st;int fd=-1,rc=-1;
    if(!body)return -1;
    if(atm_http_get(url,body,ATM_HTTP_RESPONSE_MAX,&response)<0)goto done;
    if(!response.body_length||tzst_parse(&pkg,body,response.body_length)<0||!pkg.atpk)goto done;
    if(package_cache_prepare()<0)goto done;
    kstrcpy(final,"/uiu/cache/packages/");kstrcat(final,pkg.package_name);kstrcat(final,".atpk");
    if(kstrlen(final)+10>=sizeof(stage)||vfs_stat(final,&st)==0)goto done;
    kstrcpy(stage,final);kstrcat(stage,".download");
    fd=vfs_open(stage,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0||vfs_write(fd,body,response.body_length)!=(int64_t)response.body_length)goto done;
    vfs_close(fd);fd=-1;
    if(vfs_rename(stage,final)<0){(void)vfs_unlink(stage);goto done;}
    rc=tzst_install(&pkg);
done:
    if(fd>=0){vfs_close(fd);(void)vfs_unlink(stage);}kfree(body);return rc;
}

static int valid_repo_url(const char *url){
    uint32_t n;if(!url||kstrncmp(url,"http://",7)!=0)return 0;n=(uint32_t)kstrlen(url);
    if(n<=7||n>=ATM_HTTP_URL_MAX)return 0;
    for(uint32_t i=0;i<n;i++)if((uint8_t)url[i]<=0x20||url[i]=='#'||url[i]=='?')return 0;
    return 1;
}
const char *tzst_repo_url(void){
    const char *url=cfg_get(&g_pkgcfg,"repository","url");
    return valid_repo_url(url)?url:NULL;
}
int tzst_repo_set_url(const char *url){
    char normalized[ATM_HTTP_URL_MAX+1];uint32_t n;
    if(!valid_repo_url(url))return -1;n=(uint32_t)kstrlen(url);
    while(n>7&&url[n-1]=='/')n--;if(!n)return -1;
    kmemcpy(normalized,url,n);normalized[n]=0;
    cfg_set(&g_pkgcfg,"repository","url",normalized);
    return cfg_save(&g_pkgcfg,CFG_ROOT "/packages.conf");
}
static int repo_build_url(const char *base,const char *pkg_name,char *url,uint32_t cap){
    uint32_t n;if(!base||!url||!cap||!valid_repo_url(base)||!valid_pkg_name(pkg_name))return -1;
    n=(uint32_t)kstrlen(base);if(n+1u+(uint32_t)kstrlen(pkg_name)+5u>=cap)return -1;
    kstrcpy(url,base);kstrcat(url,"/");kstrcat(url,pkg_name);kstrcat(url,".atpk");return 0;
}
int tzst_repo_fetch_package(const char *pkg_name){
    const char *base=tzst_repo_url();char url[ATM_HTTP_URL_MAX+1];
    if(repo_build_url(base,pkg_name,url,sizeof(url))<0)return -1;
    return tzst_fetch_install_http(url);
}
uint32_t tzst_installed_count(void){uint32_t count=0;for(int i=0;i<g_pkgcfg.count&&i<CFG_MAX_SECTIONS;i++){const char *installed=cfg_get(&g_pkgcfg,g_pkgcfg.sections[i].name,"installed");if(installed&&!kstrcmp(installed,"yes"))count++;}return count;}
int tzst_installed_at(uint32_t index,char *name,uint32_t name_cap,char *version,uint32_t version_cap,char *arch,uint32_t arch_cap){if(!name||!version||!arch||!name_cap||!version_cap||!arch_cap)return -1;uint32_t seen=0;for(int i=0;i<g_pkgcfg.count&&i<CFG_MAX_SECTIONS;i++){const char *section=g_pkgcfg.sections[i].name;const char *installed=cfg_get(&g_pkgcfg,section,"installed");if(!installed||kstrcmp(installed,"yes"))continue;if(seen++!=index)continue;const char *v=cfg_get(&g_pkgcfg,section,"version");const char *a=cfg_get(&g_pkgcfg,section,"architecture");kstrncpy(name,section,name_cap-1);name[name_cap-1]=0;kstrncpy(version,v?v:"-",version_cap-1);version[version_cap-1]=0;kstrncpy(arch,a?a:"-",arch_cap-1);arch[arch_cap-1]=0;return 0;}return -1;}
int tzst_installed_description_at(uint32_t index,char *description,uint32_t description_cap){if(!description||!description_cap)return -1;uint32_t seen=0;for(int i=0;i<g_pkgcfg.count&&i<CFG_MAX_SECTIONS;i++){const char *section=g_pkgcfg.sections[i].name;const char *installed=cfg_get(&g_pkgcfg,section,"installed");if(!installed||kstrcmp(installed,"yes"))continue;if(seen++!=index)continue;const char *d=cfg_get(&g_pkgcfg,section,"description");kstrncpy(description,d&&d[0]?d:"-",description_cap-1);description[description_cap-1]=0;return 0;}return -1;}
int tzst_repo_selftest(void){
    char url[ATM_HTTP_URL_MAX+1];
    if(!valid_repo_url("http://127.0.0.1:8080/atpk")||valid_repo_url("https://repo.invalid")||
       valid_repo_url("http://repo.invalid/a?b")||valid_repo_url("http://repo.invalid/a b"))return -1;
    if(repo_build_url("http://repo.test/base","demo-app",url,sizeof(url))<0||kstrcmp(url,"http://repo.test/base/demo-app.atpk"))return -1;
    if(repo_build_url("http://repo.test/base","../bad",url,sizeof(url))==0)return -1;
    return 0;
}

static uint32_t tar_write_file(uint8_t *tar,uint32_t cap,uint32_t pos,const char *name,const uint8_t *data,uint32_t size){uint32_t next=pos+TAR_BLOCK+align512(size);if(next>cap||!tar_safe_name(name))return 0;uint8_t*h=tar+pos;kmemset(h,0,TAR_BLOCK);kstrncpy((char*)h,name,99);tar_octal_write(h+100,8,0755);tar_octal_write(h+124,12,size);h[156]='0';kmemcpy(h+257,"ustar",5);h[262]='0';h[263]='0';kmemcpy(tar+pos+TAR_BLOCK,data,size);return next;}
int tzst_wrap_elf(const uint8_t *elf,uint32_t es,const char *name,const char *ver,uint8_t*out,uint32_t cap){
    if(!elf||!name||!valid_pkg_name(name)||!out||es>TZST_MAX_UNPACKED-4096u)return -1;
    kmemset(tzst_workspace,0,sizeof(tzst_workspace));
    char control[256],path[112],manifest[192],sizebuf[16],crcbuf[12];
    kstrcpy(path,"data/bin/");kstrcat(path,name);
    kstrcpy(control,"Package: ");kstrcat(control,name);kstrcat(control,"\nVersion: ");kstrcat(control,ver?ver:"1.0.0");kstrcat(control,"\nArchitecture: atmkoala-x86_64\nDescription: Native ATMKoala application\n");
    uint32_t c=crc32(elf,es);static const char hx[]="0123456789abcdef";for(int i=0;i<8;i++)crcbuf[i]=hx[(c>>((7-i)*4))&15u];crcbuf[8]=0;kuitoa(es,sizebuf,10);
    kstrcpy(manifest,crcbuf);kstrcat(manifest," ");kstrcat(manifest,sizebuf);kstrcat(manifest," ");kstrcat(manifest,path);kstrcat(manifest,"\n");
    uint32_t pos=tar_write_file(tzst_workspace,sizeof(tzst_workspace),0,"ATPK/control",(const uint8_t*)control,(uint32_t)kstrlen(control));
    if(!pos)return -2;pos=tar_write_file(tzst_workspace,sizeof(tzst_workspace),pos,"ATPK/manifest",(const uint8_t*)manifest,(uint32_t)kstrlen(manifest));
    if(!pos)return -3;pos=tar_write_file(tzst_workspace,sizeof(tzst_workspace),pos,path,elf,es);if(!pos||pos+1024u>sizeof(tzst_workspace))return -4;
    uint32_t ts=pos+1024u,op=0;if(cap<ts+32u)return -5;out[op++]=0x28;out[op++]=0xB5;out[op++]=0x2F;out[op++]=0xFD;out[op++]=0xE0;wr64(out+op,ts);op+=8;
    for(uint32_t ip=0;ip<ts;){uint32_t ch=ts-ip;if(ch>131071u)ch=131071u;uint32_t bh=(ch<<3)|((ip+ch==ts)?1u:0u);out[op++]=(uint8_t)bh;out[op++]=(uint8_t)(bh>>8);out[op++]=(uint8_t)(bh>>16);kmemcpy(out+op,tzst_workspace+ip,ch);op+=ch;ip+=ch;}return (int)op;
}
