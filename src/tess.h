#ifndef TESS_H
#define TESS_H
/* tess.h — atmkoala v0.5 Vim-inspired modal text editor */
#include <stdint.h>
#define TESS_MAX_LINES  2048
#define TESS_MAX_COL     256
#define TESS_UNDO        32
#define TESS_TAB_SZ       4
typedef enum { TM_NORMAL=0, TM_INSERT, TM_VISUAL, TM_CMD, TM_SEARCH } tess_mode_t;
typedef struct { char text[TESS_MAX_COL]; int len; } tess_line_t;
typedef struct {
    tess_line_t *lines; int nlines;
    int cx, cy, sx, sy;    /* cursor and scroll */
    int vrows, vcols;
    char fname[128];
    int modified, lineno, syntax;
    tess_mode_t mode;
    char cmdbuf[256]; int cmdpos;
    tess_line_t yank[16]; int nyank;
    struct { tess_line_t *lines; int nl, cx, cy; } undo[TESS_UNDO];
    int undo_top;
    int vis_start, vis_end;
    char search[128];
    char status[128]; int st_time;
    int g_pend, d_pend, y_pend;
} tess_t;
void tess_init(void);
void tess_open(const char *fname);
void tess_run(void);
void tess_free(void);
#endif
