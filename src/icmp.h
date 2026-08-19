#ifndef ICMP_H
#define ICMP_H
/*
 * icmp.h — ICMP echo request/reply (ping) for atmkoala
 *
 * Implements just enough of RFC 792 to support a working `ping`
 * command: type 8 (echo request) out, type 0 (echo reply) in,
 * matching identifier + sequence number, round-trip time measured
 * against the PIT tick counter.
 */
#include <stdint.h>

typedef struct {
    int      sent;
    int      received;
    uint32_t min_rtt_ms, max_rtt_ms, total_rtt_ms;
} icmp_ping_stats_t;

/* Send one echo request to `dst_ip`, wait up to `timeout_ticks`
 * (PIT ticks, 100/s) for the matching reply. Returns round-trip
 * time in milliseconds, or -1 on timeout/failure. */
int icmp_ping_once(const uint8_t dst_ip[4], uint16_t seq, uint32_t timeout_ticks);

/* Send `count` echo requests, 1 per second, printing each result —
 * this is what the `ping` shell command calls directly. */
void icmp_ping(const uint8_t dst_ip[4], int count);

#endif /* ICMP_H */
