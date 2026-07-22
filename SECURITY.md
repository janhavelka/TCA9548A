# Security Policy

## Supported Versions

Only the latest published `1.0.x` patch is supported with security fixes.
Development commits and superseded patches are not supported releases.

| Version | Supported |
| --- | --- |
| Latest 1.0.x patch | Yes |
| Older releases and development commits | No |

## Reporting a Vulnerability

Do not open a public issue for a suspected vulnerability. Email
`info@thymos.cz` with:

- the affected version or exact commit;
- a concise description and potential impact;
- reproduction steps or a minimal test case; and
- any suggested mitigation, if available.

The maintainers target acknowledgement within 48 hours and a fix or mitigation
plan within 14 days for confirmed critical issues. Actual timing can depend on
hardware reproduction and coordinated disclosure requirements.

## Security Boundary

This library controls an I2C switch; it does not authenticate devices, encrypt
traffic, validate downstream payloads, or recover the shared controller. The
application remains responsible for bus serialization, callback lifetime,
deadline enforcement, retry/admission policy, watchdogs, board-level RESET,
safe route cleanup, and validation of any externally supplied channel or mask.

Relevant defensive properties are:

- no dynamic allocation in steady-state library paths;
- no network, filesystem, persistent-storage, logging, or task dependency;
- finite callback counts and caller-supplied timeouts for hardware operations;
- distinct transport errors and explicit ambiguous-mask provenance; and
- no automatic reconnect of a previous route after recovery or RESET.

Transport callbacks are trusted code. They must honor the supplied timeout,
complete STOP before returning success, preserve the documented error mapping,
and remain valid until `end()`. The driver is not thread-safe, reentrant, or
ISR-safe.

Users should pin a reviewed full commit SHA as described in
[README.md](README.md), qualify the real electrical topology, and run live HIL
for routing, isolation, RESET, stuck-bus recovery, and soak behavior before
production deployment.
