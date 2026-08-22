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
/* Fetches a clear-text HTTP ATPK package, validates it, writes a local cache
 * staging file and then invokes the existing transactional installer. */
int tzst_fetch_install_http(const char *url);
/* Bounded clear-text repository convenience layer. It stores only http://
 * URLs and resolves one native package as <base>/<name>.atpk. */
const char *tzst_repo_url(void);
int tzst_repo_set_url(const char *url);
int tzst_repo_fetch_package(const char *pkg_name);
int tzst_repo_selftest(void);
/* Bounded installed-package metadata enumeration from the native registry.
 * It lists package records only; it is not a dependency solver. */
uint32_t tzst_installed_count(void);
int tzst_installed_at(uint32_t index,char *name,uint32_t name_cap,char *version,uint32_t version_cap,char *arch,uint32_t arch_cap);
/* Copies an optional installed ATPK Description field, or "-" when absent. */
int tzst_installed_description_at(uint32_t index,char *description,uint32_t description_cap);
int tzst_wrap_elf(const uint8_t *elf_data, uint32_t elf_size,
                  const char *name, const char *version,
                  uint8_t *out_buf, uint32_t out_sz);

#endif
