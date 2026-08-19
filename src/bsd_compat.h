#ifndef BSD_COMPAT_H
#define BSD_COMPAT_H
/*
 * bsd_compat.h — NetBSD driver-API compatibility shim for atmkoala
 *
 * This is NOT a copy of any NetBSD source file. It is an original,
 * independent reimplementation of the *public function/type
 * signatures* that NetBSD's bus_space(9) and autoconf(9) frameworks
 * expose, so that hardware-register-level logic ported from a BSD
 * driver (the actual register read/write sequences, mode-setting
 * tables, etc. — which is where the real engineering value is) can
 * be dropped in with minimal changes, without pulling in NetBSD's
 * device-tree/autoconf machinery, memory allocators, or anything
 * else this kernel doesn't have or need.
 *
 * Covered subset:
 *   - bus_space_tag_t / bus_space_handle_t   (opaque handles)
 *   - bus_space_map() / bus_space_unmap()
 *   - bus_space_read_1/2/4()  bus_space_write_1/2/4()
 *   - bus_space_read_region_*() / bus_space_write_region_*()
 *   - device_t (minimal: just a name + driver-private softc pointer)
 *   - PCI helper names matching <dev/pci/pcireg.h> conventions
 *     (PCI_VENDOR(), PCI_PRODUCT(), PCI_CLASS(), ...) so register
 *     decode logic copied verbatim from a BSD driver's probe routine
 *     still reads correctly against the value our own pci.c returns.
 *
 * What this deliberately does NOT include: autoconf(9) device
 * attach/detach trees, config_search()/config_found(), the BSD
 * VM/pmap layer, or anything from a specific copyrighted NetBSD
 * source file. Those are architecture-specific to NetBSD's kernel
 * and have no equivalent need here — atmkoala already has its own
 * PCI scanner (hw_y116.c) and its own driver registration; this
 * header exists purely so register-twiddling code can be ported
 * with its original variable names and call shapes intact, which
 * makes it much easier to diff against the upstream driver later
 * and verify nothing was mistranslated.
 */

#include <stdint.h>
#include <stddef.h>

/* ── bus_space: two kinds of bus on x86 — I/O ports and MMIO ──── */
typedef enum {
    BUS_SPACE_IO  = 0,   /* in/out instructions, 16-bit port number   */
    BUS_SPACE_MEM = 1,   /* memory-mapped, accessed via a 64-bit ptr  */
} bus_space_tag_t;

typedef struct {
    bus_space_tag_t type;
    uint64_t        base;     /* I/O port base, or MMIO physical/virtual base
                                * (identity-mapped, so physical==virtual here) */
    uint64_t        size;     /* mapped region size, for bounds checking */
} bus_space_handle_t;

/* bus_space_map(): on real NetBSD this also handles MMU mapping;
 * here all physical memory in the first 4GB is already
 * identity-mapped by boot.s, so this just records the handle. */
static inline int bus_space_map(bus_space_tag_t t, uint64_t addr,
                                 uint64_t size, int flags,
                                 bus_space_handle_t *out) {
    (void)flags;
    out->type = t;
    out->base = addr;
    out->size = size;
    return 0; /* NetBSD returns errno; we have nothing that fails here */
}

static inline void bus_space_unmap(bus_space_handle_t *h, uint64_t size) {
    (void)h; (void)size; /* nothing to release in the identity-map model */
}

/* ── 8/16/32-bit single read/write ──────────────────────────── */
uint8_t  bus_space_read_1(const bus_space_handle_t *h, uint64_t off);
uint16_t bus_space_read_2(const bus_space_handle_t *h, uint64_t off);
uint32_t bus_space_read_4(const bus_space_handle_t *h, uint64_t off);
void     bus_space_write_1(const bus_space_handle_t *h, uint64_t off, uint8_t  v);
void     bus_space_write_2(const bus_space_handle_t *h, uint64_t off, uint16_t v);
void     bus_space_write_4(const bus_space_handle_t *h, uint64_t off, uint32_t v);

/* ── Region read/write (block transfer, used by some mode-set code) */
void bus_space_read_region_1 (const bus_space_handle_t *h, uint64_t off,
                              uint8_t  *buf, size_t count);
void bus_space_write_region_1(const bus_space_handle_t *h, uint64_t off,
                              const uint8_t *buf, size_t count);

/* ── device_t: minimal stand-in ─────────────────────────────── */
typedef struct device {
    char  dv_xname[16];   /* matches NetBSD's device_xname() field name */
    void *dv_private;     /* driver "softc" — whatever the caller wants */
} *device_t;

static inline const char *device_xname(device_t d) { return d->dv_xname; }

/* ── PCI ID decode helpers (matches <dev/pci/pcireg.h> macro names) ─
 * Our own pci.c (hw_y116.c) already splits vendor/device/class out
 * of the raw 32-bit config space words; these macros let driver code
 * that still does the raw-word decoding itself (as upstream probe
 * routines typically do) work unmodified. */
#define PCI_VENDOR(id)        ((uint16_t)((id) & 0xFFFF))
#define PCI_PRODUCT(id)       ((uint16_t)(((id) >> 16) & 0xFFFF))
#define PCI_CLASS(cc)         ((uint8_t)(((cc) >> 24) & 0xFF))
#define PCI_SUBCLASS(cc)      ((uint8_t)(((cc) >> 16) & 0xFF))
#define PCI_INTERFACE(cc)     ((uint8_t)(((cc) >> 8)  & 0xFF))
#define PCI_REVISION(cc)      ((uint8_t)((cc) & 0xFF))

/* Standard PCI class codes (these numeric values are part of the PCI
 * SIG specification itself, not any one project's source — same
 * status as the MBR layout: a vendor-neutral published standard). */
#define PCI_CLASS_DISPLAY     0x03
#define PCI_SUBCLASS_DISPLAY_VGA 0x00

#endif /* BSD_COMPAT_H */
