/* Adapted from musl libc (MIT); freestanding ATMKoala string subset. */
#include <string.h>
#include <stdlib.h>

void *memcpy(void *restrict dest,const void *restrict src,size_t n){
    unsigned char *d=(unsigned char *)dest; const unsigned char *s=(const unsigned char *)src;
    while(n--) *d++=*s++; return dest;
}
void *memmove(void *dest,const void *src,size_t n){
    unsigned char *d=(unsigned char *)dest; const unsigned char *s=(const unsigned char *)src;
    if(d==s || !n) return dest;
    if(d<s) while(n--) *d++=*s++; else { d+=n; s+=n; while(n--) *--d=*--s; }
    return dest;
}
void *memset(void *dest,int c,size_t n){ unsigned char *d=(unsigned char *)dest; while(n--)*d++=(unsigned char)c; return dest; }
int memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=a,*y=b; while(n--){if(*x!=*y)return (int)*x-(int)*y;x++;y++;}return 0; }
void *memchr(const void *s,int c,size_t n){ const unsigned char*p=s; while(n--){if(*p==(unsigned char)c)return (void *)p;p++;}return 0; }
void *memrchr(const void *s,int c,size_t n){ const unsigned char*p=(const unsigned char *)s+n; while(n--){if(*--p==(unsigned char)c)return (void *)p;}return 0; }

size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
size_t strnlen(const char *s,size_t n){size_t i=0;while(i<n&&s[i])i++;return i;}
char *strcpy(char *restrict dest,const char *restrict src){char*out=dest;while((*dest++=*src++));return out;}
char *strncpy(char *restrict dest,const char *restrict src,size_t n){char*out=dest;while(n&&*src){*dest++=*src++;n--;}while(n--)*dest++=0;return out;}
char *strcat(char *restrict dest,const char *restrict src){char *d=dest+strlen(dest);while((*d++=*src++));return dest;}
char *strncat(char *restrict dest,const char *restrict src,size_t n){char*d=dest+strlen(dest);while(n--&&*src)*d++=*src++;*d=0;return dest;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char*a,const char*b,size_t n){while(n--){if(*a!=*b)return (unsigned char)*a-(unsigned char)*b;if(!*a)return 0;a++;b++;}return 0;}
static unsigned char fold_ascii(unsigned char c){return (c>='A'&&c<='Z')?(unsigned char)(c+('a'-'A')):c;}
int strcasecmp(const char*a,const char*b){unsigned char x,y;do{x=fold_ascii((unsigned char)*a++);y=fold_ascii((unsigned char)*b++);}while(x&&x==y);return (int)x-(int)y;}
int strncasecmp(const char*a,const char*b,size_t n){unsigned char x=0,y=0;while(n--){x=fold_ascii((unsigned char)*a++);y=fold_ascii((unsigned char)*b++);if(x!=y||!x)break;}return (int)x-(int)y;}
char *strchr(const char*s,int c){char ch=(char)c;do{if(*s==ch)return(char *)s;}while(*s++);return 0;}
char *strrchr(const char*s,int c){const char*last=0;char ch=(char)c;do{if(*s==ch)last=s;}while(*s++);return(char *)last;}
char *strstr(const char *haystack,const char *needle){if(!*needle)return(char *)haystack;for(;*haystack;haystack++){const char*h=haystack,*n=needle;while(*h&&*n&&*h==*n){h++;n++;}if(!*n)return(char *)haystack;}return 0;}
char *strpbrk(const char*s,const char*accept){for(;*s;s++)if(strchr(accept,*s))return(char *)s;return 0;}
size_t strspn(const char*s,const char*accept){const char*p=s;while(*p&&strchr(accept,*p))p++;return(size_t)(p-s);}
size_t strcspn(const char*s,const char*reject){const char*p=s;while(*p&&!strchr(reject,*p))p++;return(size_t)(p-s);}
char *strtok_r(char *restrict s,const char *restrict delim,char **restrict saveptr){
    char *token; if(!s)s=*saveptr; s+=strspn(s,delim); if(!*s){*saveptr=s;return 0;} token=s; s+=strcspn(s,delim); if(*s)*s++=0; *saveptr=s; return token;
}
char *strtok(char *restrict s,const char *restrict delim){static char *save;return strtok_r(s,delim,&save);}
char *strdup(const char*s){size_t n=strlen(s)+1;char*d=malloc(n);if(d)memcpy(d,s,n);return d;}
char *strndup(const char*s,size_t n){n=strnlen(s,n);char*d=malloc(n+1);if(!d)return 0;memcpy(d,s,n);d[n]=0;return d;}
