# Threat model

Status: accepted for 1.3.0. Reviewed against the code on 2026-11-18. This
document records what the firmware protects, from whom, and which risks are
accepted. The [security configuration guide](../how-to/security-and-signing.md)
gives the settings; this document explains why they are the way they are.

## Assets

- **Panel control:** commands on `<base>/cmd/...` (page, sync, reboot, OTA,
  screenshots, configuration).
- **Stored credentials:** Wi-Fi password, MQTT username and password, the
  screenshot token, and the custom MQTT CA in runtime NVS.
- **Firmware trust:** the RSA-3072 signing key (kept outside the repository) and
  the signed-image check that enforces it.
- **Display content:** what the panel shows, including the setup AP password
  while the AP is active.

## Actors considered

| Actor | Capability assumed |
| --- | --- |
| Neighbour or passer-by | Within Wi-Fi range; can see the display and probe the AP. |
| Station-network client | On the same Wi-Fi as the panel, not trusted. |
| MQTT publisher | Any client that can publish to the broker. |
| Physical attacker | Can read or modify flash with the device in hand. |
| Upstream release host | Could serve a wrong manifest or image. |

Physical flash access, hardware Secure Boot, and flash encryption are out of
scope; the firmware does not enable them (see the security guide).

## Setup access point decision

While the setup AP is active, the setup portal is the way to configure the
panel. The decision is:

- **Access control is the AP's WPA2 passphrase.** It is random for each boot
  (12 symbols from a 31-symbol alphabet, about 59 bits), is not derived from the
  SSID, BSSID, or MAC address, and is shown only on the About page and the
  serial console. The portal has no separate login.
- **The portal answers only AP clients.** A request whose local address is not
  the AP address receives `403`, so a station-network client cannot reach it
  even while the AP is up.
- **Recovery needs no extra tooling.** Holding the AP passphrase lets an
  operator reconfigure the panel or use `/api/reset`, which erases the stored
  settings and returns to setup. The panel has no button, so a separate
  recovery credential would only add a secret that someone must also store.
- **The AP lifetime depends on how it started.** An AP enabled over MQTT stops
  after 15 minutes without portal activity. An unprovisioned panel keeps its
  AP up until it is configured, because the AP is the only way to configure it.

### Accepted risks

- Anyone who can read the display, or the serial console, while the AP is up
  can configure the panel, including reset it. The physical proximity needed
  is the same as for the Wi-Fi password printed on a router label.
- An MQTT publisher with `cmd/config/ap` rights can enable the AP on demand.
  The broker ACL is the control; see `config/mosquitto-acl.example`.
- The portal and its traffic are plain HTTP inside the AP's WPA2 link. A client
  that knows the passphrase can read stored settings only through the portal's
  own outputs, which never include secrets; the AP is not a confidentiality
  boundary against a holder of its passphrase.
- Failed-attempt limiting is five POSTs per minute. It slows a guessing run of
  the passphrase but does not lock the AP.

### Alternatives considered

- **Portal login with a PIN shown on the display.** The PIN has the same
  exposure as the passphrase and adds an input step with no security gain.
- **A physical button on the panel.** The panel has touch only; adding a button
  is a hardware change.
- **Stop the AP after first configuration.** Already the case for an AP that
  was enabled over MQTT. A panel that is never configured keeps its AP up by
  design.

## Other controls

- MQTT commands from retained messages are rejected (see the security guide).
- Stored MQTT passwords are reused for a changed broker only when the SSID,
  URI, username, and CA are unchanged, so a portal client cannot send them to
  a broker it controls.
- OTA images must carry a valid RSA-3072 signature from the trusted key and
  match the manifest hash and size. An unsigned build rejects update requests.
- Screenshot tokens are compared in constant time and never echoed.

## Open items

- The signing key is still kept in the repository root on the development
  machine. Move it to protected storage outside the repository.
- Hardware evidence for the setup AP rows is pending; see
  [IPv6 implementation and acceptance notes](../plans/ipv6.md) and
  [SoftAP provisioning acceptance](../acceptance/softap-provisioning.md).
