#include "image_fixtures.h"
#include "image_fixture_data.h"
#include "vfs.h"

static void seed_one(const char *path,const unsigned char *data,uint32_t size){
    vfs_stat_t st;
    if(vfs_stat(path,&st)==0)return;
    int fd=vfs_open(path,O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd<0)return;
    if(vfs_write(fd,data,size)!=(int64_t)size)vfs_unlink(path);
    vfs_close(fd);
}

void image_fixtures_seed(void){
    (void)vfs_mkdir("/home",0755);
    seed_one("/home/exp-sample.png",atm_fixture_png,ATM_FIXTURE_PNG_LEN);
    seed_one("/home/exp-sample.jpg",atm_fixture_jpeg,ATM_FIXTURE_JPEG_LEN);
}
