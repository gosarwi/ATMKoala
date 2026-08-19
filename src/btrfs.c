#include "btrfs.h"
#include "disk.h"
#include "partmgr.h"
#include "util.h"
#include "vga.h"
#include <stdint.h>

#define BTRFS_MIRROR_0 0x0000000000010000ULL
#define BTRFS_MIRROR_1 0x0000000004000000ULL
#define BTRFS_MIRROR_2 0x0000004000000000ULL

btrfs_state_t btrfs={0};
static uint8_t sb[BTRFS_SUPER_SIZE];
/* Metadata transactions must not reserve 12+ KiB from the kernel stack. */
static uint8_t tx_sb[BTRFS_MIRROR_MAX][BTRFS_SUPER_SIZE];
static uint8_t tx_verify[BTRFS_SUPER_SIZE];
static const char *btrfs_err="not run";
static uint16_t le16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t le64(const uint8_t *p){return (uint64_t)le32(p)|((uint64_t)le32(p+4)<<32);}
static void put_le32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void put_le64(uint8_t *p,uint64_t v){put_le32(p,(uint32_t)v);put_le32(p+4,(uint32_t)(v>>32));}
static int pow2(uint32_t v){return v&&!(v&(v-1u));}
/* Btrfs' default checksum is CRC32C (Castagnoli), initial seed 0xffffffff.
 * Btrfs stores the little-endian digest in the first four checksum bytes. */
static uint32_t crc32c_btrfs(const uint8_t *p,uint32_t len){uint32_t c=0xFFFFFFFFu;while(len--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^((c&1u)?0x82F63B78u:0u);}return c^0xFFFFFFFFu;}
static int label_copy(char *out,const uint8_t *src){int n=0;for(;n<256&&src[n];n++)out[n]=(char)src[n];out[n]=0;return n;}
static int geometry_ok(const uint8_t *p){uint32_t sec=le32(p+0x90),node=le32(p+0x94);return pow2(sec)&&pow2(node)&&sec>=512u&&sec<=65536u&&node>=sec&&node<=65536u;}
static int better_mirror(const btrfs_mirror_t *cand,const btrfs_mirror_t *best){
    if(!best->present)return 1;
    if(cand->csum_ok!=best->csum_ok)return cand->csum_ok>best->csum_ok;
    return cand->generation>best->generation;
}
static void copy_selected(const uint8_t *p,int idx){
    btrfs.selected_mirror=idx;btrfs.bytenr=le64(p+0x30);btrfs.generation=le64(p+0x48);
    btrfs.total_bytes=le64(p+0x70);btrfs.bytes_used=le64(p+0x78);btrfs.num_devices=le64(p+0x88);
    btrfs.sectorsize=le32(p+0x90);btrfs.nodesize=le32(p+0x94);btrfs.csum_type=le16(p+0xC4);
    kmemcpy(btrfs.fsid,p+0x20,16);label_copy(btrfs.label,p+0x12B);
}

int btrfs_probe_partition(int drive,int part){
    static const uint64_t offsets[BTRFS_MIRROR_MAX]={BTRFS_MIRROR_0,BTRFS_MIRROR_1,BTRFS_MIRROR_2};
    mbr_table_t m;if(drive<0||part<0||part>=4||mbr_read(drive,&m)<0||!m.valid||!m.entries[part].sector_count)return -1;
    kmemset(&btrfs,0,sizeof(btrfs));btrfs.drive=drive;btrfs.part=part;btrfs.part_lba=m.entries[part].lba_start;btrfs.selected_mirror=-1;
    uint64_t part_bytes=(uint64_t)m.entries[part].sector_count*SECTOR_SIZE;int best=-1;uint8_t best_sb[BTRFS_SUPER_SIZE];
    for(int i=0;i<BTRFS_MIRROR_MAX;i++){
        uint64_t off=offsets[i];btrfs_mirror_t *mi=&btrfs.mirrors[i];mi->offset=off;
        if(off+BTRFS_SUPER_SIZE>part_bytes)continue;
        uint64_t lba64=(uint64_t)m.entries[part].lba_start+off/SECTOR_SIZE;
        if(lba64>0xFFFFFFFFu||disk_read(drive,(uint32_t)lba64,BTRFS_SUPER_SIZE/SECTOR_SIZE,sb)<0)continue;
        mi->present=1;mi->magic_ok=kmemcmp(sb+0x40,"_BHRfS_M",8)==0;mi->bytenr=le64(sb+0x30);mi->generation=le64(sb+0x48);mi->geometry_ok=geometry_ok(sb);
        /* For legacy synthetic inspection images the checksum may be zero. It
         * remains inspectable, but is never marked trusted or preferred to a
         * checksum-valid mirror. */
        mi->csum_ok=(le16(sb+0xC4)==0&&le32(sb)==crc32c_btrfs(sb+0x20,BTRFS_SUPER_SIZE-0x20));
        btrfs.mirror_count++;
        if(!mi->magic_ok||!mi->geometry_ok||mi->bytenr!=off)continue;
        if(best<0||better_mirror(mi,&btrfs.mirrors[best])){best=i;kmemcpy(best_sb,sb,sizeof(best_sb));}
    }
    if(best<0)return -1;
    copy_selected(best_sb,best);btrfs.valid=1;btrfs.trusted=btrfs.mirrors[best].csum_ok;return 0;
}
void btrfs_clear(void){kmemset(&btrfs,0,sizeof(btrfs));btrfs.selected_mirror=-1;}
const char *btrfs_write_policy(void){return "label metadata transaction available; general files, trees and extents remain unsupported";}
const char *btrfs_last_error(void){return btrfs_err;}
int btrfs_set_write_enabled(int enabled){
    if(!enabled){btrfs.write_enabled=0;return 0;}
    if(!btrfs.valid||!btrfs.trusted||btrfs.csum_type!=0)return -1;
    btrfs.write_enabled=1;return 0;
}
static int label_ok(const char *label){
    int n=0;if(!label||!*label)return 0;
    while(label[n]){unsigned char c=(unsigned char)label[n];if(c<' '||c=='/'||c=='\\'||n>=255)return 0;n++;}
    return 1;
}
static int read_super_at(uint64_t off,uint8_t *out){
    uint64_t lba=(uint64_t)btrfs.part_lba+off/SECTOR_SIZE;
    if(lba>0xFFFFFFFFu)return -1;
    return disk_read(btrfs.drive,(uint32_t)lba,BTRFS_SUPER_SIZE/SECTOR_SIZE,out);
}
static int write_super_at(uint64_t off,const uint8_t *in){
    uint64_t lba=(uint64_t)btrfs.part_lba+off/SECTOR_SIZE;
    if(lba>0xFFFFFFFFu)return -1;
    return disk_write(btrfs.drive,(uint32_t)lba,BTRFS_SUPER_SIZE/SECTOR_SIZE,in);
}
static int super_valid_at(const uint8_t *p,uint64_t off){
    return kmemcmp(p+0x40,"_BHRfS_M",8)==0 && geometry_ok(p) && le64(p+0x30)==off && le16(p+0xC4)==0 && le32(p)==crc32c_btrfs(p+0x20,BTRFS_SUPER_SIZE-0x20);
}
int btrfs_label_set(const char *label){
    int usable[BTRFS_MIRROR_MAX],n=0; uint64_t next_gen;
    btrfs_err="invalid label or write capability disabled";
    if(!btrfs.write_enabled||!btrfs.valid||!btrfs.trusted||!label_ok(label))return -1;
    /* Preflight every selected trusted mirror before touching the media. */
    for(int i=0;i<BTRFS_MIRROR_MAX;i++){
        btrfs_mirror_t *m=&btrfs.mirrors[i];usable[i]=0;
        if(!m->present||!m->csum_ok)continue;
        if(read_super_at(m->offset,tx_sb[i])<0){btrfs_err="preflight read failed";return -1;}
        if(!super_valid_at(tx_sb[i],m->offset)){btrfs_err="preflight superblock verification failed";return -1;}
        usable[i]=1;n++;
    }
    if(!n){btrfs_err="no trusted mirror available";return -1;}
    next_gen=btrfs.generation+1;
    for(int i=0;i<BTRFS_MIRROR_MAX;i++)if(usable[i]){
        kmemset(tx_sb[i],0,32);put_le64(tx_sb[i]+0x48,next_gen);
        kmemset(tx_sb[i]+0x12B,0,256);kstrcpy((char*)tx_sb[i]+0x12B,label);
        put_le32(tx_sb[i],crc32c_btrfs(tx_sb[i]+0x20,BTRFS_SUPER_SIZE-0x20));
    }
    /* Secondary mirrors first; primary last is the recoverable commit point. */
    for(int i=BTRFS_MIRROR_MAX-1;i>=0;i--)if(usable[i]&&i!=0)if(write_super_at(btrfs.mirrors[i].offset,tx_sb[i])<0){btrfs_err="secondary mirror write failed";return -1;}
    if(usable[0]&&write_super_at(btrfs.mirrors[0].offset,tx_sb[0])<0){btrfs_err="primary mirror write failed";return -1;}
    for(int i=0;i<BTRFS_MIRROR_MAX;i++)if(usable[i]){
        if(read_super_at(btrfs.mirrors[i].offset,tx_verify)<0){btrfs_err="post-write read failed";return -1;}
        if(!super_valid_at(tx_verify,btrfs.mirrors[i].offset)){btrfs_err="post-write checksum verification failed";return -1;}
        if(le64(tx_verify+0x48)!=next_gen){btrfs_err="post-write generation mismatch";return -1;}
        if(kstrcmp((char*)tx_verify+0x12B,label)){btrfs_err="post-write label mismatch";return -1;}
        btrfs.mirrors[i].generation=next_gen;
    }
    btrfs.generation=next_gen;label_copy(btrfs.label,(const uint8_t*)label);
    btrfs_err="ok";return 0;
}
static void uuid_string(char *id,const uint8_t *u){int p=0;static const char x[]="0123456789abcdef";for(int i=0;i<16;i++){id[p++]=x[u[i]>>4];id[p++]=x[u[i]&15];if(i==3||i==5||i==7||i==9)id[p++]='-';}id[p]=0;}
void btrfs_print_status(void){
    if(!btrfs.valid){terminal_writeln("btrfs: no valid superblock selected");return;}char id[48];uuid_string(id,btrfs.fsid);
    kprintf("btrfs: drive %d partition %d, selected mirror %d, generation %u, devices %u\n",btrfs.drive,btrfs.part,btrfs.selected_mirror,(uint32_t)btrfs.generation,(uint32_t)btrfs.num_devices);
    kprintf("       label '%s', sector %u node %u, total %u MiB, used %u MiB, fsid %s\n",btrfs.label,btrfs.sectorsize,btrfs.nodesize,(uint32_t)(btrfs.total_bytes>>20),(uint32_t)(btrfs.bytes_used>>20),id);
    kprintf("       integrity: %s; write mode: %s; file/tree writes unsupported\n",btrfs.trusted?"CRC32C verified":"UNVERIFIED synthetic/invalid checksum",btrfs.write_enabled?"label transaction enabled":"disabled");
    for(int i=0;i<BTRFS_MIRROR_MAX;i++){btrfs_mirror_t*m=&btrfs.mirrors[i];if(!m->offset)continue;kprintf("       mirror %d @%u MiB: %s magic=%s crc32c=%s generation=%u\n",i,(uint32_t)(m->offset>>20),m->present?"read":"unavailable",m->magic_ok?"ok":"bad",m->csum_ok?"ok":"bad",(uint32_t)m->generation);}
}
