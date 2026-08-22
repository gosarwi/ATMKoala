#ifndef ATM_IMAGE_DECODE_H
#define ATM_IMAGE_DECODE_H

#include <stdint.h>

#define ATM_IMAGE_MAX_FILE_BYTES (8u*1024u*1024u)
#define ATM_IMAGE_MAX_PIXELS     (2u*1024u*1024u)
#define ATM_IMAGE_MAX_DIMENSION  4096

typedef enum {
    ATM_IMAGE_NONE=0,
    ATM_IMAGE_BMP,
    ATM_IMAGE_PNG,
    ATM_IMAGE_JPEG,
    ATM_IMAGE_PPM
} atm_image_format_t;

typedef struct {
    int width,height;
    atm_image_format_t format;
    uint8_t *rgba; /* width*height*4, owned by image decoder */
    char error[64];
} atm_image_t;

int  atm_image_decode_memory(const uint8_t *data,uint32_t size,const char *name,atm_image_t *out);
int  atm_image_decode_file(const char *path,atm_image_t *out);
void atm_image_release(atm_image_t *image);
const char *atm_image_format_name(atm_image_format_t format);
/* Bounded memory-decoder regression, including 24-bit BMP ownership path. */
int  atm_image_selftest(void);

#endif
