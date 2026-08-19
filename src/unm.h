#ifndef UNM_H
#define UNM_H

/*  unm.h — UserNet Manager for atmkoala
 *
 *  Manages network profiles (static / DHCP-sim), provides:
 *    unm_init()          — load saved profile from config
 *    unm_connect()       — apply the active profile
 *    unm_disconnect()    — bring interface down
 *    unm_status()        — print current status to console
 *    unm_set_static()    — configure static IP/GW/DNS
 *    unm_dhcp_request()  — send DHCP Discover + handle Offer (QEMU SLIRP)
 *    unm_save_profile()  — persist current settings to /uiu/etc/network.conf
 *    unm_load_profile()  — load from /uiu/etc/network.conf
 *
 *  DHCP implementation is minimal but functional under QEMU SLIRP:
 *    - Sends a DHCP Discover (UDP broadcast)
 *    - Waits up to ~3 s for a DHCP Offer
 *    - Parses offered IP / lease / gateway from options
 *    - Sends DHCP Request + waits for ACK
 *
 *  Static mode just calls net_set_ip() directly.
 */

#include <stdint.h>
#include <stddef.h>

/* ── Connection modes ──────────────────────────────────────── */
typedef enum {
    UNM_MODE_NONE   = 0,
    UNM_MODE_DHCP   = 1,
    UNM_MODE_STATIC = 2,
} unm_mode_t;

/* ── Connection state ──────────────────────────────────────── */
typedef enum {
    UNM_STATE_DOWN        = 0,
    UNM_STATE_CONNECTING  = 1,
    UNM_STATE_UP          = 2,
    UNM_STATE_FAILED      = 3,
} unm_state_t;

/* ── A network profile ─────────────────────────────────────── */
#define UNM_PROFILE_NAME_LEN  32
#define UNM_MAX_PROFILES       8

typedef struct {
    char        name[UNM_PROFILE_NAME_LEN];
    unm_mode_t  mode;
    uint8_t     ip[4];
    uint8_t     netmask[4];
    uint8_t     gateway[4];
    uint8_t     dns[4];
    int         active;     /* 1 = this is the saved default */
} unm_profile_t;

/* ── Global UNM state ──────────────────────────────────────── */
typedef struct {
    unm_state_t   state;
    unm_profile_t profiles[UNM_MAX_PROFILES];
    int           profile_count;
    int           active_profile; /* index, -1 if none */
    uint8_t       leased_ip[4];
    uint8_t       leased_gw[4];
    uint8_t       leased_dns[4];
    uint32_t      lease_time;   /* seconds */
} unm_state_block_t;

extern unm_state_block_t g_unm;

/* ── Public API ────────────────────────────────────────────── */
void       unm_init(void);

/* Connect using the active profile (or mode override) */
int        unm_connect(void);
void       unm_disconnect(void);

/* DHCP: send Discover→Request, return 0 on success */
int        unm_dhcp_request(void);

/* Static setup */
void       unm_set_static(uint8_t ip[4], uint8_t nm[4],
                           uint8_t gw[4], uint8_t dns[4]);

/* Profile management */
int        unm_profile_add(const char *name, unm_mode_t mode,
                            uint8_t ip[4], uint8_t nm[4],
                            uint8_t gw[4], uint8_t dns[4]);
int        unm_profile_set_active(int idx);
void       unm_profile_list(void);

/* Persist / load */
void       unm_save_profile(void);
void       unm_load_profile(void);

/* Status to console */
void       unm_status(void);

/* Helper: format IP to string */
void       unm_ip_str(const uint8_t ip[4], char *out, int sz);

/* DNS resolution stub (returns static IP for known QEMU hosts) */
int        unm_dns_resolve(const char *hostname, uint8_t out_ip[4]);

#endif /* UNM_H */
