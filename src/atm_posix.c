#include "atm_posix.h"
#include "catfs.h"
#include "catfs_vfs.h"
#include "sched.h"
#include "util.h"

#define ATM_POSIX_IOV_MAX 16

/* The idle task owns the normal kernel shell, while native scheduled tasks
 * inherit these fields through task_create(). The bootstrap fallback only
 * exists before sched_init() has created idle. */
static char bootstrap_cwd[VFS_PATH_MAX]="/";
static uint32_t bootstrap_umask=0022;

static char *posix_cwd_slot(void){
    task_t *t=sched_current();
    return t?t->cwd:bootstrap_cwd;
}
static uint32_t *posix_umask_slot(void){
    task_t *t=sched_current();
    return t?&t->umask_value:&bootstrap_umask;
}

/* Canonical lexical path resolution. VFS still resolves symlinks itself;
 * this intentionally normalizes only separators, '.' and '..' so every
 * portable wrapper observes the same per-task cwd semantics. */
static int posix_abspath(const char *path,char out[VFS_PATH_MAX]){
    char raw[VFS_PATH_MAX];const char *cwd=posix_cwd_slot();
    if(!path||!path[0])return -1;
    if(path[0]=='/'){
        if(kstrlen(path)>=sizeof(raw))return -1;
        kstrcpy(raw,path);
    }else{
        if(kstrlen(cwd)+1+kstrlen(path)>=sizeof(raw))return -1;
        kstrcpy(raw,cwd);if(raw[kstrlen(raw)-1]!='/')kstrcat(raw,"/");kstrcat(raw,path);
    }
    out[0]='/';out[1]=0;const char *p=raw;
    while(*p){
        while(*p=='/')p++;
        if(!*p)break;
        const char *part=p;while(*p&&*p!='/')p++;
        size_t n=(size_t)(p-part);
        if(n==1&&part[0]=='.')continue;
        if(n==2&&part[0]=='.'&&part[1]=='.'){
            size_t used=kstrlen(out);
            if(used>1){while(used>1&&out[used-1]!='/')used--;if(used>1)used--;out[used]=0;}
            continue;
        }
        size_t used=kstrlen(out);
        if(used>1){if(used+1+n>=VFS_PATH_MAX)return -1;out[used++]='/';}
        else if(used+n>=VFS_PATH_MAX)return -1;
        kmemcpy(out+used,part,n);out[used+n]=0;
    }
    return 0;
}

int atm_posix_open(const char *path,uint32_t flags,uint32_t mode){
    char full[VFS_PATH_MAX];if(posix_abspath(path,full)<0)return -1;
    if(flags&O_CREAT)mode&=~(*posix_umask_slot());
    return vfs_open(full,flags,mode);
}
int atm_posix_creat(const char *path,uint32_t mode){return atm_posix_open(path,O_WRONLY|O_CREAT|O_TRUNC,mode);}
int atm_posix_close(int fd){if(fd<0)return -1;vfs_close(fd);return 0;}
int64_t atm_posix_read(int fd,void *buf,uint64_t count){return vfs_read(fd,buf,count);}
int64_t atm_posix_write(int fd,const void *buf,uint64_t count){return vfs_write(fd,buf,count);}

static int posix_seek_to(int fd,uint64_t off,int64_t *saved){
    if(off>0x7FFFFFFFFFFFFFFFULL)return -1;
    *saved=vfs_lseek(fd,0,SEEK_CUR);if(*saved<0)return -1;
    if(vfs_lseek(fd,(int64_t)off,SEEK_SET)<0)return -1;
    return 0;
}
int64_t atm_posix_pread(int fd,void *buf,uint64_t count,uint64_t offset){
    int64_t saved;if(posix_seek_to(fd,offset,&saved)<0)return -1;
    int64_t n=vfs_read(fd,buf,count);(void)vfs_lseek(fd,saved,SEEK_SET);return n;
}
int64_t atm_posix_pwrite(int fd,const void *buf,uint64_t count,uint64_t offset){
    int64_t saved;if(posix_seek_to(fd,offset,&saved)<0)return -1;
    int64_t n=vfs_write(fd,buf,count);(void)vfs_lseek(fd,saved,SEEK_SET);return n;
}
int64_t atm_posix_readv(int fd,const atm_posix_iovec_t *iov,int iovcnt){
    if(!iov||iovcnt<0||iovcnt>ATM_POSIX_IOV_MAX)return -1;
    int64_t total=0;for(int i=0;i<iovcnt;i++){if(iov[i].iov_len&& !iov[i].iov_base)return total?total:-1;int64_t n=vfs_read(fd,iov[i].iov_base,iov[i].iov_len);if(n<0)return total?total:-1;total+=n;if((uint64_t)n<iov[i].iov_len)break;}return total;
}
int64_t atm_posix_writev(int fd,const atm_posix_iovec_t *iov,int iovcnt){
    if(!iov||iovcnt<0||iovcnt>ATM_POSIX_IOV_MAX)return -1;
    int64_t total=0;for(int i=0;i<iovcnt;i++){if(iov[i].iov_len&& !iov[i].iov_base)return total?total:-1;int64_t n=vfs_write(fd,iov[i].iov_base,iov[i].iov_len);if(n<0)return total?total:-1;total+=n;if((uint64_t)n<iov[i].iov_len)break;}return total;
}
int atm_posix_fsync(int fd){vfs_stat_t st;if(vfs_fstat(fd,&st)<0)return -1;return catfs_vfs_is_mounted()?catfs_sync():0;}
int atm_posix_fdatasync(int fd){return atm_posix_fsync(fd);}
int64_t atm_posix_lseek(int fd,int64_t offset,int whence){return vfs_lseek(fd,offset,whence);}

int atm_posix_stat(const char *path,atm_posix_stat_t *st){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_stat(full,st);}
int atm_posix_lstat(const char *path,atm_posix_stat_t *st){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_lstat(full,st);}
int atm_posix_fstat(int fd,atm_posix_stat_t *st){return vfs_fstat(fd,st);}
int atm_posix_dup(int fd){return vfs_dup(fd);}
int atm_posix_dup2(int fd,int newfd){return vfs_dup2(fd,newfd);}
int atm_posix_truncate(const char *path,uint64_t size){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_truncate(full,size);}
int atm_posix_ftruncate(int fd,uint64_t size){return vfs_ftruncate(fd,size);}
int atm_posix_chmod(const char *path,uint32_t mode){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_chmod(full,mode);}
int atm_posix_chown(const char *path,uint32_t uid,uint32_t gid){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_chown(full,uid,gid);}
int atm_posix_mkdir(const char *path,uint32_t mode){char full[VFS_PATH_MAX];if(posix_abspath(path,full)<0)return -1;return vfs_mkdir(full,mode&~(*posix_umask_slot()));}
int atm_posix_rmdir(const char *path){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_rmdir(full);}
int atm_posix_unlink(const char *path){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_unlink(full);}
int atm_posix_chdir(const char *path){char full[VFS_PATH_MAX];vfs_stat_t st;if(posix_abspath(path,full)<0||vfs_stat(full,&st)<0||!S_ISDIR(st.st_mode))return -1;kstrcpy(posix_cwd_slot(),full);return 0;}
char *atm_posix_getcwd(char *buf,size_t size){char *cwd=posix_cwd_slot();if(!buf||size<=kstrlen(cwd))return NULL;kstrcpy(buf,cwd);return buf;}
int atm_posix_access(const char *path,int mode){char full[VFS_PATH_MAX];vfs_stat_t st;if(posix_abspath(path,full)<0||vfs_stat(full,&st)<0)return -1;if(mode==ATM_POSIX_F_OK)return 0;uint32_t perm=(vfs_current_uid()==st.st_uid)?((st.st_mode>>6)&7):((vfs_current_gid()==st.st_gid)?((st.st_mode>>3)&7):(st.st_mode&7));if((mode&ATM_POSIX_R_OK)&&!(perm&4))return -1;if((mode&ATM_POSIX_W_OK)&&!(perm&2))return -1;if((mode&ATM_POSIX_X_OK)&&!(perm&1))return -1;return 0;}
uint32_t atm_posix_umask(uint32_t newmask){uint32_t *slot=posix_umask_slot(),old=*slot;*slot=newmask&0777;return old;}
int atm_posix_isatty(int fd){return fd>=0&&fd<=2;}
atm_posix_dir_t *atm_posix_opendir(const char *path){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?NULL:vfs_opendir(full);}
atm_posix_dirent_t *atm_posix_readdir(atm_posix_dir_t *dir){return vfs_readdir_next(dir);}
int atm_posix_closedir(atm_posix_dir_t *dir){if(!dir)return -1;vfs_closedir(dir);return 0;}
int atm_posix_rename(const char *oldpath,const char *newpath){char a[VFS_PATH_MAX],b[VFS_PATH_MAX];return posix_abspath(oldpath,a)<0||posix_abspath(newpath,b)<0?-1:vfs_rename(a,b);}
int atm_posix_link(const char *oldpath,const char *newpath){char a[VFS_PATH_MAX],b[VFS_PATH_MAX];return posix_abspath(oldpath,a)<0||posix_abspath(newpath,b)<0?-1:vfs_link(a,b);}
int atm_posix_symlink(const char *target,const char *linkpath){char full[VFS_PATH_MAX];return posix_abspath(linkpath,full)<0?-1:vfs_symlink(target,full);}
int atm_posix_readlink(const char *path,char *buf,size_t size){char full[VFS_PATH_MAX];return posix_abspath(path,full)<0?-1:vfs_readlink(full,buf,size);}
uint32_t atm_posix_getuid(void){return vfs_current_uid();}
uint32_t atm_posix_getgid(void){return vfs_current_gid();}
uint32_t atm_posix_getpid(void){task_t *t=sched_current();return t?t->pid:0;}
uint32_t atm_posix_getppid(void){task_t *t=sched_current();return t?t->ppid:0;}

int atm_posix_selftest(void){
    char saved[VFS_PATH_MAX],cwd[VFS_PATH_MAX],readback[8]={0};
    if(!atm_posix_getcwd(saved,sizeof(saved)))return -1;
    uint32_t oldmask=atm_posix_umask(0022);int rc=-1,fd=-1;const char *root="/tmp/.atm-posix-test";
    (void)vfs_mkdir(root,0700);if(atm_posix_chdir(root)<0)goto done;
    if(atm_posix_mkdir("nest",0777)<0)goto done;
    if(atm_posix_chdir("nest/../.")<0||!atm_posix_getcwd(cwd,sizeof(cwd))||kstrcmp(cwd,root))goto done;
    fd=atm_posix_creat("io",0666);if(fd<0)goto done;
    atm_posix_iovec_t out[2]={{(void *)"o",1},{(void *)"k!",2}};
    if(atm_posix_writev(fd,out,2)!=3||atm_posix_fsync(fd)<0)goto done;
    atm_posix_close(fd);fd=atm_posix_open("io",O_RDWR,0);if(fd<0)goto done;
    if(atm_posix_pread(fd,readback,2,1)!=2||readback[0]!='k'||readback[1]!='!')goto done;
    if(atm_posix_lseek(fd,0,SEEK_SET)<0)goto done;
    atm_posix_iovec_t in[2]={{readback,1},{readback+1,2}};
    if(atm_posix_readv(fd,in,2)!=3||readback[0]!='o'||readback[1]!='k'||readback[2]!='!')goto done;
    atm_posix_close(fd);fd=-1;
    if(atm_posix_access("io",ATM_POSIX_R_OK)<0)goto done;
    atm_posix_dir_t *d=atm_posix_opendir(".");if(!d)goto done;int saw=0;atm_posix_dirent_t *e;while((e=atm_posix_readdir(d)))if(!kstrcmp(e->name,"io")){saw=1;break;}atm_posix_closedir(d);if(!saw)goto done;
    rc=0;
done:
    if(fd>=0)atm_posix_close(fd);(void)atm_posix_unlink("io");(void)atm_posix_chdir(saved);(void)vfs_rmdir("/tmp/.atm-posix-test/nest");(void)vfs_rmdir(root);atm_posix_umask(oldmask);return rc;
}

uint32_t atm_posix_features(void){
    return ATM_POSIX_FILES|ATM_POSIX_UIDGID|ATM_POSIX_PATHS|ATM_POSIX_FD|
           ATM_POSIX_META|ATM_POSIX_LINKS|ATM_POSIX_TRUNC|ATM_POSIX_DIR|
           ATM_POSIX_CWD|ATM_POSIX_ACCESS|ATM_POSIX_TTY|ATM_POSIX_IOV|
           ATM_POSIX_SYNC|ATM_POSIX_TASKCTX;
}
