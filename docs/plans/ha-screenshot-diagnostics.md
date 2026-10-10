# Home Assistant screenshot diagnostics

Feature branch: `feature/ha-screenshot-diagnostics`.
Base: `120b953` on `main` (onboarding release 1.1.0, contract 10; squash of the original `11490dd` base).
Selected release: **1.2.0**. Selected MQTT contract: **11**.
Keep these values unchanged for all follow-up commits on this branch.

Expose IPv4 and both IPv6 address diagnostics, a command-only password token setting, capture button, and structured screenshot status through firmware MQTT Discovery. Both synchronization alternatives use the same entities and retain their existing screenshot-enable configuration. Tokens persist without reboot and are never returned in MQTT state. Capture errors identify the failing stage and ESP-IDF error, including rejected requests.

Live validation on 10.0.2.73 reproduced a successful first capture followed by an error for the same name on the pre-change firmware. SPIFFS rejects rename over an existing destination; the BMP writer now removes the old destination only after completing the temporary file, under the storage lock. A native regression test simulates SPIFFS rename semantics and verifies replacement pixels. Updated firmware has been built but not installed on the panel.

## Complete device controls

The maintainer expanded this same feature to expose all canonical MQTT commands and writable inputs and every diagnostic payload on the physical device. Retain release 1.2.0 and contract 11. Add current-page telemetry and accepted-grid canonical state; preserve secrets as command-only values. Firmware discovery serves both synchronization alternatives, and native integration entities share its device identifier while keeping their unique IDs. HA text entities support 255 characters; larger JSON remains available as attributes and through existing configuration/MQTT publishing. Manual overrides may be restored by the selected synchronization path. Validate the actual discovery payloads against installed Home Assistant schemas and independently enumerate endpoint coverage.

Control expansion validation: 50 Python tests and native tests passed, including compilation of actual firmware discovery, validation of 139 entity payloads against Home Assistant schemas, all canonical endpoint coverage, common device identifiers, zero-preserving display templates, and media volume conversion. Firmware build, YAML parsing, component/version consistency, diff checks, and OTA stack guard passed. HA frontend/entity migration behavior and live firmware controls have not been verified on a running Home Assistant instance; firmware remains unflashed.
