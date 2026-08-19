#ifndef CONFIG_H
#define CONFIG_H

/*  config.c — Ubuntu-style configuration system for atmkoala v0.5
 *
 *  All configs live under /uiu/etc/
 *  Format: INI-style sections and key=value pairs
 *
 *  Example /uiu/etc/system.conf:
 *    [system]
 *    hostname=atmkoala
 *    timezone=UTC
 *
 *    [console]
 *    theme=caramel
 *    mode=vbe
 */
#include <stdint.h>
#include <stddef.h>

#define CFG_MAX_SECTIONS  16
#define CFG_MAX_KEYS      32
#define CFG_KEY_LEN       64
#define CFG_VAL_LEN       128
#define CFG_ROOT          "/uiu/etc"

typedef struct {
    char key[CFG_KEY_LEN];
    char val[CFG_VAL_LEN];
} cfg_pair_t;

typedef struct {
    char      name[CFG_KEY_LEN];
    cfg_pair_t entries[CFG_MAX_KEYS];
    int        count;
} cfg_section_t;

typedef struct {
    cfg_section_t sections[CFG_MAX_SECTIONS];
    int           count;
    char          path[256];    /* source file */
    int           dirty;
} cfg_file_t;

/* Parse a config file from string buffer */
int  cfg_parse(cfg_file_t *cfg, const char *buf, const char *path);

/* Serialize back to string */
int  cfg_serialize(const cfg_file_t *cfg, char *buf, size_t bufsz);

/* Get/set values */
const char *cfg_get(const cfg_file_t *cfg, const char *section, const char *key);
const char *cfg_get_default(const cfg_file_t *cfg, const char *section,
                             const char *key, const char *def);
int  cfg_set(cfg_file_t *cfg, const char *section,
             const char *key, const char *val);

/* Load from VFS path */
int  cfg_load(cfg_file_t *cfg, const char *vfs_path);

/* Save to VFS path */
int  cfg_save(const cfg_file_t *cfg, const char *vfs_path);

/* Global system config (loaded at boot) */
extern cfg_file_t g_syscfg;    /* /uiu/etc/system.conf  */
extern cfg_file_t g_netcfg;    /* /uiu/etc/network.conf */
extern cfg_file_t g_usercfg;   /* /uiu/etc/users.conf   */
extern cfg_file_t g_pkgcfg;    /* /uiu/etc/packages.conf*/

void config_init(void);   /* load all global configs */

/* Convenience: read system config value */
const char *sysconf_get(const char *section, const char *key);
int         sysconf_set(const char *section, const char *key, const char *val);
void        sysconf_save(void);

#endif
