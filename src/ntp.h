#ifndef ATM_NTP_H
#define ATM_NTP_H

#include <stdint.h>

/* A manual, one-request SNTP/NTPv4 client over the bounded kernel UDP helper.
 * It applies a volatile UTC correction only after a validated server reply. */
typedef struct {
    uint8_t server_ip[4];
    int64_t unix_seconds;
    uint8_t stratum;
    uint32_t roundtrip_ticks;
} ntp_result_t;

/* Server may be an IPv4 literal or a DNS name resolvable by the active UNM
 * profile. No background retries, scheduling or persistent RTC writeback. */
int ntp_sync_once(const char *server,ntp_result_t *out);
int ntp_selftest(void);

#endif
