#ifndef ATM_FREESTANDING_STRING_H
#define ATM_FREESTANDING_STRING_H

#include "util.h"
#define memcpy  kmemcpy
#define memmove kmemmove
#define memset  kmemset
#define memcmp  kmemcmp
#define strlen  kstrlen
#define strcmp  kstrcmp
#define strncmp kstrncmp
#define strcpy  kstrcpy
#define strncpy kstrncpy
#define strcat  kstrcat
#define strchr  kstrchr
#define strstr  kstrstr

#endif
