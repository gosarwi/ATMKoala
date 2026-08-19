#ifndef DNS_H
#define DNS_H
/*
 * dns.h — DNS resolver for atmkoala (RFC 1035, A records only)
 *
 * Sends a single iterative query to the configured DNS server (UNM's
 * leased_dns, or a caller-supplied server) over UDP port 53, parses
 * the response, and returns the first A-record answer found.
 *
 * No recursion, no caching, no CNAME chasing beyond a small fixed
 * number of hops, no DNSSEC — this is the minimum viable client a
 * `ping hostname` or `unm dns` command actually needs.
 */
#include <stdint.h>

/* Resolve `hostname` against `server_ip` (typically the DHCP-leased
 * DNS server). Returns 0 and fills `out_ip` on success, -1 on
 * timeout/NXDOMAIN/malformed response. */
int dns_resolve(const char *hostname, const uint8_t server_ip[4],
                uint8_t out_ip[4]);

#endif /* DNS_H */
