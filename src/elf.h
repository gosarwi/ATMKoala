#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>
#include "paging.h"

/* ELF32 types */
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

/* ELF magic */
#define ELF_MAGIC0  0x7F
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'

/* e_type */
#define ET_NONE  0
#define ET_REL   1
#define ET_EXEC  2
#define ET_DYN   3

/* e_machine */
#define EM_386     3
#define EM_X86_64  62

/* e_ident indices */
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4   /* 1=32bit, 2=64bit */
#define EI_DATA     5   /* 1=LSB, 2=MSB */
#define EI_VERSION  6
#define EI_OSABI    7
#define EI_NIDENT  16

/* Native ELF64 types and headers used by CPL 3 ATMKoala programs. */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

typedef struct __attribute__((packed)) {
    uint8_t e_ident[EI_NIDENT];
    Elf64_Half e_type, e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff, e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct __attribute__((packed)) {
    Elf64_Word p_type, p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr, p_paddr;
    Elf64_Xword p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct {
    int valid;
    uint64_t entry, load_base, load_end, stack_top;
    char error[64];
} elf64_user_image_t;

/* ELF32 header */
typedef struct __attribute__((packed)) {
    uint8_t    e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off  e_phoff;   /* program header offset */
    Elf32_Off  e_shoff;   /* section header offset */
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
} Elf32_Ehdr;

/* Program header */
typedef struct __attribute__((packed)) {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

/* p_type */
#define PT_NULL  0
#define PT_LOAD  1
#define PT_NOTE  4

/* p_flags */
#define PF_X  0x1
#define PF_W  0x2
#define PF_R  0x4

/* Section header */
typedef struct __attribute__((packed)) {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off  sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

/* ELF load result */
typedef struct {
    int       valid;
    uint32_t  entry;      /* entry point virtual address */
    uint32_t  load_base;  /* lowest load address */
    uint32_t  load_end;   /* highest load address */
    char      error[64];
} elf_load_result_t;

/* Validate an ELF buffer (check magic, class, arch) */
int              elf_validate(const uint8_t *buf, uint32_t size);

/* Load ELF segments into memory, return entry point */
elf_load_result_t elf_load(const uint8_t *buf, uint32_t size);

/* Print ELF info (readelf-lite) */
void             elf_info(const uint8_t *buf, uint32_t size);

/* Native x86-64 ET_EXEC loader. It maps data only into a supplied user
 * page-table window and never executes the resulting entry in ring 0. */
int elf64_validate_native(const uint8_t *buf, uint32_t size);
int elf64_load_user(const uint8_t *buf, uint32_t size, user_space_t *space,
                    elf64_user_image_t *result);

#endif
