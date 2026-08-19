#ifndef ATM_TINYGL_LITE_H
#define ATM_TINYGL_LITE_H

#include <stdint.h>
#include "vbe.h"

/* TinyGL-Lite is a compact fixed-point, OpenGL-inspired software renderer.
 * It deliberately uses no floating point, libc, GPU or Linux ABI. */
#define TGL_FP_SHIFT 14
#define TGL_FP_ONE   (1 << TGL_FP_SHIFT)

typedef struct {
    int x, y, width, height;
    uint16_t *zbuffer;
    uint32_t zcount;
    uint32_t frame;
    int ready;
} tgl_context_t;

typedef struct {
    int x, y, z;          /* screen X/Y and positive camera-space Z */
    uint32_t color;
} tgl_vertex_t;

int  tgl_init(tgl_context_t *ctx, int width, int height);
void tgl_reset(tgl_context_t *ctx);
void tgl_begin(tgl_context_t *ctx, int x, int y, int width, int height, uint32_t clear_color);
void tgl_triangle(tgl_context_t *ctx, const tgl_vertex_t *a, const tgl_vertex_t *b, const tgl_vertex_t *c);
void tgl_draw_cube(tgl_context_t *ctx, int x, int y, int width, int height, int angle);
const char *tgl_renderer_name(void);

#endif
