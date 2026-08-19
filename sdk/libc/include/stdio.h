#ifndef ATM_LIBC_STDIO_H
#define ATM_LIBC_STDIO_H

/* Minimal unbuffered stdio subset for static ATM-native applications. */
int putchar(int c);
int puts(const char *s);

#endif
