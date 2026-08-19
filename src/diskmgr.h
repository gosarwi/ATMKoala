#ifndef DISKMGR_H
#define DISKMGR_H
/*
 * diskmgr.h — interactive disk partitioning TUI for atmkoala
 *
 * Full-screen text interface over partmgr.c (MBR table) + catfs.c
 * (format a partition with CatFS once it's been carved out).
 *
 * Run from the shell as: diskmgr
 */
void diskmgr_run(void);

#endif
