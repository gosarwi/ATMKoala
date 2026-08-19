#ifndef ATM_TARZST_H
#define ATM_TARZST_H

#include <stdint.h>

/* USTAR archive in a Zstandard frame. Decoder supports raw/RLE Zstd blocks.
 * ATPK is native to ATMKoala. It is inspired by the metadata/payload split of
 * Debian packages but it is not a .deb/dpkg compatibility implementation. */
#define TZST_MAX_UNPACKED 262144u
#define TZST_MAX_FILES    64u
#define TZST_NAME_MAX     64u

typedef struct {
    const uint8_t *tar;
    uint32_t tar_size;
    char package_name[TZST_NAME_MAX];
    char version[32];
    char architecture[24];
    char description[96];
    uint32_t file_count;
    uint32_t payload_count;
    int atpk;
    int manifest_present;
} tzst_pkg_t;

int tzst_parse(tzst_pkg_t *pkg, const uint8_t *buf, uint32_t size);
void tzst_info(const tzst_pkg_t *pkg);
/* Installs legacy tar.zst only in compatibility mode, or ATPK transactionally
 * through /tmp staging after control/manifest validation. */
int tzst_install(const tzst_pkg_t *pkg);
int tzst_remove(const char *pkg_name);
int tzst_wrap_elf(const uint8_t *elf_data, uint32_t elf_size,
                  const char *name, const char *version,
                  uint8_t *out_buf, uint32_t out_sz);

#endif
