/* Adapted from musl libc (MIT); ASCII-only freestanding ATMKoala subset. */
#include <ctype.h>

static int ascii_alpha(int c){ return ((unsigned)c|32U)-'a' < 26U; }
int isalnum(int c){ return isdigit(c)||ascii_alpha(c); }
int isalpha(int c){ return ascii_alpha(c); }
int isascii(int c){ return !(c&~0x7f); }
int isblank(int c){ return c==' '||c=='\t'; }
int iscntrl(int c){ return (unsigned)c<0x20U || c==0x7f; }
int isdigit(int c){ return (unsigned)c-'0' < 10U; }
int isgraph(int c){ return (unsigned)c-0x21U < 0x5eU; }
int islower(int c){ return (unsigned)c-'a' < 26U; }
int isprint(int c){ return (unsigned)c-0x20U < 0x5fU; }
int ispunct(int c){ return isgraph(c) && !isalnum(c); }
int isspace(int c){ return c==' ' || (unsigned)c-'\t' < 5U; }
int isupper(int c){ return (unsigned)c-'A' < 26U; }
int isxdigit(int c){ return isdigit(c) || (unsigned)((c|32)-'a') < 6U; }
int toascii(int c){ return c&0x7f; }
int tolower(int c){ return isupper(c) ? c|32 : c; }
int toupper(int c){ return islower(c) ? c&0x5f : c; }
