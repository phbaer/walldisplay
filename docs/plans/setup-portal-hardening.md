# Setup portal hardening

Status: implemented, hardware acceptance outstanding.

## Fixed feature selection

- Branch: `fix/setup-portal-hardening`.
- Base: `5ea636c` on `feature/ipv6` (release `1.3.0`, MQTT contract `12`).
- Selected release: `1.3.0`, the release already selected by `feature/ipv6`. `main` is at `1.2.0`, and these fixes ship in the same next release, so the version is not raised again.
- MQTT contract: unchanged at `12`. No topic, payload, or discovery entity changes.
- Retain these selected versions for all follow-up fixes on this branch.

Both Home Assistant delivery paths are unaffected apart from the generated
version references; the setup portal is firmware-only.

## Problems and fixes

| Problem | Fix |
| --- | --- |
| The setup AP password was `wd-` plus four MAC bytes, which the AP's BSSID broadcasts. | The password is drawn from the hardware RNG once per boot (about 59 bits) and shown only on the display and serial console. |
| The portal listened on every interface, so station-network clients could reach it while the AP was enabled. | Every handler refuses requests whose local address is not the setup AP address. |
| A request that omitted `mqtt_password` reused the stored password whenever the broker URI was unchanged, even with a different Wi-Fi network or CA certificate. | The stored MQTT password is reused only when SSID, URI, username, and CA are unchanged and TLS is not switched off. |
| The broker URI, which may embed credentials, was logged. | Only its presence is logged. |
| The setup page escaped the stored CA with `sizeof(pointer)`, rendered an empty field, and so removed the CA on the next save. | The real buffer size is used; the page is rejected instead of truncated if it does not fit. |
| A URL-encoded CA larger than the decoded limit was silently replaced by the stored certificate; other oversized fields fell back the same way. | Fields are extracted into a body-sized buffer, decoded, then length-checked; an oversized value returns `400`. |
| The request body leaked when `ssid` or `name` was missing. | The body is released as soon as the fields are parsed. |
| The idle timeout stopped the AP of an unprovisioned panel. | The timeout applies only to an AP enabled on demand. |
| The captive DNS socket was closed while its task was blocked in `recvfrom()`. | The task owns the socket, polls with a receive timeout, and closes it itself. |

The portal still has no login of its own: the WPA2 password of the setup AP,
readable only on the panel, is its access control. Plain HTTP inside the AP
network and unencrypted NVS remain as documented in
[Security configuration and firmware signing](../how-to/security-and-signing.md).

## Verification (2026-11-17)

- ESP-IDF 6.0.2 unsigned firmware build passed.
- Host tests compile the portal's password, escaping, and form-field helpers
  from `src/wifi_manager.c` and check the access guard, trust rule, and timeout
  condition in every handler; the Python suite and native unit checks passed.
- Not verified: no panel was flashed. The access guard depends on the local
  address lwIP reports for the dual-stack server socket, and the RNG, DNS
  restart, and timeout behavior depend on the Wi-Fi driver. Run the rows added
  to the [SoftAP acceptance procedure](../acceptance/softap-provisioning.md)
  before release.
