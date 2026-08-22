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
    switch(f){case ATM_IMAGE_BMP:return "BMP";case ATM_IMAGE_PNG:return "PNG";case ATM_IMAGE_JPEG:return "JPEG";case ATM_IMAGE_PPM:return "PPM P6";default:return "Unknown";}
}
static atm_image_format_t image_format(const uint8_t *d,uint32_t n,const char *name){
    file_fmt_t f=fmt_detect(d,n,name);if(f==FMT_BMP)return ATM_IMAGE_BMP;if(f==FMT_PNG)return ATM_IMAGE_PNG;if(f==FMT_JPEG)return ATM_IMAGE_JPEG;if(f==FMT_PPM)return ATM_IMAGE_PPM;return ATM_IMAGE_NONE;
}
static uint16_t image_u16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t image_u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int image_ppm_space(uint8_t c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static void image_ppm_skip_header(const uint8_t *d,uint32_t n,uint32_t *pos){while(*pos<n){if(image_ppm_space(d[*pos])){(*pos)++;continue;}if(d[*pos]=='#'){while(*pos<n&&d[*pos]!='\n'&&d[*pos]!='\r')(*pos)++;continue;}break;}}
static int image_ppm_number(const uint8_t *d,uint32_t n,uint32_t *pos,uint32_t *out){uint32_t value=0;image_ppm_skip_header(d,n,pos);if(*pos>=n||d[*pos]<'0'||d[*pos]>'9')return -1;while(*pos<n&&d[*pos]>='0'&&d[*pos]<='9'){if(value>429496729u)return -1;value=value*10u+(uint32_t)(d[*pos]-'0');(*pos)++;}*out=value;return 0;}
static int image_decode_ppm(const uint8_t *data,uint32_t size,atm_image_t *out){uint32_t pos=2,w=0,h=0,maxval=0;if(size<4||data[0]!='P'||data[1]!='6'||image_ppm_number(data,size,&pos,&w)<0||image_ppm_number(data,size,&pos,&h)<0||image_ppm_number(data,size,&pos,&maxval)<0){image_error(out,"invalid PPM P6 header");return -1;}if(!w||!h||maxval!=255||w>ATM_IMAGE_MAX_DIMENSION||h>ATM_IMAGE_MAX_DIMENSION||(uint64_t)w*(uint64_t)h>ATM_IMAGE_MAX_PIXELS){image_error(out,"unsupported PPM dimensions/range");return -1;}if(pos>=size||!image_ppm_space(data[pos])){image_error(out,"missing PPM pixel separator");return -1;}pos++;uint64_t pixels=(uint64_t)w*(uint64_t)h,bytes=pixels*3u;if(bytes>(uint64_t)size-pos){image_error(out,"truncated PPM pixels");return -1;}uint8_t *rgba=(uint8_t*)kmalloc((size_t)pixels*4u);if(!rgba){image_error(out,"out of memory");return -1;}for(uint64_t i=0;i<pixels;i++){rgba[i*4u]=data[pos+i*3u];rgba[i*4u+1]=data[pos+i*3u+1];rgba[i*4u+2]=data[pos+i*3u+2];rgba[i*4u+3]=255;}out->width=(int)w;out->height=(int)h;out->format=ATM_IMAGE_PPM;out->rgba=rgba;return 0;}
static int image_decode_bmp(const uint8_t *data,uint32_t size,atm_image_t *out){
    if(size<54||image_u16(data)!=0x4D42||image_u32(data+14)<40||image_u16(data+26)!=1){image_error(out,"invalid BMP header");return -1;}
    int32_t sw=(int32_t)image_u32(data+18),sh=(int32_t)image_u32(data+22);
    uint16_t bpp=image_u16(data+28);uint32_t compression=image_u32(data+30),offset=image_u32(data+10);
    if(sw<=0||sh==0||bpp!=24&&bpp!=32||compression!=0){image_error(out,"unsupported BMP encoding");return -1;}
    int h=sh<0?-sh:sh,w=sw;
    if(w>ATM_IMAGE_MAX_DIMENSION||h>ATM_IMAGE_MAX_DIMENSION||(uint64_t)w*(uint64_t)h>ATM_IMAGE_MAX_PIXELS){image_error(out,"invalid or oversized dimensions");return -1;}
    uint64_t stride=(((uint64_t)w*(uint64_t)(bpp/8u))+3u)&~3u;
    if(offset>size||stride>(uint64_t)size-offset||(uint64_t)h*stride>(uint64_t)size-offset){image_error(out,"truncated BMP pixels");return -1;}
    uint8_t *rgba=(uint8_t*)kmalloc((size_t)w*(size_t)h*4u);if(!rgba){image_error(out,"out of memory");return -1;}
    for(int y=0;y<h;y++){
        int src_y=sh>0?h-1-y:y;const uint8_t *row=data+offset+(uint64_t)src_y*stride;
        for(int x=0;x<w;x++){const uint8_t *p=row+(uint32_t)x*(bpp/8u);uint8_t *q=rgba+((uint32_t)y*(uint32_t)w+(uint32_t)x)*4u;q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=bpp==32?p[3]:255;}
    }
    out->width=w;out->height=h;out->format=ATM_IMAGE_BMP;out->rgba=rgba;return 0;
}
int atm_image_decode_memory(const uint8_t *data,uint32_t size,const char *name,atm_image_t *out){
    image_clear(out);if(!out||!data||!size||size>ATM_IMAGE_MAX_FILE_BYTES){image_error(out,"invalid or oversized file");return -1;}
    atm_image_format_t f=image_format(data,size,name);    if(f==ATM_IMAGE_NONE){image_error(out,"unsupported image format");return -1;}
    if(f==ATM_IMAGE_BMP)return image_decode_bmp(data,size,out);
    if(f==ATM_IMAGE_PPM)return image_decode_ppm(data,size,out);

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

int atm_image_selftest(void){
    static const uint8_t bmp_1x1[58]={
        0x42,0x4D,0x3A,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,
        0x28,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x18,0x00,
        0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x33,0x22,0x11,0x00
    };
    static const uint8_t ppm_2x1[]={'P','6','\n','#','x','\n','2',' ','1','\n','2','5','5','\n',0x11,0x22,0x33,0x44,0x55,0x66};
    atm_image_t image;
    if(atm_image_decode_memory(bmp_1x1,sizeof(bmp_1x1),"fixture.bmp",&image)<0)return -1;
    int ok=image.format==ATM_IMAGE_BMP&&image.width==1&&image.height==1&&image.rgba&&image.rgba[0]==0x11&&image.rgba[1]==0x22&&image.rgba[2]==0x33&&image.rgba[3]==255;
    atm_image_release(&image);if(!ok||atm_image_decode_memory(ppm_2x1,sizeof(ppm_2x1),"fixture.ppm",&image)<0)return -1;
    ok=image.format==ATM_IMAGE_PPM&&image.width==2&&image.height==1&&image.rgba&&image.rgba[0]==0x11&&image.rgba[5]==0x55&&image.rgba[7]==255;atm_image_release(&image);
    return ok?0:-1;
}
