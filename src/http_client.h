#ifndef ATM_HTTP_CLIENT_H
#define ATM_HTTP_CLIENT_H

#include <stdint.h>
#include <stddef.h>

/* Small kernel-internal HTTP/1.0 client for the package repository path.
 * It intentionally supports only clear-text HTTP with a bounded Content-Length
 * response. HTTPS/TLS, redirects, authentication, chunked transfer encoding,
 * proxy handling and persistent connections are rejected rather than guessed. */
#define ATM_HTTP_URL_MAX       255u
#define ATM_HTTP_HOST_MAX      127u
#define ATM_HTTP_PATH_MAX      127u
#define ATM_HTTP_RESPONSE_MAX  (128u * 1024u)
#define ATM_HTTP_TIMEOUT_TICKS 300u

typedef struct {
    uint32_t status_code;
    uint32_t content_length;
    uint32_t body_length;
    char host[ATM_HTTP_HOST_MAX + 1u];
    char path[ATM_HTTP_PATH_MAX + 1u];
} atm_http_response_t;

/* Downloads one HTTP resource into `out`. The caller owns `out`; no data is
 * retained on failure. `host` may be a dotted IPv4 string or a hostname that
 * the configured UserNet DNS resolver can resolve. */
int atm_http_get(const char *url,uint8_t *out,uint32_t cap,atm_http_response_t *response);

/* Parser-only regression for status/header bounds. It performs no network I/O. */
int atm_http_selftest(void);

#endif
