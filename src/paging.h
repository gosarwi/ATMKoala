#ifndef ATM_PAGING_H
#define ATM_PAGING_H

#include <stdint.h>
#include <stddef.h>

#define ATM_PAGE_SIZE        0x1000ULL
#define ATM_PAGE_MASK        0x000FFFFFFFFFF000ULL
#define ATM_USER_BASE        0x0000000040000000ULL
/* Bounded per-process region for static Linux-style user runtimes. It covers
 * 64 x 2 MiB page-table spans while retaining a high guard page for the stack. */
#define ATM_USER_WINDOW_SIZE 0x0000000008000000ULL
#define ATM_USER_TOP         (ATM_USER_BASE + ATM_USER_WINDOW_SIZE)
#define ATM_USER_STACK_TOP   (ATM_USER_TOP - 0x1000ULL)
/* Static ELF and brk occupy the lower 16 MiB; anonymous Linux mmap is placed
 * top-down above this boundary and below the mapped user stack page. */
#define ATM_USER_ANON_BASE   (ATM_USER_BASE + 0x01000000ULL)
#define ATM_USER_PT_COUNT    (ATM_USER_WINDOW_SIZE / (512ULL * ATM_PAGE_SIZE))

#define ATM_PTE_P            0x001ULL
#define ATM_PTE_W            0x002ULL
#define ATM_PTE_U            0x004ULL
#define ATM_PTE_PS           0x080ULL
#define ATM_PTE_NX           (1ULL << 63)

typedef struct user_space {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd[4];
    /* Heap-owned index of lazily allocated 4 KiB tables, one per 2 MiB span.
     * It stays out of kernel task-stack frames that also hold 4 KiB ELF probes. */
    uint64_t **user_pt;
    uintptr_t cr3;
    uintptr_t phys_base;
    uint64_t mapped_pages;
    int valid;
} user_space_t;

void paging_init(void);
uintptr_t paging_kernel_cr3(void);
/* Select a fully constructed user CR3 on the current CPU. */
int paging_activate_user_space(const user_space_t *space);
int paging_create_user_space(user_space_t *space);
int paging_map_user_page(user_space_t *space, uint64_t user_va,
                         uintptr_t phys, uint64_t flags);
int paging_unmap_user_page(user_space_t *space, uint64_t user_va);
/* Clear a mapping and release its owned physical page. */
int paging_release_user_page(user_space_t *space, uint64_t user_va);
/* Retain the mapped physical page while replacing its writable/NX policy. */
int paging_protect_user_page(user_space_t *space, uint64_t user_va,
                             uint64_t flags);
/* Count present pages in the dedicated bounded user window. */
uint64_t paging_user_mapped_bytes(const user_space_t *space);
/* Free only resources owned by paging_create_user_space()/elf64_load_user(). */
void paging_destroy_user_space(user_space_t *space);
int paging_user_translate(const user_space_t *space, uint64_t user_va,
                          uintptr_t *phys_out, uint64_t *flags_out);
int paging_user_range(const user_space_t *space, uint64_t user_va,
                      size_t size, int write);
int paging_selftest(void);

#endif
