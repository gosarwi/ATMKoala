#ifndef GDT_H
#define GDT_H
#include <stdint.h>

/* x86-64 GDT and TSS. Descriptors are bootstrapped in boot.s and the
 * runtime TSS descriptor is installed by gdt_install(). */
void gdt_install(void);
void gdt_set_kernel_stack(uint64_t rsp0);

#endif
