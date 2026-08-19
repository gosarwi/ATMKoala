#include "ext2.h"
#include "disk.h"
#include "partmgr.h"
#include "util.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define EXT2_MAGIC 0xEF53
#define EXT2_ROOT_INO 2
#define EXT2_INCOMPAT_EXTENTS 0x0040
#define EXT2_FEATURE_COMPAT_HAS_JOURNAL 0x0004
#define EXT2_VALID_FS 0x0001

typedef struct __attribute__((packed)) {
 uint32_t inodes_count,blocks_count,r_blocks_count,free_blocks_count,free_inodes_count,first_data_block,log_block_size,log_frag_size,blocks_per_group,frags_per_group,inodes_per_group;
 uint32_t mtime,wtime; uint16_t mnt_count,max_mnt_count,magic,state,errors,minor_rev_level; uint32_t lastcheck,checkinterval,creator_os,rev_level; uint16_t def_resuid,def_resgid; uint32_t first_ino; uint16_t inode_size; uint16_t block_group_nr; uint32_t feature_compat,feature_incompat,feature_ro_compat; uint8_t uuid[16]; char volume_name[16];
} ext2_super_t;
typedef struct __attribute__((packed)){uint32_t block_bitmap,inode_bitmap,inode_table,free_blocks,free_inodes,used_dirs;uint16_t pad;uint8_t reserved[12];} ext2_gd_t;
typedef struct __attribute__((packed)){uint16_t mode,uid;uint32_t size,atime,ctime,mtime,dtime;uint16_t gid,links_count;uint32_t blocks,flags,osd1,block[15],generation,file_acl,dir_acl,faddr;uint8_t osd2[12];} ext2_disk_inode_t;

ext2_state_t ext2={0};
static uint8_t blockbuf[4096], dirbuf[4096], indbuf1[4096], indbuf2[4096], indbuf3[4096], inodebuf[4096];
static uint32_t u32_at(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int read_block(uint32_t b,uint8_t *out){
 if(!ext2.mounted||!out||ext2.block_size<1024||ext2.block_size>4096||b>=ext2.blocks_count)return -1;
 return disk_read(ext2.drive,ext2.part_lba+b*(ext2.block_size/512),ext2.block_size/512,out);
}
static int write_block(uint32_t b,const uint8_t *in){
 if(!ext2.mounted||!in||!ext2.write_enabled||b>=ext2.blocks_count)return -1;
 return disk_write(ext2.drive,ext2.part_lba+b*(ext2.block_size/512),ext2.block_size/512,in);
}
static char inode_kind(uint16_t mode){
 switch(mode&0xF000){case EXT2_S_IFDIR:return 'd';case EXT2_S_IFREG:return 'f';case EXT2_S_IFLNK:return 'l';case EXT2_S_IFCHR:return 'c';case EXT2_S_IFBLK:return 'b';case EXT2_S_IFIFO:return 'p';case EXT2_S_IFSOCK:return 's';default:return '?';}
}
static int inode_get(uint32_t ino,ext2_inode_t *out){
 if(!ext2.mounted||!ino||!out)return -1;uint32_t group=(ino-1)/ext2.inodes_per_group,idx=(ino-1)%ext2.inodes_per_group;
 if(group>=ext2.groups)return -1;uint8_t gdblk[4096];uint32_t per=ext2.block_size/sizeof(ext2_gd_t);
 if(!per||read_block(ext2.group_desc_block+group/per,gdblk)<0)return -1;
 ext2_gd_t *gd=(ext2_gd_t *)(gdblk+(group%per)*sizeof(ext2_gd_t));uint32_t off=idx*ext2.inode_size,block=gd->inode_table+off/ext2.block_size,boff=off%ext2.block_size;
 if(read_block(block,blockbuf)<0)return -1;const uint8_t *raw=blockbuf+boff;
 if(boff+sizeof(ext2_disk_inode_t)>ext2.block_size){uint32_t first=ext2.block_size-boff;kmemcpy(inodebuf,raw,first);if(read_block(block+1,blockbuf)<0)return -1;kmemcpy(inodebuf+first,blockbuf,sizeof(ext2_disk_inode_t)-first);raw=inodebuf;}
 const ext2_disk_inode_t *di=(const ext2_disk_inode_t *)raw;out->inode=ino;out->mode=di->mode;out->size=di->size;out->blocks=di->blocks;out->links_count=di->links_count;kmemcpy(out->block,di->block,sizeof(out->block));return 0;
}
/* Resolve a logical file block using EXT2 direct, single, double and triple indirect pointers. */
static int file_block(const ext2_inode_t *in,uint32_t logical,uint32_t *data){
 uint32_t n=ext2.block_size/4;if(!in||!data||!n)return -1;*data=0;
 if(logical<12){*data=in->block[logical];return 0;}uint64_t p=(uint64_t)logical-12;
 if(p<n){if(!in->block[12])return 0;if(read_block(in->block[12],indbuf1)<0)return -1;*data=u32_at(indbuf1+(uint32_t)p*4);return 0;}
 p-=n;uint64_t n2=(uint64_t)n*n;
 if(p<n2){if(!in->block[13])return 0;if(read_block(in->block[13],indbuf1)<0)return -1;uint32_t a=u32_at(indbuf1+(uint32_t)(p/n)*4);if(!a)return 0;if(read_block(a,indbuf2)<0)return -1;*data=u32_at(indbuf2+(uint32_t)(p%n)*4);return 0;}
 p-=n2;uint64_t n3=n2*n;
 if(p<n3){if(!in->block[14])return 0;if(read_block(in->block[14],indbuf1)<0)return -1;uint32_t a=u32_at(indbuf1+(uint32_t)(p/n2)*4);if(!a)return 0;if(read_block(a,indbuf2)<0)return -1;uint64_t q=p%n2;uint32_t b=u32_at(indbuf2+(uint32_t)(q/n)*4);if(!b)return 0;if(read_block(b,indbuf3)<0)return -1;*data=u32_at(indbuf3+(uint32_t)(q%n)*4);return 0;}
 return -1;
}
static int lookup_in_dir(const ext2_inode_t *dir,const char *name,uint32_t *ino,uint8_t *type){
 if(!dir||(dir->mode&0xF000)!=EXT2_S_IFDIR)return -1;uint32_t count=(dir->size+ext2.block_size-1)/ext2.block_size;
 for(uint32_t bi=0;bi<count;bi++){uint32_t db;if(file_block(dir,bi,&db)<0)return -1;if(!db)continue;if(read_block(db,blockbuf)<0)return -1;uint32_t pos=0;
  while(pos+8<=ext2.block_size){uint32_t n=u32_at(blockbuf+pos);uint16_t rec=(uint16_t)(blockbuf[pos+4]|((uint16_t)blockbuf[pos+5]<<8));uint8_t len=blockbuf[pos+6],ty=blockbuf[pos+7];
   if(!rec||rec<8||pos+rec>ext2.block_size)return -1;if(n&&len&&kstrlen(name)==len&&!kstrncmp((char*)blockbuf+pos+8,name,len)){if(ino)*ino=n;if(type)*type=ty;return 0;}pos+=rec;}
 }return -1;
}
int ext2_mount_partition(int drive,int part){
 mbr_table_t m;uint8_t raw_mbr[512];kmemset(&ext2,0,sizeof(ext2));
 if(drive<0||part<0||part>=4){ext2.last_error="invalid drive or partition index";return -1;}
 if(disk_read_sector(drive,0,raw_mbr)<0){ext2.last_error="ATA read of MBR sector failed";return -1;}
 ext2.mbr_sig0=raw_mbr[510];ext2.mbr_sig1=raw_mbr[511];
 if(raw_mbr[510]!=0x55||raw_mbr[511]!=0xAA){ext2.last_error="MBR signature is not 55 AA";return -1;}
 if(mbr_read(drive,&m)<0||!m.valid||mbr_validate_drive(drive,&m)<0){ext2.last_error="MBR parser or capacity validation rejected table";return -1;}
 if(!m.entries[part].sector_count){ext2.last_error="empty MBR partition";return -1;}
 ext2.drive=drive;ext2.part=part;ext2.part_lba=m.entries[part].lba_start;
 uint8_t sec[512];if(disk_read(drive,ext2.part_lba+2,1,sec)<0){ext2.last_error="cannot read superblock sector";return -1;}ext2_super_t *s=(ext2_super_t*)sec;
 if(s->magic!=EXT2_MAGIC){ext2.last_error="EXT2 magic missing at superblock";return -1;}ext2.block_size=1024u<<s->log_block_size;
 if(ext2.block_size<1024||ext2.block_size>4096||!s->blocks_per_group||!s->inodes_per_group){ext2.last_error="unsupported block or group geometry";return -1;}
 /* inode_get() reads the classic 128-byte inode fields. Refuse malformed
  * short records and records that cannot fit inside the supported block. */
 if(s->inode_size && (s->inode_size<sizeof(ext2_disk_inode_t)||s->inode_size>ext2.block_size)){
  ext2.last_error="unsupported inode record size";return -1;
 }
 /* EXT4 extents are not EXT2 block maps and must never be interpreted as indirect pointers. */
 if(s->feature_incompat&EXT2_INCOMPAT_EXTENTS){ext2.last_error="EXT4 extent format is unsupported";return -1;}
 if (!s->blocks_count || (uint64_t)s->blocks_count*(ext2.block_size/512u)>(uint64_t)m.entries[part].sector_count){
  ext2.last_error="filesystem block range exceeds MBR partition";return -1;
 }
 ext2.blocks_per_group=s->blocks_per_group;ext2.inodes_per_group=s->inodes_per_group;ext2.inode_size=s->inode_size?s->inode_size:128;ext2.group_desc_block=s->first_data_block+1;
 ext2.blocks_count=s->blocks_count;ext2.inodes_count=s->inodes_count;ext2.free_blocks=s->free_blocks_count;ext2.free_inodes=s->free_inodes_count;ext2.feature_incompat=s->feature_incompat;ext2.feature_compat=s->feature_compat;ext2.fs_state=s->state;ext2.fs_errors=s->errors;
 ext2.groups=(s->blocks_count+s->blocks_per_group-1)/s->blocks_per_group;kmemcpy(ext2.volume_name,s->volume_name,16);ext2.volume_name[16]=0;ext2.mounted=1;ext2.last_error="none";return 0;
}
int ext2_unmount(void){kmemset(&ext2,0,sizeof(ext2));ext2.last_error="not mounted";return 0;}
const char *ext2_last_error(void){return ext2.last_error?ext2.last_error:"not mounted";}
int ext2_lookup(const char *path,ext2_inode_t *out,uint8_t *type){
 if(!ext2.mounted||!path||!out)return -1;ext2_inode_t cur;if(inode_get(EXT2_ROOT_INO,&cur)<0)return -1;uint8_t ty=EXT2_FT_DIR;const char *p=path;while(*p=='/')p++;
 while(*p){char n[256];int l=0;while(*p&&*p!='/'&&l<255)n[l++]=*p++;n[l]=0;while(*p=='/')p++;uint32_t ino;if(lookup_in_dir(&cur,n,&ino,&ty)<0||inode_get(ino,&cur)<0)return -1;}
 *out=cur;if(type)*type=ty;return 0;
}
int ext2_stat(const char *path,ext2_inode_t *out,char *type){if(ext2_lookup(path,out,0)<0)return -1;if(type)*type=inode_kind(out->mode);return 0;}
static int list_dir(const ext2_inode_t *dir,char *out,size_t sz,int long_form){
 if(!dir||!out||!sz||(dir->mode&0xF000)!=EXT2_S_IFDIR)return -1;size_t used=0;out[0]=0;uint32_t count=(dir->size+ext2.block_size-1)/ext2.block_size;
 for(uint32_t bi=0;bi<count;bi++){uint32_t db;if(file_block(dir,bi,&db)<0)return -1;if(!db)continue;if(read_block(db,dirbuf)<0)return -1;uint32_t pos=0;
  while(pos+8<=ext2.block_size){uint32_t ino=u32_at(dirbuf+pos);uint16_t rec=(uint16_t)(dirbuf[pos+4]|((uint16_t)dirbuf[pos+5]<<8));uint8_t len=dirbuf[pos+6];if(!rec||rec<8||pos+rec>ext2.block_size)return -1;
   if(ino&&len){char name[256];uint32_t cp=len>255?255:len;kmemcpy(name,dirbuf+pos+8,cp);name[cp]=0;char line[300];int w;
    if(long_form){ext2_inode_t ent;char kind='?';uint32_t bytes=0;if(inode_get(ino,&ent)==0){kind=inode_kind(ent.mode);bytes=ent.size;}w=ksnprintf(line,sizeof(line),"%c %u %s",kind,bytes,name);}else w=ksnprintf(line,sizeof(line),"%s",name);
    if(w>0&&used+(size_t)w+1<sz){kmemcpy(out+used,line,(size_t)w);used+=(size_t)w;out[used++]='\n';out[used]=0;}
   }pos+=rec;}
 }return (int)used;
}
int ext2_readdir(const char *path,char *out,size_t sz){ext2_inode_t dir;if(ext2_lookup(path,&dir,0)<0)return -1;return list_dir(&dir,out,sz,0);}
int ext2_ls_long(const char *path,char *out,size_t sz){ext2_inode_t dir;if(ext2_lookup(path,&dir,0)<0)return -1;return list_dir(&dir,out,sz,1);}
static int inode_read_range(const ext2_inode_t *f,uint32_t offset,uint8_t *out,size_t cap,size_t *got){
 if(got)*got=0;if(!f||!out)return -1;if(offset>=f->size)return 0;size_t done=0;uint32_t bi=offset/ext2.block_size,skip=offset%ext2.block_size;
 while(done<cap&&offset+done<f->size){uint32_t db;if(file_block(f,bi,&db)<0)return -1;if(!db)break;if(read_block(db,blockbuf)<0)return -1;size_t n=ext2.block_size-skip;if(n>f->size-(offset+done))n=f->size-(offset+done);if(n>cap-done)n=cap-done;kmemcpy(out+done,blockbuf+skip,n);done+=n;bi++;skip=0;}
 if(got)*got=done;return 0;
}
static int inode_read_bytes(const ext2_inode_t *f,uint8_t *out,size_t cap,size_t *got){return inode_read_range(f,0,out,cap,got);}
int ext2_readfile(const char *path,uint8_t *out,size_t cap,size_t *got){ext2_inode_t f;if(got)*got=0;if(!out||ext2_lookup(path,&f,0)||(f.mode&0xF000)!=EXT2_S_IFREG)return -1;return inode_read_bytes(&f,out,cap,got);}
int ext2_read_range(const char *path,uint32_t offset,uint8_t *out,size_t cap,size_t *got){ext2_inode_t f;if(got)*got=0;if(!out||ext2_lookup(path,&f,0)||(f.mode&0xF000)!=EXT2_S_IFREG)return -1;return inode_read_range(&f,offset,out,cap,got);}
int ext2_readlink(const char *path,char *out,size_t cap){
 ext2_inode_t f;if(!out||!cap||ext2_lookup(path,&f,0)<0||(f.mode&0xF000)!=EXT2_S_IFLNK)return -1;size_t n=f.size;if(n>=cap)n=cap-1;
 if(f.size<=60)kmemcpy(out,(const uint8_t*)f.block,n);else{size_t got=0;if(inode_read_bytes(&f,(uint8_t*)out,n,&got)<0)return -1;n=got;}out[n]=0;return (int)n;
}
int ext2_set_write_enabled(int enabled){
 if(!ext2.mounted){ext2.last_error="not mounted";return -1;}
 if(!enabled){ext2.write_enabled=0;ext2.last_error="write guard disabled";return 0;}
 if((ext2.fs_state&EXT2_VALID_FS)==0){ext2.last_error="filesystem is not clean";return -1;}
 if(ext2.feature_compat&EXT2_FEATURE_COMPAT_HAS_JOURNAL){ext2.last_error="journaled EXT3/EXT4 volume is write-protected";return -1;}
 ext2.write_enabled=1;ext2.last_error="guarded direct-block writes enabled";return 0;
}
int ext2_write_range(const char *path,uint32_t offset,const uint8_t *data,size_t len,size_t *written){
 ext2_inode_t f;if(written)*written=0;
 if(!ext2.write_enabled){ext2.last_error="write guard is disabled";return -1;}
 if(!path||!data||!len||ext2_lookup(path,&f,0)<0||(f.mode&0xF000)!=EXT2_S_IFREG){ext2.last_error="target is not a regular file";return -1;}
 if(offset>=f.size||len>f.size-offset){ext2.last_error="writes may not grow or truncate files";return -1;}
 uint32_t first=offset/ext2.block_size,last=(uint32_t)((offset+len-1)/ext2.block_size);
 if(last>=12){ext2.last_error="indirect-block writes are intentionally unsupported";return -1;}
 for(uint32_t b=first;b<=last;b++)if(!f.block[b]||f.block[b]>=ext2.blocks_count){ext2.last_error="target has an invalid or sparse direct block";return -1;}
 size_t done=0;while(done<len){uint32_t pos=offset+(uint32_t)done,logical=pos/ext2.block_size,boff=pos%ext2.block_size;size_t n=ext2.block_size-boff;if(n>len-done)n=len-done;
  if(read_block(f.block[logical],blockbuf)<0){ext2.last_error="cannot read target data block";return -1;}kmemcpy(blockbuf+boff,data+done,n);
  if(write_block(f.block[logical],blockbuf)<0){ext2.last_error="ATA write of target data block failed";return -1;}done+=n;
 }
 if(written)*written=done;ext2.last_error="guarded write complete";return 0;
}
void ext2_print_status(void){if(!ext2.mounted){terminal_writeln("ext2: not mounted");return;}kprintf("ext2: drive %d partition %d, %u-byte blocks, %u groups, inode %u bytes, volume '%s' (%s)\n",ext2.drive,ext2.part,ext2.block_size,ext2.groups,ext2.inode_size,ext2.volume_name,ext2.write_enabled?"guarded write":"read-only");}
void ext2_print_info(void){if(!ext2.mounted){terminal_writeln("ext2: not mounted");return;}kprintf("ext2 info: volume '%s', blocks %u (%u free), inodes %u (%u free)\n",ext2.volume_name,ext2.blocks_count,ext2.free_blocks,ext2.inodes_count,ext2.free_inodes);kprintf("           block size %u, groups %u, incompat 0x%x, mode read-only\n",ext2.block_size,ext2.groups,ext2.feature_incompat);}
