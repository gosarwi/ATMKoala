#include <stdio.h>
#include <unistd.h>

int putchar(int c) {
    unsigned char ch=(unsigned char)c;
    return write(STDOUT_FILENO,&ch,1)==1 ? (int)ch : -1;
}

int puts(const char *s) {
    size_t n=0;
    if(!s) return -1;
    while(s[n]) n++;
    if(write(STDOUT_FILENO,s,n)!=(ssize_t)n) return -1;
    return putchar('\n')<0 ? -1 : 0;
}
