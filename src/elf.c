/*  elf.c — ELF32 loader for atmkoala OS v0.5
 *
 *  Supports loading ET_EXEC (executable) ELF32 binaries.
 *  PT_LOAD segments are copied from the file buffer into memory.
 *  Zero-initializes BSS (memsz > filesz).
 */
#include "elf.h"
#include "kmalloc.h"
#include "util.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

/* ── Validation ──────────────────────────────────────────── */
int elf_validate(const uint8_t *buf, uint32_t size) {
    if (size < sizeof(Elf32_Ehdr)) return 0;
    const Elf32_Ehdr *hdr = (const Elf32_Ehdr *)buf;
    if (hdr->e_ident[EI_MAG0] != ELF_MAGIC0) return 0;
    if (hdr->e_ident[EI_MAG1] != ELF_MAGIC1) return 0;
    if (hdr->e_ident[EI_MAG2] != ELF_MAGIC2) return 0;
    if (hdr->e_ident[EI_MAG3] != ELF_MAGIC3) return 0;
    if (hdr->e_ident[EI_CLASS] != 1)         return 0; /* ELF32 */
    if (hdr->e_ident[EI_DATA]  != 1)         return 0; /* little-endian */
    if (hdr->e_machine != EM_386)             return 0;
    return 1;
}

/* ── Load ─────────────────────────────────────────────────── */
elf_load_result_t elf_load(const uint8_t *buf, uint32_t size) {
    elf_load_result_t res = {0};

    if (!elf_validate(buf, size)) {
        kstrcpy(res.error, "Not a valid ELF32 x86 binary");
        return res;
    }

    const Elf32_Ehdr *hdr = (const Elf32_Ehdr *)buf;

    if (hdr->e_type != ET_EXEC) {
        kstrcpy(res.error, "Only ET_EXEC supported");
        return res;
    }
    if (hdr->e_phnum == 0) {
        kstrcpy(res.error, "No program headers");
        return res;
    }

    res.load_base = 0xFFFFFFFF;
    res.load_end  = 0;

    /* Walk PT_LOAD segments */
    for (int i = 0; i < hdr->e_phnum; i++) {
        uint32_t ph_off = hdr->e_phoff + (uint32_t)i * hdr->e_phentsize;
        if (ph_off + sizeof(Elf32_Phdr) > size) break;

        const Elf32_Phdr *ph = (const Elf32_Phdr *)(buf + ph_off);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        /* Safety: don't allow loading into low memory or kernel space */
        if (ph->p_vaddr < 0x100000) {
            kstrcpy(res.error, "ELF vaddr too low (<1MB)");
            return res;
        }

        /* Copy file data */
        uint8_t *dest = (uint8_t *)(uintptr_t)ph->p_vaddr;
        uint32_t filesz = ph->p_filesz;
        if (filesz > size - ph->p_offset) filesz = size - ph->p_offset;

        for (uint32_t j = 0; j < filesz; j++)
            dest[j] = buf[ph->p_offset + j];

        /* Zero BSS (memsz - filesz) */
        for (uint32_t j = filesz; j < ph->p_memsz; j++)
            dest[j] = 0;

        if (ph->p_vaddr < res.load_base) res.load_base = ph->p_vaddr;
        uint32_t end = ph->p_vaddr + ph->p_memsz;
        if (end > res.load_end) res.load_end = end;
    }

    res.entry = hdr->e_entry;
    res.valid = 1;
    return res;
}

/* ── elf_info — print ELF metadata ─────────────────────────── */
static const char *elf_type_str(uint16_t t) {
    switch (t) {
        case ET_REL:  return "REL (relocatable)";
        case ET_EXEC: return "EXEC (executable)";
        case ET_DYN:  return "DYN (shared object)";
        default:      return "UNKNOWN";
    }
}

void elf_info(const uint8_t *buf, uint32_t size) {
    if (!elf_validate(buf, size)) {
        terminal_writeln("  Not a valid ELF32 binary");
        return;
    }
    const Elf32_Ehdr *hdr = (const Elf32_Ehdr *)buf;
    char tmp[16];

    terminal_write("  Type:        "); terminal_writeln(elf_type_str(hdr->e_type));
    terminal_write("  Machine:     ");
    terminal_writeln(hdr->e_machine == EM_386 ? "x86 (i386)" : "unknown");
    terminal_write("  Entry:       0x");
    kuitoa(hdr->e_entry, tmp, 16); terminal_writeln(tmp);
    terminal_write("  PH count:    ");
    kuitoa(hdr->e_phnum, tmp, 10); terminal_writeln(tmp);
    terminal_write("  SH count:    ");
    kuitoa(hdr->e_shnum, tmp, 10); terminal_writeln(tmp);

    /* Print program headers */
    terminal_writeln("  Program headers:");
    for (int i = 0; i < hdr->e_phnum; i++) {
        uint32_t ph_off = hdr->e_phoff + (uint32_t)i * hdr->e_phentsize;
        if (ph_off + sizeof(Elf32_Phdr) > size) break;
        const Elf32_Phdr *ph = (const Elf32_Phdr *)(buf + ph_off);
        const char *ptype = (ph->p_type == PT_LOAD) ? "LOAD" :
                            (ph->p_type == PT_NOTE) ? "NOTE" : "OTHER";
        terminal_write("    [");
        kuitoa((uint32_t)i, tmp, 10); terminal_write(tmp);
        terminal_write("] ");
        terminal_write(ptype);
        terminal_write("  vaddr=0x");
        kuitoa(ph->p_vaddr, tmp, 16); terminal_write(tmp);
        terminal_write("  filesz=");
        kuitoa(ph->p_filesz, tmp, 10); terminal_write(tmp);
        terminal_write("  memsz=");
        kuitoa(ph->p_memsz, tmp, 10); terminal_writeln(tmp);
    }
}

/* ── Native ELF64 user loader ──────────────────────────────── */
int elf64_validate_native(const uint8_t *buf,uint32_t size){
    if(!buf||size<sizeof(Elf64_Ehdr)) return 0;
    const Elf64_Ehdr *h=(const Elf64_Ehdr *)buf;
    return h->e_ident[EI_MAG0]==ELF_MAGIC0 && h->e_ident[EI_MAG1]==ELF_MAGIC1 &&
           h->e_ident[EI_MAG2]==ELF_MAGIC2 && h->e_ident[EI_MAG3]==ELF_MAGIC3 &&
           h->e_ident[EI_CLASS]==2 && h->e_ident[EI_DATA]==1 &&
           h->e_ident[EI_VERSION]==1 && h->e_machine==EM_X86_64 &&
           h->e_type==ET_EXEC && h->e_ehsize==sizeof(Elf64_Ehdr) &&
           h->e_phentsize==sizeof(Elf64_Phdr) && h->e_phnum>0;
}

static int elf64_map_page(user_space_t *s,uint64_t va,uint64_t pf){
    uintptr_t phys;
    if(paging_user_translate(s,va,&phys,0)==0) return 0;
    uint8_t *page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    if(!page) return -1;
    uint64_t flags=(pf&PF_W)?ATM_PTE_W:0;
    if(!(pf&PF_X)) flags|=ATM_PTE_NX;
    return paging_map_user_page(s,va,(uintptr_t)page,flags);
}
static int elf64_copy_to_mapped(user_space_t *s,uint64_t va,const uint8_t *src,uint64_t n){
    while(n){
        uintptr_t phys=0;
        if(paging_user_translate(s,va,&phys,0)<0) return -1;
        uint64_t room=ATM_PAGE_SIZE-(va&0xfffULL);
        uint64_t take=n<room?n:room;
        kmemcpy((void *)phys,src,(size_t)take);
        va+=take; src+=take; n-=take;
    }
    return 0;
}
static int elf64_zero_mapped(user_space_t *s,uint64_t va,uint64_t n){
    while(n){
        uintptr_t phys=0;
        if(paging_user_translate(s,va,&phys,0)<0) return -1;
        uint64_t room=ATM_PAGE_SIZE-(va&0xfffULL);
        uint64_t take=n<room?n:room;
        kmemset((void *)phys,0,(size_t)take);
        va+=take; n-=take;
    }
    return 0;
}

int elf64_load_user(const uint8_t *buf,uint32_t size,user_space_t *s,elf64_user_image_t *out){
    if(out) kmemset(out,0,sizeof(*out));
    if(!out||!s||!s->valid||!elf64_validate_native(buf,size)){
        if(out) kstrcpy(out->error,"invalid native ELF64");
        return -1;
    }
    const Elf64_Ehdr *h=(const Elf64_Ehdr *)buf;
    if(h->e_phoff>(uint64_t)size || (uint64_t)h->e_phnum*sizeof(Elf64_Phdr)>(uint64_t)size-h->e_phoff){
        kstrcpy(out->error,"program headers outside file"); return -1;
    }
    uint64_t base=~0ULL,end=0; int loads=0;
    for(uint16_t i=0;i<h->e_phnum;i++){
        const Elf64_Phdr *p=(const Elf64_Phdr *)(buf+h->e_phoff+(uint64_t)i*sizeof(*p));
        if(p->p_type!=PT_LOAD||!p->p_memsz) continue;
        if(p->p_filesz>p->p_memsz || p->p_offset>(uint64_t)size || p->p_filesz>(uint64_t)size-p->p_offset){
            kstrcpy(out->error,"malformed load segment"); return -1;
        }
        uint64_t seg_end=p->p_vaddr+p->p_memsz;
        if(seg_end<p->p_vaddr || p->p_vaddr<ATM_USER_BASE || seg_end>ATM_USER_STACK_TOP){
            kstrcpy(out->error,"segment outside user window"); return -1;
        }
        uint64_t page=p->p_vaddr&ATM_PAGE_MASK, last=(seg_end-1)&ATM_PAGE_MASK;
        for(;;){
            if(elf64_map_page(s,page,p->p_flags)<0){kstrcpy(out->error,"out of user pages");return -1;}
            if(page==last) break;
            page+=ATM_PAGE_SIZE;
        }
        if(elf64_copy_to_mapped(s,p->p_vaddr,buf+p->p_offset,p->p_filesz)<0 ||
           elf64_zero_mapped(s,p->p_vaddr+p->p_filesz,p->p_memsz-p->p_filesz)<0){
            kstrcpy(out->error,"segment copy failed"); return -1;
        }
        if(p->p_vaddr<base) base=p->p_vaddr;
        if(seg_end>end) end=seg_end;
        loads++;
    }
    if(!loads||h->e_entry<ATM_USER_BASE||h->e_entry>=ATM_USER_STACK_TOP){
        kstrcpy(out->error,"invalid user entry"); return -1;
    }
    if(elf64_map_page(s,ATM_USER_STACK_TOP,PF_W)<0){kstrcpy(out->error,"stack map failed");return -1;}
    out->valid=1; out->entry=h->e_entry; out->load_base=base; out->load_end=end;
    out->stack_top=ATM_USER_STACK_TOP+ATM_PAGE_SIZE;
    return 0;
}
