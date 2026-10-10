# Home Assistant screenshot diagnostics

Feature branch: `feature/ha-screenshot-diagnostics`.
Base: `120b953` on `main` (onboarding release 1.1.0, contract 10; squash of the original `11490dd` base).
Selected release: **1.2.0**. Selected MQTT contract: **11**.
Keep these values unchanged for all follow-up commits on this branch.

Expose IPv4 and both IPv6 address diagnostics, a command-only password token setting, capture button, and structured screenshot status through firmware MQTT Discovery. Both synchronization alternatives use the same entities and retain their existing screenshot-enable configuration. Tokens persist without reboot and are never returned in MQTT state. Capture errors identify the failing stage and ESP-IDF error, including rejected requests.

Live validation on 10.0.2.73 reproduced a successful first capture followed by an error for the same name on the pre-change firmware. SPIFFS rejects rename over an existing destination; the BMP writer now removes the old destination only after completing the temporary file, under the storage lock. A native regression test simulates SPIFFS rename semantics and verifies replacement pixels. Updated firmware has been built but not installed on the panel.
