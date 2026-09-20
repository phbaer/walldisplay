# SoftAP provisioning handover

This document hands the next feature branch the work needed to make a generic signed factory image usable by a normal user. It is deliberately separate from the current release work. The current firmware reads Wi-Fi and MQTT settings from build-time defaults, rejects the example Wi-Fi placeholder, and has no first-boot network configuration path.

## Desired user experience

1. The user downloads a signed factory image and flashes it over USB.
2. A panel with no valid stored network configuration starts a local setup access point, for example `WallDisplay-AB12`.
3. The user connects a phone or laptop, opens the captive portal at `192.168.4.1`, and enters Wi-Fi and MQTT settings.
4. The panel validates the settings, stores them, stops the access point, and connects to Home Assistant through MQTT Discovery.
5. The user installs either the MQTT Sync blueprint or the `walldisplay_sync` integration and completes the display configuration.
6. Later signed firmware updates continue through the integration's Home Assistant Repairs flow.

The portal must configure all values required before MQTT is available: Wi-Fi SSID/password, MQTT URI, MQTT username/password, TLS mode, optional CA certificate, discovery prefix, base topic, and the optional screenshot token. Page layout and display settings can remain Home Assistant configuration, but accepting them in the portal is useful for a fully standalone first boot.

## Current implementation constraints

- `src/wifi_manager.c` starts station mode directly from `app_config_get()`.
- `src/app_config.c` reads build defaults from the generated header and optional read-only `appcfg` NVS, then reads writable runtime settings from `runtimecfg`. Only the base topic and page layout are currently writable at runtime.
- `config/panel_config.yaml` is intentionally ignored and is absent from CI checkouts. Public CI images therefore contain the example placeholders and cannot be advertised as ready-to-use until provisioning exists.
- The existing signed OTA trust model remains appropriate. Provisioning does not replace image signature verification and must never receive or store the OTA private key.

## Recommended architecture

Use Espressif's `espressif/network_provisioning` component with the SoftAP transport as the protocol and lifecycle foundation. ESP-IDF 6 removed the old in-tree `wifi_provisioning` component; the replacement is a separate component-manager dependency. Prefer protocomm Security 2 (SRP6a plus AES-GCM) and a per-device proof-of-possession value. A browser-facing captive portal can wrap the same provisioning state machine, but it must not fall back to unauthenticated protocomm Security 0.

The project already has an HTTP server for screenshots, but that server is intentionally disabled without a token and is not a provisioning surface. Keep provisioning HTTP routes separate from the screenshot server and stop the provisioning server when setup succeeds.

### Persistent configuration

Add a writable `provisioning` namespace (or a dedicated data partition if the final size requires it) for credentials. Keep the generated build defaults as fallback values for development builds, but apply this precedence:

1. Valid provisioned values in NVS.
2. Build-time defaults from `config/panel_config.yaml`.
3. Safe empty/default values that force setup mode.

Write a complete candidate configuration transactionally. Test Wi-Fi association and an MQTT connection before promoting it to active configuration. A failed test must leave the last known-good configuration intact. Do not log passwords, full broker URIs containing credentials, CA contents, or the provisioning proof value. Bound every stored string and reject control characters.

### Access point and portal

- Derive the SSID from the device identity so nearby panels are distinguishable.
- Generate a unique random WPA2 passphrase per device or provisioning session; show it on the panel and serial console. Never use an open AP or a project-wide default password.
- Keep the AP local-only. Do not bridge it to the user's LAN or provide routing to the internet.
- Run a DNS catch-all and redirect common connectivity checks to the portal. Bind the portal only to the SoftAP interface.
- Expire setup mode after a reasonable idle period and rate-limit login/submission attempts.
- Shut down the AP after successful provisioning. Require a deliberate physical gesture, such as a long touch on the panel, to start reprovisioning later. Add an MQTT reprovision command only if its authentication and retained-command policy are specified first.
- Make the form usable on a phone and expose validation errors without echoing secrets. The portal should report the new panel IP and whether MQTT connected before closing.

### MQTT and Home Assistant

Do not make MQTT a prerequisite for initial provisioning. The panel must be able to enter setup mode when it cannot associate with Wi-Fi or when the broker settings are invalid. Once MQTT is connected, retain the existing discovery and synchronization contract. No MQTT contract bump is needed if provisioning is entirely local; adding a network reprovision command or new MQTT configuration topics requires a new contract version and matching blueprint/integration documentation.

## Implementation order

1. Create a feature branch from the merged current release and record its one selected firmware/blueprint version. The onboarding branch uses firmware/blueprint `1.1.0` and MQTT contract `10`; retain these values for every commit in the branch.
2. Add and pin `espressif/network_provisioning` in `src/idf_component.yml`; enable only the required protocomm security scheme(s), with Security 2 preferred for production.
3. Extend `app_config` with bounded provisioning records, migration/default handling, transactional writes, and a clear/reset operation. Add native tests for reboot, failed commit, invalid input, and precedence.
4. Refactor `wifi_manager` startup into explicit states: provisioned station, provisioning AP, connection test, and rollback. Ensure MQTT and OTA tasks cannot start with incomplete credentials.
5. Add the SoftAP portal, DNS handling, per-device setup secret, timeout, rate limiting, and safe shutdown. Add a visible setup status to the LVGL screen and redacted diagnostics.
6. Add optional first-boot page/display fields to the portal only after the credential path is stable. Keep Home Assistant YAML and UI configuration as the authoritative post-setup path.
7. Update the factory packaging and README so the generic signed archive is advertised as ready-to-use only after a clean-device provisioning test passes. Keep the application-only `.bin` OTA flow unchanged.

## Security acceptance criteria

- No open or fixed-password AP.
- Provisioning messages are authenticated and encrypted, or the browser portal is protected by a unique setup secret and is reachable only through the isolated AP.
- Secrets are never printed, placed in URLs, published over MQTT, or included in release artifacts.
- Invalid submissions cannot overwrite a known-good configuration.
- Provisioning endpoints reject oversized input, malformed certificates, control characters, and excessive request rates.
- The AP and portal are unavailable after successful setup and can only be re-enabled intentionally.
- OTA still requires HTTPS, the manifest hash/size check, and the trusted signed application key.
- Physical flash extraction remains outside this feature's threat model unless encrypted NVS/flash encryption is explicitly added and documented.

## Validation and handoff checklist

- ESP-IDF clean build with a generic configuration and with signed OTA enabled.
- Native tests for NVS persistence, migration, validation, rollback, and state transitions.
- HTTP/DNS tests for captive portal routes, authentication, bounds, timeout, and redaction.
- Physical ESP32-S3 test: blank flash → AP → phone browser → Wi-Fi → MQTTS → Discovery → integration setup.
- Negative physical tests: wrong password, unreachable broker, invalid CA, power loss during save, repeated failed submissions, and forced reprovisioning.
- OTA test from the provisioned generic image through Home Assistant Repairs, including rollback after a failed boot.
- Verify both MQTT Sync and `walldisplay_sync` remain available and no panel runs both paths for the same topic.
- Update `README.md`, `config/panel_config.example.yaml`, relevant examples, release metadata, and the deployment copy. Run the full native/Python/YAML checks and commit each verified implementation step.

## Resolved decisions for the feature branch

- Use a browser-only captive portal protected by the unique WPA2 AP password.
- Wi-Fi is required; MQTT settings are optional and can be added later.
- The portal accepts a bounded PEM CA certificate and stores it with the runtime network configuration. A blank field removes the runtime override and restores the built-in CA bundle. The Home Assistant MQTT Sync and `walldisplay_sync` paths continue to manage panel data; certificate provisioning remains a local portal operation so broker trust material is never sent through MQTT.
- Re-enable the AP intentionally through the authenticated MQTT command; manually enabled AP sessions expire after 15 minutes of inactivity.
- NVS encryption is out of scope. The current project deliberately does not enable flash encryption or Secure Boot eFuses.

The physical acceptance and production sign-off procedure is maintained in
[docs/acceptance/softap-provisioning.md](../acceptance/softap-provisioning.md).
It covers clean-device provisioning, negative/recovery cases, power-loss
behavior, and signed OTA rollback; passing that procedure is required before a
generic factory image is called production-ready.
