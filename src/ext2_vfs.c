#include "ext2_vfs.h"
#include "ext2.h"
#include "vfs.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

#define EXT2_VFS_MAX_NODES 64
#define EXT2_VFS_PINNED 0x40000000

typedef struct { vfs_inode_t ino; char path[VFS_PATH_MAX]; int used; } ext2_vnode_t;
static ext2_vnode_t nodes[EXT2_VFS_MAX_NODES];
static int active;
static char mounted_path[VFS_PATH_MAX];
static vfs_fsops_t ops;

static ext2_vnode_t *node_of(vfs_inode_t *n){ return n?(ext2_vnode_t *)n->priv:NULL; }
static uint32_t mode_for(const ext2_inode_t *in){ return (uint32_t)in->mode; }
static uint8_t dtype_for(uint16_t mode){uint16_t k=mode&0xF000;return k==EXT2_S_IFDIR?DT_DIR:(k==EXT2_S_IFLNK?DT_LNK:DT_REG);}
static ext2_vnode_t *get_node(const char *path){
    ext2_inode_t in;char ty; if(!active||!path||ext2_stat(path,&in,&ty)<0)return NULL;
    for(int i=0;i<EXT2_VFS_MAX_NODES;i++)if(nodes[i].used&&!kstrcmp(nodes[i].path,path))return &nodes[i];
    for(int i=0;i<EXT2_VFS_MAX_NODES;i++)if(!nodes[i].used){ext2_vnode_t *v=&nodes[i];kmemset(v,0,sizeof(*v));v->used=1;kstrncpy(v->path,path,VFS_PATH_MAX-1);v->ino.ino=in.inode;v->ino.mode=mode_for(&in);v->ino.nlink=in.links_count;v->ino.size=in.size;v->ino.ops=&ops;v->ino.priv=v;v->ino.refcount=EXT2_VFS_PINNED;return v;}return NULL;
}
static int child_path(const char *base,const char *name,char *out){if(!base||!name||!out||!name[0]||kstrlen(name)>VFS_NAME_MAX)return -1;if(!kstrcmp(base,"/"))ksnprintf(out,VFS_PATH_MAX,"/%s",name);else ksnprintf(out,VFS_PATH_MAX,"%s/%s",base,name);return 0;}
static int lookup(vfs_inode_t *dir,const char *name,vfs_inode_t **out){ext2_vnode_t*v=node_of(dir);if(!v||!out||!S_ISDIR(dir->mode))return -ENOTDIR;if(!kstrcmp(name,".")){*out=dir;return 0;}char p[VFS_PATH_MAX];if(child_path(v->path,name,p)<0)return -ENAMETOOLONG;ext2_vnode_t*n=get_node(p);if(!n)return -ENOENT;*out=&n->ino;return 0;}
static int readdir(vfs_inode_t *dir,uint32_t index,vfs_dirent_t *out){ext2_vnode_t*v=node_of(dir);char list[4096];if(!v||!out||!S_ISDIR(dir->mode))return -ENOTDIR;int n=ext2_readdir(v->path,list,sizeof(list));if(n<0)return -ENOENT;char *p=list;uint32_t cur=0;while(*p){char name[VFS_NAME_MAX+1];int l=0;while(*p&&*p!='\n'&&l<VFS_NAME_MAX)name[l++]=*p++;name[l]=0;if(*p=='\n')p++;if(cur++!=index)continue;char full[VFS_PATH_MAX];ext2_vnode_t*ch=NULL;if(child_path(v->path,name,full)==0)ch=get_node(full);if(!ch)return -ENOENT;kmemset(out,0,sizeof(*out));out->ino=ch->ino.ino;out->inode=(uint32_t)ch->ino.ino;out->d_type=out->type=dtype_for(ch->ino.mode);kstrncpy(out->name,name,VFS_NAME_MAX);return 0;}return -ENOENT;}
static int64_t readop(vfs_inode_t *ino,uint64_t off,void *buf,uint64_t sz){ext2_vnode_t*v=node_of(ino);size_t got=0;if(!v||off>0xFFFFFFFFu||sz>0xFFFFFFFFu)return -EINVAL;if(ext2_read_range(v->path,(uint32_t)off,buf,(size_t)sz,&got)<0)return -EACCES;return (int64_t)got;}
/* This bridge deliberately delegates every policy decision to ext2_write_range:
 * it permits only the existing clean/non-journaled direct-block guard and never
 * allocates, grows, truncates, or changes Ext2 metadata. */
static int64_t writeop(vfs_inode_t *ino,uint64_t off,const void *buf,uint64_t sz){ext2_vnode_t*v=node_of(ino);size_t wrote=0;if(!v||!buf||!S_ISREG(ino->mode)||off>0xFFFFFFFFu||sz>0xFFFFFFFFu)return -EINVAL;if(ext2_write_range(v->path,(uint32_t)off,(const uint8_t*)buf,(size_t)sz,&wrote)<0)return -EACCES;return (int64_t)wrote;}
static int readlinkop(vfs_inode_t *ino,char *buf,size_t bufsz){ext2_vnode_t*v=node_of(ino);return !v||!S_ISLNK(ino->mode)?-EINVAL:ext2_readlink(v->path,buf,bufsz);}
static int statop(vfs_inode_t *ino,vfs_stat_t *st){if(!ino||!st)return -EINVAL;kmemset(st,0,sizeof(*st));st->st_ino=ino->ino;st->st_mode=ino->mode;st->st_nlink=ino->nlink;st->st_size=ino->size;st->st_blksize=ext2.block_size;st->inode=(uint32_t)ino->ino;st->size=(uint32_t)ino->size;st->type=ino->mode&S_IFMT;return 0;}
static vfs_fsops_t ops={.lookup=lookup,.readlink=readlinkop,.readdir=readdir,.read=readop,.write=writeop,.stat=statop};
int ext2_vfs_mount(const char *path){
    if(active||!ext2.mounted||!path||!path[0])return -1;
    kmemset(nodes,0,sizeof(nodes));
    /* get_node() intentionally rejects inactive adapters, so establish the
     * internal lookup state before asking it for the Ext2 root. Roll back on
     * every mount failure: no half-attached VFS adapter may remain visible. */
    active=1;ext2_vnode_t*root=get_node("/");
    if(!root||vfs_mount(path,&root->ino)<0){active=0;kmemset(nodes,0,sizeof(nodes));return -1;}
    kstrncpy(mounted_path,path,VFS_PATH_MAX-1);return 0;
}
int ext2_vfs_unmount(void){
    if(!active)return 0;
    /* Nodes are static and intentionally pinned while mounted; clearing them
     * below an open descriptor would leave a stale VFS object. */
    if(vfs_fsops_busy(&ops))return -EBUSY;
    int r=vfs_unmount(mounted_path);
    if(r<0)return r;
    active=0;mounted_path[0]=0;kmemset(nodes,0,sizeof(nodes));return 0;
}
int ext2_vfs_is_mounted(void){return active;}
int ext2_vfs_is_busy(void){return active&&vfs_fsops_busy(&ops);}
