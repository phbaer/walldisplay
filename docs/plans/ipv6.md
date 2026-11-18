# IPv6 implementation and acceptance

## Fixed feature selection

- Branch: `feature/ipv6`.
- Base: `af94b5f` (release `1.2.0`, MQTT contract `11`).
- Selected release: `1.3.0` (backward-compatible networking capability).
- Selected MQTT contract: `12` (IPv6 screenshot URLs and ULA-aware IPv6 diagnostics).
- Retain these selected versions for all follow-up fixes on this branch.

## Implementation

Committed defaults enable IPv6, SLAAC, RDNSS with two DNS server slots, and
ESP-IDF's dual-family resolver. The station creates link-local IPv6 on
association. Startup and provisioning accept IPv4 or a preferred ULA/global
IPv6 address; link-local-only connectivity does not pass. The background startup
worker handles delayed initial connectivity without starting normal MQTT during
an unconfigured panel's temporary portal connection tests.

`network_client` shares address selection across MQTT (including portal probes),
artwork, OTA, screenshots, About, and device diagnostics. It prefers IPv4 for
outbound connections when present, with sequential IPv6 fallback on connection
failure. On IPv6-only stations the order is reversed. `network_family_t` keeps
the family that last connected: only an attempt that never connected moves to
the other family, so a dropped MQTT session and later HTTP range requests start
with the family that worked. IPv4 literals and bracketed IPv6 literals use only
their own family. TLS URLs remain unchanged, preserving hostname verification,
SNI, and existing CA/signature policy. Range headers survive OTA connection
fallback; failed HTTP clients are cleaned up before retrying. Body validation
failures abort instead of restarting a partially written image.

The existing `ipv6_global` diagnostic selects ULA before global addresses.
HTTP status URLs use IPv4 or bracketed ULA/global IPv6. Automatic Home Assistant
previews try reported private IPv4 and IPv6 ULA addresses and reject public,
link-local, unspecified, loopback, and multicast IPv6 destinations. Both full
integration and passive blueprint preview companions use the same proxy.
The IPv4 setup AP, action ownership differences between the two synchronization
paths, and existing MQTT commands remain supported. Matter is a separate future
feature.

## Automated validation (2026-11-17)

Checked against the branch head after the remembered-family follow-up:

- ESP-IDF 6.0.2 unsigned firmware build with a freshly generated SDK
  configuration passed; the generated SLAAC, RDNSS, and dual-family resolver
  settings were checked. The 2,454,528-byte application fits the 3 MiB slots.
- Locked Python suite: 60 tests and 19 subtests passed, including the host
  build of `network_client.c` that covers family memory and address literals.
- Native firmware unit checks passed.
- OTA direct stack guard: 1,392 bytes, below the 2,048-byte budget.
- Version/contract alignment, integration mirror, and whitespace checks passed.
- A signed build was not repeated on this date. No panel was flashed and
  hardware acceptance below remains outstanding.

## Hardware status (2026-11-18)

Firmware `v1.3.0-rc.1` (commit `93051d0`) on one panel, connected to a router
that advertises SLAAC prefixes with RDNSS on the Wi-Fi network. The panel also
has IPv4 from DHCP. Observed in the panel's serial log and device state; the
addresses are redacted here.

| Observed | Result |
| --- | --- |
| Link-local address on association | Yes: the station created a link-local address. |
| Global and ULA addresses from router advertisements | Yes: one global (2000::/3) and one ULA (fd00::/8) address, both with the panel's interface identifier. |
| IPv4 with IPv6 present | MQTT connected. The log does not show the family used; the firmware prefers IPv4 when the station has it. |
| Firmware update to the RC1 image | Yes, but on the previous firmware (`v1.2.0-rc.3`, no IPv6): the signed 2,560,000-byte image downloaded over IPv4 and verified, and the panel rebooted into RC1. This shows the update path, not the IPv6 code. |
| Disconnect, reconnect, or address expiry | Not run. |

Matrix rows not run on hardware yet: the IPv6-only networks, the RA after 15
seconds, the failing-IPv4 fallback, the AAAA-only hostname, the
Wi-Fi-failure and wrong-certificate cases, the screenshot preview over ULA,
and the factory reset on an IPv6 deployment. The "dual-stack, A + AAAA" row is
only partly covered: the panel showed both IPv6 address kinds, but the release
host's AAAA behaviour was not exercised.

Earlier, the same panel's first update attempts failed because the manifest URL
arrived as a bare scheme, and then because the release host was blocked on the
panel's network. Both were fixed in the network or the request path, not in the
IPv6 code, and are recorded in the update button change.

## Hardware acceptance matrix

Automated builds and host tests cannot establish router behavior, address
lifetimes, Wi-Fi driver events, or physical display timing. Run this matrix on
a panel before releasing:

| Network / fault | Expected result |
| --- | --- |
| IPv4-only, no RA | Existing provisioning, MQTT, artwork, OTA, screenshots work. |
| Dual-stack, A + AAAA | IPv4 remains first; both IPv6 diagnostics appear. |
| IPv6-only ULA, SLAAC + RDNSS | Portal test/save succeeds, MQTT starts, LAN artwork and previews work. |
| IPv6-only global, SLAAC + RDNSS | MQTT/artwork/OTA work with reachable AAAA hosts; direct screenshot URL works; automatic preview requires ULA. |
| Link-local only | Wi-Fi test fails; normal configured startup waits without claiming readiness. |
| Broken/unscoped AAAA + working A | IPv4 operation and signed OTA remain successful. |
| Failing IPv4 connection + working AAAA | HTTP retries IPv6 and keeps it for later requests; MQTT uses IPv6 on reconnect and stays there; HTTPS verification still succeeds. |
| Broker restart or Wi-Fi drop on IPv4-only LAN | MQTT reconnects over IPv4 on the first attempt. |
| AAAA-only hostname on dual stack | IPv4 resolution failure falls back to IPv6. |
| RA arrives after 15 seconds | MQTT starts later without reboot; display remains responsive. |
| Disconnect/reconnect, prefix change/expiry | Readiness follows live addresses, diagnostics refresh, MQTT reconnects, previews use new hosts. |
| Candidate Wi-Fi fails | Stored settings remain intact and prior connection is restored. |
| Candidate Wi-Fi succeeds, MQTT fails in both families | Portal reports failure and restores prior station settings. |
| Wrong certificate or MQTT credentials | No TLS downgrade; connection test fails and no configuration is committed. |
| Factory reset on IPv6 deployment | Credentials are erased and IPv4 setup AP is available. |

Also verify signed manifest/range downloads, image-size/hash/signature failure,
OTA rollback, and maximum-length screenshot names over IPv6. Use a DNS hostname
with a matching certificate for TLS; use bracketed literals for direct IPv6
HTTP. This feature does not provide stateful DHCPv6, DHCPv6-only DNS, mDNS
advertisement, interface-scoped link-local URLs, or NAT64/DNS64 configuration.
