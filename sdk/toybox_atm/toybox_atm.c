/*
 * ATM Toybox compatibility subset.
 *
 * Command model inspired by upstream toybox (0.8.14), whose permissive
 * LICENSE is preserved under third_party/toybox/. This file is an ATMKoala
 * adaptation using the native syscall/libc ABI, not an upstream Linux build.
 */
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static size_t slen(const char *s) { size_t n=0; while (s && s[n]) n++; return n; }
static int seq(const char *a,const char *b) { while (*a && *a==*b) { a++; b++; } return (unsigned char)*a-(unsigned char)*b; }
static void out(const char *s) { (void)write(STDOUT_FILENO,s,slen(s)); }
static void err(const char *s) { (void)write(STDERR_FILENO,s,slen(s)); }
static void outnum(unsigned long v) { char b[24]; unsigned n=0; do { b[n++]=(char)('0'+v%10); v/=10; } while(v); while(n) { char c=b[--n]; (void)write(1,&c,1); } }

static int app_true(int ac,char **av) { (void)ac; (void)av; return 0; }
static int app_false(int ac,char **av) { (void)ac; (void)av; return 1; }
static int app_echo(int ac,char **av) { int i=1, first=1, nl=1; if (i<ac && !seq(av[i],"-n")) { nl=0; i++; } for(;i<ac;i++) { if(!first) out(" "); out(av[i]); first=0; } if(nl) out("\n"); return 0; }
static int app_cat(int ac,char **av) { char b[512]; int i=1, fd; ssize_t n; if(ac==1) { fd=0; goto copy; } for(;i<ac;i++) { fd=!seq(av[i],"-")?0:open(av[i],O_RDONLY); if(fd<0) { err("toybox: cat: cannot open file\n"); continue; } copy: while((n=read(fd,b,sizeof(b)))>0) if(write(1,b,(size_t)n)!=n) return 1; if(fd>2) close(fd); } return 0; }
static int app_list(int ac,char **av) { (void)ac; (void)av; out("cat echo false true toybox\n"); return 0; }
static int run(int ac,char **av) { const char *name=(ac>1)?av[1]:"toybox"; if(!seq(name,"true")) return app_true(ac-1,av+1); if(!seq(name,"false")) return app_false(ac-1,av+1); if(!seq(name,"echo")) return app_echo(ac-1,av+1); if(!seq(name,"cat")) return app_cat(ac-1,av+1); if(!seq(name,"toybox")||!seq(name,"--list")) return app_list(ac-1,av+1); err("toybox: unknown command\n"); return 127; }
int main(int argc,char **argv) { return run(argc,argv); }
