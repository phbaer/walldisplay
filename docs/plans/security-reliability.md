# Security and reliability release

Base: `8b7fd8e` (configurable pages). Branch: `feat/security-reliability`.
Fixed release: **1.0.0**, MQTT contract **7**. This is a major release because unsigned OTA and unauthenticated screenshots are no longer accepted.

Implementation scope:

1. Fix OTA stack use, require ESP-IDF signed-app verification, provide a signing workflow, and enforce an OTA verification deadline.
2. Add MQTT TLS trust configuration, Last Will, retained-command rejection, strict fragment assembly, and broker ACL guidance.
3. Repair footer routing and artwork caching; bound downloads and decoding, serialize latest artwork, isolate synchronization jobs.
4. Authenticate screenshot downloads, allow disabling captures, bound stored captures, and update tooling.
5. Move HA configuration definitions/validation outside flows with explicit migrations; extract weather and grid rendering; emit typed UI actions through a transport adapter.
6. Add targeted host/HA tests, inspect compiled stack sizes, build ordinary and signed firmware, parse YAML, synchronize deployment copies, and update README migration guidance.

No hardware secure-boot or encryption fuses will be changed. Signing keys are external inputs, never committed or bundled with source. Hardware touch/reboot/update validation remains separate from host tests.

## Validation

- Ordinary and signed ESP-IDF 6.0.2 builds pass (23% application partition space free).
- Both compiled OTA caller/download frames total 1,120 bytes, down from 9,328; the automated budget is 2,048. Deeper HTTP/TLS usage is observable through runtime stack low-water logging.
- A disposable-key signed firmware passes signature verification; tampered firmware and an unrelated signer are rejected. The signing build has signed-update verification enabled and hardware Secure Boot disabled.
- Six native test executables pass, including simulated NVS reboot/failed-commit recovery, MQTT fragment assembly, replay/authentication policy, and artwork request races/retries.
- Nineteen Home Assistant/Python tests pass against Home Assistant 2026.9.2.
- Blueprint, workflow and example YAML parse; expanded blueprint schema validates; deployment copies match; README local links and diff whitespace checks pass.

Hardware signing/bootstrap, touch/rendering, reboot deadline, TLS broker connectivity and live Home Assistant frontend behaviour still require deployment testing. No firmware was flashed, broker configured, HA integration installed, eFuse changed or production signing key created. The signed build artifact uses a disposable test key and is only a validation artifact.

The blueprint cannot inspect the retained MQTT flag in Home Assistant trigger data; it depends on non-retained action publishers. Firmware command rejection applies to both paths; the integration additionally rejects retained incoming panel actions. This limitation is documented in the README.
