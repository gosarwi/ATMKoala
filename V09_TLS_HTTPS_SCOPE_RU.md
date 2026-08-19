# ATMKoala v0.9 — TLS/HTTPS и curl: безопасная граница интеграции

TLS нельзя безопасно представить как «ещё один HTTP parser». TLS 1.3 обеспечивает аутентификацию сервера, конфиденциальность и целостность поверх надёжного упорядоченного transport stream [1]. Для curl certificate verification должен проверять цепочку до доверенного CA и имя сервера; отключение verification не является production mode [2].

## ATMKoala prerequisites

| Required layer | Current ATMKoala status | Gate before HTTPS |
|---|---|---|
| TCP transport | Experimental native TCP with bounded client/listener paths | Live interoperability, retransmit/window/OOO regression coverage |
| Socket I/O | Native task-local experimental API | Blocking read/write timeout semantics and user DNS ABI |
| Entropy | No audited cryptographic strong entropy source | Hardware/approved entropy source and seed lifecycle |
| Time | No trusted RTC/time service for X.509 validity | Audited wall-clock and monotonic time policy |
| Certificates | No X.509 parser or CA bundle | PEM/DER parser, trust-anchor store, hostname and validity checks |
| TLS crypto | No reviewed AEAD/HKDF/ECDHE/signature provider | A maintained, appropriately licensed TLS backend with native platform callbacks |

Mbed TLS documents that a bare-metal port may replace networking, timing, entropy, filesystem and time dependencies through platform-specific callbacks; its entropy module refuses output until a declared-strong source is registered [3]. That architecture is appropriate for a future ATMKoala port, but must not be mistaken for completed HTTPS support.

## Planned integration boundary

The future implementation should vendor and attribute a maintained TLS backend, compile it with its hosted network module disabled, then connect its BIO callbacks to the native socket layer. The first HTTPS milestone must require `https://` hostname verification against a read-only CA bundle stored in VFS. There will be no default insecure mode. curl remains audit-only until the transport and TLS gates above pass.

## References

[1]: https://datatracker.ietf.org/doc/html/rfc8446 "RFC 8446: TLS 1.3"
[2]: https://curl.se/docs/sslcerts.html "curl: TLS Certificate Verification"
[3]: https://mbed-tls.readthedocs.io/en/latest/kb/how-to/how-do-i-port-mbed-tls-to-a-new-environment-OS/ "Mbed TLS: Porting to a new environment or OS"
