# About page

Feature base: `63d3384` on `feat/security-reliability`.
Selected firmware, blueprint, and integration release: **1.0.0**.
Selected MQTT contract: **9**, because the retained page-layout payload now accepts five ordered slots.

The About page is a built-in, selectable page. It renders the firmware version from `APP_FW_VERSION`, the MQTT contract from `APP_CONTRACT_VERSION`, the hardware model, hostname, IPv4 address, global and link-local IPv6 addresses when available, Wi-Fi MAC, connected SSID/RSSI/channel, uptime, and reset reason. Runtime network values refresh every five seconds and never include credentials. Tagged release images embed their exact tag in `APP_FW_VERSION` (including `-rc.N`) so Home Assistant can detect newer release candidates; the checked-in project version remains `1.0.0`. Its title is configurable through the firmware defaults, the MQTT Sync blueprint, or the `walldisplay_sync` YAML/forms editor. Existing layouts continue to default to weather → media and remain valid. Multi-page layouts use direct symbol buttons in a vertical strip on the right side of the display.

Validation covers the four-page native layout parser, generated build defaults, Home Assistant YAML/forms and blueprint parity, translation/deployment-copy synchronization, and the host test suites. An ESP-IDF build and on-panel rendering still require the configured ESP-IDF environment and hardware.
