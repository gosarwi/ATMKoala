#ifndef ATM_NET_TCP_H
#define ATM_NET_TCP_H

#include <stdint.h>

#define ATM_TCP_MSS 512
#define ATM_TCP_OOO_MAX 2

/* Initial bounded TCP client contract. No listen/accept, no fragmentation,
 * no congestion window and no TLS. The API remains kernel-internal until the
 * state machine has QEMU regressions. */
typedef enum {
    ATM_TCP_CLOSED = 0,
    ATM_TCP_LISTEN,
    ATM_TCP_SYN_SENT,
    ATM_TCP_SYN_RECEIVED,
    ATM_TCP_ESTABLISHED,
    ATM_TCP_FIN_WAIT,
    ATM_TCP_ERROR
} atm_tcp_state_t;

typedef struct {
    uint8_t remote_ip[4];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t seq;
    uint32_t ack;
    uint32_t retry_count;
    uint16_t peer_window;
    uint16_t local_window;
    uint16_t cwnd;
    uint16_t ssthresh;
    struct { uint32_t seq; uint16_t len; uint8_t used; uint8_t data[ATM_TCP_MSS]; } ooo[ATM_TCP_OOO_MAX];
    atm_tcp_state_t state;
} atm_tcp_conn_t;

int atm_tcp_listen(atm_tcp_conn_t *listener,uint16_t local_port);
int atm_tcp_accept(atm_tcp_conn_t *listener,atm_tcp_conn_t *accepted,uint32_t timeout_ticks);
int atm_tcp_connect(atm_tcp_conn_t *conn, const uint8_t remote_ip[4],
                    uint16_t remote_port, uint32_t timeout_ticks);
int atm_tcp_send(atm_tcp_conn_t *conn, const void *data, uint16_t len);
int atm_tcp_recv(atm_tcp_conn_t *conn, void *data, uint16_t cap,
                 uint32_t timeout_ticks);
int atm_tcp_close(atm_tcp_conn_t *conn);
int atm_tcp_selftest(void);

#endif
