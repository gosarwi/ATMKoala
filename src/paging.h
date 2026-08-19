#ifndef ATM_PAGING_H
#define ATM_PAGING_H

#include <stdint.h>
#include <stddef.h>

#define ATM_PAGE_SIZE        0x1000ULL
#define ATM_PAGE_MASK        0x000FFFFFFFFFF000ULL
#define ATM_USER_BASE        0x0000000040000000ULL
#define ATM_USER_WINDOW_SIZE 0x0000000000200000ULL
#define ATM_USER_TOP         (ATM_USER_BASE + ATM_USER_WINDOW_SIZE)
#define ATM_USER_STACK_TOP   (ATM_USER_TOP - 0x1000ULL)

#define ATM_PTE_P            0x001ULL
#define ATM_PTE_W            0x002ULL
#define ATM_PTE_U            0x004ULL
#define ATM_PTE_PS           0x080ULL
#define ATM_PTE_NX           (1ULL << 63)

typedef struct user_space {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd[4];
    uint64_t *user_pt;
    uintptr_t cr3;
    uintptr_t phys_base;
    uint64_t mapped_pages;
    int valid;
} user_space_t;

void paging_init(void);
uintptr_t paging_kernel_cr3(void);
int paging_create_user_space(user_space_t *space);
int paging_map_user_page(user_space_t *space, uint64_t user_va,
                         uintptr_t phys, uint64_t flags);
int paging_unmap_user_page(user_space_t *space, uint64_t user_va);
int paging_user_translate(const user_space_t *space, uint64_t user_va,
                          uintptr_t *phys_out, uint64_t *flags_out);
int paging_user_range(const user_space_t *space, uint64_t user_va,
                      size_t size, int write);
int paging_selftest(void);

#endif
