#include "image_decode.h"
#include "fileformat.h"
#include "vfs.h"
#include "kmalloc.h"
#include "util.h"

static void *image_stbi_realloc(void *p,size_t oldsz,size_t newsz){
    void *q=kmalloc(newsz);if(!q)return NULL;if(p){kmemcpy(q,p,oldsz<newsz?oldsz:newsz);kfree(p);}return q;
}
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz) kmalloc(sz)
#define STBI_FREE(p) kfree(p)
#define STBI_REALLOC_SIZED(p,oldsz,newsz) image_stbi_realloc(p,oldsz,newsz)
#define STBI_MEMCPY(d,s,n) kmemcpy(d,s,n)
#define STBI_MEMMOVE(d,s,n) kmemmove(d,s,n)
#define STBI_MEMSET(d,c,n) kmemset(d,c,n)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static void image_clear(atm_image_t *o){if(o)kmemset(o,0,sizeof(*o));}
static void image_error(atm_image_t *o,const char *e){if(o){kstrncpy(o->error,e,63);o->error[63]=0;}}

const char *atm_image_format_name(atm_image_format_t f){
    switch(f){case ATM_IMAGE_BMP:return "BMP";case ATM_IMAGE_PNG:return "PNG";case ATM_IMAGE_JPEG:return "JPEG";default:return "Unknown";}
}
static atm_image_format_t image_format(const uint8_t *d,uint32_t n,const char *name){
    file_fmt_t f=fmt_detect(d,n,name);if(f==FMT_BMP)return ATM_IMAGE_BMP;if(f==FMT_PNG)return ATM_IMAGE_PNG;if(f==FMT_JPEG)return ATM_IMAGE_JPEG;return ATM_IMAGE_NONE;
}
int atm_image_decode_memory(const uint8_t *data,uint32_t size,const char *name,atm_image_t *out){
    image_clear(out);if(!out||!data||!size||size>ATM_IMAGE_MAX_FILE_BYTES){image_error(out,"invalid or oversized file");return -1;}
    atm_image_format_t f=image_format(data,size,name);if(f==ATM_IMAGE_NONE){image_error(out,"unsupported image format");return -1;}
    if(f==ATM_IMAGE_BMP){image_error(out,"BMP decode pending viewer migration");return -1;}
    int w=0,h=0,c=0;if(!stbi_info_from_memory(data,(int)size,&w,&h,&c)||w<=0||h<=0||w>ATM_IMAGE_MAX_DIMENSION||h>ATM_IMAGE_MAX_DIMENSION||(uint64_t)w*(uint64_t)h>ATM_IMAGE_MAX_PIXELS){image_error(out,"invalid or oversized dimensions");return -1;}
    stbi_uc *rgba=stbi_load_from_memory(data,(int)size,&w,&h,&c,4);if(!rgba){image_error(out,"decoder rejected image");return -1;}
    out->width=w;out->height=h;out->format=f;out->rgba=rgba;return 0;
}
int atm_image_decode_file(const char *path,atm_image_t *out){
    image_clear(out);if(!path){image_error(out,"path missing");return -1;}int fd=vfs_open(path,O_RDONLY,0);if(fd<0){image_error(out,"file not found");return -1;}
    vfs_stat_t st;if(vfs_fstat(fd,&st)<0||st.st_size==0||st.st_size>ATM_IMAGE_MAX_FILE_BYTES){vfs_close(fd);image_error(out,"file too large");return -1;}
    uint8_t *buf=kmalloc((size_t)st.st_size);if(!buf){vfs_close(fd);image_error(out,"out of memory");return -1;}
    int64_t n=vfs_read(fd,buf,st.st_size);vfs_close(fd);int r=(n==(int64_t)st.st_size)?atm_image_decode_memory(buf,(uint32_t)st.st_size,path,out):-1;if(n!=(int64_t)st.st_size)image_error(out,"short file read");kfree(buf);return r;
}
void atm_image_release(atm_image_t *i){if(i&&i->rgba)stbi_image_free(i->rgba);image_clear(i);}
