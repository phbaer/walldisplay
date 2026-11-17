# SoftAP provisioning acceptance

This is the physical sign-off procedure for a generic signed factory image. It
is intentionally separate from automated unit tests because several checks
require a real panel, a second Wi-Fi network, and a broker.

## Artifact gate

Run this before touching a device:

```sh
PYTHONPATH=. uv run python tools/acceptance_check.py \
  --factory dist/walldisplay-<version>-factory.bin \
  --image build/walldisplay.bin \
  --manifest dist/walldisplay-<version>.json
```

For a release image, also run `python tools/check_signed_firmware.py` with the
trusted public signing key. Never use a build containing real credentials or
tokens as the generic artifact.

## Clean-device test

Record the image version, panel MAC, tester, date, and broker used. Flash the
merged factory image to a panel with erased application configuration. Confirm that
the panel starts a WPA2 AP with a device-specific SSID and a random password
shown on the About page, that the password differs after a reboot and cannot be
derived from the SSID or BSSID, and that `http://192.168.4.1/` loads from a phone. Use **Refresh networks**, select an
SSID, and verify that the password field stays unchanged. Confirm that the
page shows a human display name and the generated hostname separately.

Submit Wi-Fi with MQTT fields empty. Verify that a valid Wi-Fi connection is
saved, MQTT remains reported as unconfigured, and the AP stops after reboot.
Then enable the AP through the authenticated MQTT command, configure MQTT, and
verify discovery and the hostname sensor in both supported Home Assistant
delivery paths (one path per panel).

## Negative and recovery tests

Run each test with the previous known-good configuration present:

| Test | Expected result |
| --- | --- |
| Wrong Wi-Fi password | Connection test fails; previous settings remain active. |
| Unreachable or invalid MQTT broker | Wi-Fi is retained; candidate MQTT settings are rejected or reported unavailable. |
| Oversized/control-character submission | HTTP request is rejected; no NVS change. |
| Five or more rapid submissions | Rate limiting responds with an error; portal stays responsive. |
| Power removed during save | On reboot, NVS contains either the old complete record or the new complete record; AP recovery remains possible. |
| MQTT AP command `ON` twice | One AP session; no HTTPD bind error. |
| AP inactivity (configured panel) | An AP enabled over MQTT expires after 15 minutes without portal activity; a request resets the timer. |
| AP inactivity (unprovisioned panel) | The AP is still available after more than 15 minutes without portal activity. |
| Portal through the station network | With the AP enabled on a configured panel, `http://<station-ip>/` and `POST /api/reset` return `403`; the same requests through `192.168.4.1` work. |
| Stored custom CA | The setup page shows the stored certificate; saving without editing keeps it, clearing the field removes it. |
| CA certificate near 3072 bytes | Save succeeds; a longer certificate is rejected with `400` and nothing changes. |
| Crafted request without `mqtt_password`, changed SSID or CA | The stored MQTT password is not sent; the MQTT test fails authentication. |
| AP `OFF` then `ON` repeatedly | Captive DNS answers after every restart; no leaked task or socket. |
| Reset/reprovision | Deliberate reset flow clears credentials and returns to setup AP. |

## OTA sign-off

From the provisioned panel, install a signed update through Home Assistant
Repairs. Confirm HTTPS, manifest hash/size, and signature verification, and
that the panel reports the new version. Test a deliberately non-booting signed
image in a controlled environment: ESP-IDF rollback must restore the previous
working image, and the panel must reconnect to MQTT afterward. Do not use an
unsigned image or burn Secure Boot/anti-rollback eFuses as part of this test.

Record pass/fail evidence in the release issue. A release is production-ready
only when the artifact gate and all clean-device, negative, recovery, and OTA
checks pass on the target panel revision.
