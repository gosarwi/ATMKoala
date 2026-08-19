/* config.c — INI-style config system for atmkoala v0.5
 * All configs under /uiu/etc/ — Ubuntu-style naming conventions
 */
#include "config.h"
#include "vfs.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

cfg_file_t g_syscfg  = {0};
cfg_file_t g_netcfg  = {0};
cfg_file_t g_usercfg = {0};
cfg_file_t g_pkgcfg  = {0};

/* ── Parser ─────────────────────────────────────────────────── */
int cfg_parse(cfg_file_t *cfg, const char *buf, const char *path) {
    kmemset(cfg, 0, sizeof(*cfg));
    if (path) kstrcpy(cfg->path, path);

    cfg_section_t *cur = NULL;
    const char *p = buf;

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '\n' || *p == '\r') { p++; continue; }
        if (*p == '#' || *p == ';') {
            /* Comment — skip line */
            while (*p && *p != '\n') p++;
            continue;
        }

        if (*p == '[') {
            /* Section header */
            p++;
            if (cfg->count >= CFG_MAX_SECTIONS) { while (*p && *p != '\n') p++; continue; }
            cur = &cfg->sections[cfg->count++];
            int i = 0;
            while (*p && *p != ']' && *p != '\n' && i < CFG_KEY_LEN-1)
                cur->name[i++] = *p++;
            cur->name[i] = 0;
            while (*p && *p != '\n') p++;
            continue;
        }

        /* key=value */
        if (!cur) {
            /* No section yet — create implicit [global] */
            if (cfg->count < CFG_MAX_SECTIONS) {
                cur = &cfg->sections[cfg->count++];
                kstrcpy(cur->name, "global");
            }
        }

        if (cur && cur->count < CFG_MAX_KEYS) {
            cfg_pair_t *kv = &cur->entries[cur->count];
            int i = 0;
            while (*p && *p != '=' && *p != '\n' && i < CFG_KEY_LEN-1) {
                char c = *p++;
                if (c != ' ' && c != '\t') kv->key[i++] = c;
                else if (i > 0) break;
            }
            kv->key[i] = 0;
            if (*p == '=') {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                i = 0;
                while (*p && *p != '\n' && *p != '\r' && i < CFG_VAL_LEN-1)
                    kv->val[i++] = *p++;
                /* Trim trailing spaces */
                while (i > 0 && (kv->val[i-1] == ' ' || kv->val[i-1] == '\t')) i--;
                kv->val[i] = 0;
                if (kv->key[0]) cur->count++;
            }
        }

        while (*p && *p != '\n') p++;
    }
    return 0;
}

/* ── Serializer ─────────────────────────────────────────────── */
int cfg_serialize(const cfg_file_t *cfg, char *buf, size_t bufsz) {
    size_t pos = 0;
    for (int s = 0; s < cfg->count; s++) {
        const cfg_section_t *sec = &cfg->sections[s];
        /* Section header */
        if (kstrcmp(sec->name, "global") != 0) {
            if (pos + kstrlen(sec->name) + 4 >= bufsz) break;
            buf[pos++] = '[';
            kstrcpy(buf + pos, sec->name); pos += kstrlen(sec->name);
            buf[pos++] = ']'; buf[pos++] = '\n';
        }
        for (int k = 0; k < sec->count; k++) {
            const cfg_pair_t *kv = &sec->entries[k];
            size_t need = kstrlen(kv->key) + kstrlen(kv->val) + 3;
            if (pos + need >= bufsz) break;
            kstrcpy(buf + pos, kv->key); pos += kstrlen(kv->key);
            buf[pos++] = '=';
            kstrcpy(buf + pos, kv->val); pos += kstrlen(kv->val);
            buf[pos++] = '\n';
        }
        buf[pos++] = '\n';
    }
    if (pos < bufsz) buf[pos] = 0;
    return (int)pos;
}

/* ── Get/Set ─────────────────────────────────────────────────── */
const char *cfg_get(const cfg_file_t *cfg, const char *section, const char *key) {
    for (int s = 0; s < cfg->count; s++) {
        if (kstrcmp(cfg->sections[s].name, section) != 0) continue;
        for (int k = 0; k < cfg->sections[s].count; k++) {
            if (kstrcmp(cfg->sections[s].entries[k].key, key) == 0)
                return cfg->sections[s].entries[k].val;
        }
    }
    return NULL;
}

const char *cfg_get_default(const cfg_file_t *cfg, const char *section,
                             const char *key, const char *def) {
    const char *v = cfg_get(cfg, section, key);
    return v ? v : def;
}

int cfg_set(cfg_file_t *cfg, const char *section, const char *key, const char *val) {
    /* Find or create section */
    cfg_section_t *sec = NULL;
    for (int s = 0; s < cfg->count; s++) {
        if (kstrcmp(cfg->sections[s].name, section) == 0) {
            sec = &cfg->sections[s]; break;
        }
    }
    if (!sec) {
        if (cfg->count >= CFG_MAX_SECTIONS) return -1;
        sec = &cfg->sections[cfg->count++];
        kstrcpy(sec->name, section);
        sec->count = 0;
    }

    /* Find or create key */
    for (int k = 0; k < sec->count; k++) {
        if (kstrcmp(sec->entries[k].key, key) == 0) {
            kstrcpy(sec->entries[k].val, val);
            cfg->dirty = 1;
            return 0;
        }
    }
    if (sec->count >= CFG_MAX_KEYS) return -1;
    kstrcpy(sec->entries[sec->count].key, key);
    kstrcpy(sec->entries[sec->count].val, val);
    sec->count++;
    cfg->dirty = 1;
    return 0;
}

/* ── Load/Save ───────────────────────────────────────────────── */
int cfg_load(cfg_file_t *cfg, const char *vfs_path) {
    int fd = vfs_open(vfs_path, O_RDONLY, 0);
    if (fd < 0) return -1;

    static char lbuf[8192];  /* static — stack is only 16KB */
    int n = vfs_read(fd, lbuf, sizeof(lbuf) - 1);
    vfs_close(fd);
    if (n <= 0) return -1;
    lbuf[n] = 0;

    return cfg_parse(cfg, lbuf, vfs_path);
}

int cfg_save(const cfg_file_t *cfg, const char *vfs_path) {
    static char sbuf[8192];
    int n = cfg_serialize(cfg, sbuf, sizeof(sbuf));
    if (n <= 0) return -1;

    int fd = vfs_open(vfs_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    vfs_write(fd, sbuf, (uint32_t)n);
    vfs_close(fd);
    return 0;
}

/* ── Global config init ─────────────────────────────────────── */
const char *sysconf_get(const char *section, const char *key) {
    return cfg_get(&g_syscfg, section, key);
}
int sysconf_set(const char *section, const char *key, const char *val) {
    return cfg_set(&g_syscfg, section, key, val);
}
void sysconf_save(void) {
    cfg_save(&g_syscfg, CFG_ROOT "/system.conf");
}

void config_init(void) {
    /* Load system config — VFS must be mounted first */
    cfg_load(&g_syscfg,  CFG_ROOT "/system.conf");
    cfg_load(&g_netcfg,  CFG_ROOT "/network.conf");
    cfg_load(&g_usercfg, CFG_ROOT "/users.conf");
    cfg_load(&g_pkgcfg,  CFG_ROOT "/packages.conf");
}
