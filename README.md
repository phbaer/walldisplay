# Guition Wall Panel Firmware

Firmware for the ESP32-4848S040 wall panel, built with ESP-IDF, LVGL, MQTT, and Home Assistant MQTT Discovery. It shows time, weather, media, and an About page with live device and network diagnostics, optional measurement chips, a configurable sequence of weather, media, buttons, and About pages, and up to five footer buttons; supports display dimming, HTTPS OTA with rollback, diagnostics, and screenshots.

## Install a precompiled release (normal users)

Signed firmware releases are published on the [Forgejo releases page](https://git.baer.one/phbaer/walldisplay/releases). A normal user does not need ESP-IDF or this source tree to flash a release:

1. Confirm that the panel is an ESP32-S3 Guition ESP32-4848S040 with 16 MB flash, and connect its USB port. Use a data-capable USB cable.
2. Open the release you want and download its `walldisplay-<version>-factory.tar.gz` asset. The factory archive contains the bootloader, partition table, initial OTA data, and application. The `.bin` asset is the application-only OTA image; do not use it for a blank panel.
3. Install Espressif's `esptool` in a temporary Python environment and unpack the archive:

   ```sh
   python -m venv /tmp/walldisplay-esptool
   /tmp/walldisplay-esptool/bin/pip install esptool
   mkdir walldisplay-factory && tar -xzf walldisplay-<version>-factory.tar.gz -C walldisplay-factory
   ```

4. Replace `<PORT>` with the panel's serial device (`/dev/ttyUSB0`, `/dev/ttyACM0`, or the corresponding Windows COM port), then flash all factory files at these offsets:

   ```sh
   /tmp/walldisplay-esptool/bin/esptool \
     --chip esp32s3 --port <PORT> write-flash \
     --flash-mode dio --flash-size keep --flash-freq 80m \
     0x0000 walldisplay-factory/bootloader.bin \
     0x8000 walldisplay-factory/partition-table.bin \
     0xf000 walldisplay-factory/ota_data_initial.bin \
     0x20000 walldisplay-factory/walldisplay.bin
   ```

   A factory flash replaces the firmware partitions but does not create Wi-Fi or MQTT credentials. After reboot, continue with [Initial setup](#initial-setup) and [Home Assistant](#home-assistant).

The public CI artifacts are signed and can be configured on first boot through the local setup AP. Existing panels that already have a trusted signed baseline can use the application-only `.bin` through the integration's Repairs update flow. Before advertising a generic factory archive as production-ready, run the [SoftAP provisioning acceptance procedure](docs/acceptance/softap-provisioning.md); the provisioning design is captured in the [SoftAP provisioning handover](docs/plans/softap-provisioning-handover.md).

## Build and configuration

Install ESP-IDF 6.0.2, then configure and flash:

```sh
source "$HOME/.espressif/v6.0.2/esp-idf/export.sh"
cp config/panel_config.example.yaml config/panel_config.yaml
# Edit Wi-Fi, MQTT, discovery, and the base topic.
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

There are two supported deployment modes:

* **Preconfigured deployment:** put the target Wi-Fi and MQTT values in
  `config/panel_config.yaml` before building. These values are compiled into
  the firmware as defaults, so a new device can connect immediately even when
  its `appcfg` and runtime `nvs` partitions are blank. Provisioned values saved
  later take precedence over those defaults.
* **Generic factory/provisioning deployment:** build with the example or an
  otherwise non-credentialed `panel_config.yaml`, and ship blank `appcfg` and
  runtime `nvs` partitions. With no valid stored credentials the device starts
  its setup access point and the user completes configuration through the
  portal. Do not include a customer Wi-Fi password, MQTT secret, or screenshot
  token in this image.

The partitions are deliberately separate: `appcfg` is the factory/provisioning
NVS area and writable runtime `nvs` stores user overrides. Erasing only one can
leave the other source of settings intact. Give each panel a distinct base
topic, such as `panel/guition-4848s040-kitchen`. Changing the discovered
**Panel MQTT Topic** entity persists the new topic and restarts the panel;
update the matching Home Assistant configuration to publish to the new topic.

## Initial setup

The setup portal tests the submitted Wi-Fi credentials before writing them to
NVS. A failed association returns an error and restores the previously active
station configuration, so a typo cannot replace a working network. MQTT
settings remain optional during this step; MQTT connectivity is checked after
the panel reconnects and can be configured later from the portal.
Open Wi-Fi networks are supported; the saved station authentication threshold
matches whether a password was provided.

1. **Prepare the panel and network.** Connect the panel's USB port, identify its serial port, and make sure it can reach the MQTT broker over the configured network. The target is the ESP32-S3 480×480 panel with 16 MB flash and octal PSRAM.
2. **Create the build configuration.** Copy `config/panel_config.example.yaml` to `config/panel_config.yaml` and set the Wi-Fi SSID/password, MQTTS broker URI, broker credentials, Home Assistant discovery prefix, and a unique `homeassistant.base_topic`. Keep `mqtt.require_tls: true` unless the broker is on an isolated trusted LAN; use a CA PEM in `config/` when the broker certificate is not in the ESP-IDF bundle. Set the initial page layout under `display`. Keep local configuration files out of version control.
3. **Flash a baseline.** With ESP-IDF 6.0.2 activated, run:

   ```sh
   idf.py set-target esp32s3
   idf.py build
   idf.py -p <PORT> flash monitor
   ```

   This produces an unsigned development image. It is suitable for first bring-up and local testing, but it rejects remote OTA requests. For OTA, flash a signed factory archive from a Forgejo release, or build and sign one locally with the RSA-3072 key described in [Security and upgrading to 1.0.0](#security-and-upgrading-to-100). A signed baseline must be installed before later signed OTA updates can be accepted.
4. **Finish Home Assistant setup.** Confirm the panel publishes `online` and appears through MQTT Discovery. Choose exactly one of the [MQTT Sync blueprint](config/blueprints/automation/walldisplay/mqtt_sync.yaml) or the [`walldisplay_sync` custom integration](custom_components/walldisplay_sync). Set the same panel topic in that path, then configure the page sequence, entities, buttons, display timing, and (optionally) screenshots. The complete copy/paste documents are [panel_blueprint.yaml](config/examples/panel_blueprint.yaml) and [panel_integration.yaml](config/examples/panel_integration.yaml).
5. **Check updates.** The custom integration listens for the panel's retained firmware version and checks the configured HTTPS Forgejo releases API every six hours (the default follows this project). When a newer signed manifest is available, Home Assistant creates a fixable **Repairs** issue for that panel. The issue title and confirmation page show the current and available versions; confirming it publishes the manifest URL as a non-retained OTA command and closes the dialog immediately. A per-panel lock rejects duplicate requests while the panel reports `queued`, `checking`, or `installing`; the repair remains until the panel reports the new version or an error. The panel verifies HTTPS, the image hash and size, and its configured signing key before rebooting. Set `update_api_url` in the integration YAML or its **Firmware updates** UI step to follow a fork or another anonymously readable compatible release feed; leave it empty to disable checks. The blueprint remains a synchronization-only path and does not perform automatic update checks.

## Home Assistant

Choose exactly one synchronization path per panel topic:

- Import [MQTT Sync](config/blueprints/automation/walldisplay/mqtt_sync.yaml) and set `panel_topic` and the desired panel settings. Weather and media entities are optional, allowing a buttons-only panel. It also owns configured footer-action sequences and optional wake-up triggers.
- Or install [`walldisplay_sync`](custom_components/walldisplay_sync) as a custom integration (the equivalent deployable copy is under `config/custom_components/`). Its native Setup and Configure flows manage panel settings and reload the integration after saving; the **Firmware updates** step accepts an HTTPS release API URL (or an empty value to disable checks) and validates it before saving. The package includes `translations/en.json`, which Home Assistant uses to render config-flow and repair text. After replacing an installed copy, restart Home Assistant and hard-refresh the browser so its integration and translation caches are rebuilt. It provides standard entities and events, so create normal automations for action-only **Footer Button N** and **Buttons Page N** event entities and for external events that press **Wake Panel**.

Both paths publish time, weather, media, favourites, chips, and footer state; map media commands to the selected media player. Do not enable both for one panel, or they will duplicate state and commands. The integration is HACS-packaged from the repository-root custom-component directory.

## Configurable pages: YAML and Home Assistant UI

Choose an ordered sequence of up to five page slots with `page1` through `page5`. Each slot accepts `weather`, `media`, `buttons`, `about`, or `none`; skip `none` slots during navigation. Each built-in page can appear once, so the current set provides four distinct page types while the layout and navigation capacity is five. `default_page` must name an enabled page. Defaults remain weather → media, starting on weather. One-page layouts hide navigation; multi-page layouts show a dedicated navigation box to the right of the main content box, with one symbol button per enabled page in the configured order. The symbols are weather, media, buttons, and settings for the corresponding page types. Applying a changed layout opens its startup page; repeated identical synchronization messages preserve the currently viewed page.

Set `weather_title`, `media_title`, `buttons_title`, and `about_title` to customize titles (48 UTF-8 bytes each). `panel_name` is a human-readable display label: spaces, capitalization, and other printable UTF-8 characters are preserved. The firmware and `walldisplay_sync` derive a separate lowercase hyphenated `panel_hostname` (up to 32 characters), publish it as `state/hostname`, and use it for Wi-Fi hostname resolution. The About page displays the firmware version, MQTT contract, hardware model, generated hostname, IPv4 address, global and link-local IPv6 addresses when available, Wi-Fi MAC, connected SSID/RSSI/channel, Wi-Fi and MQTT configuration status, uptime, and reset reason. Network values refresh every five seconds and show `unavailable` until the interface has connected; passwords are never shown. The About page is always appended as the final navigation page, and becomes active when setup AP mode is enabled so its credentials are visible. The header displays the panel label and current page title. The clock, indicators, chips, and five footer buttons remain shared. This release provides one instance of each built-in page; it does not yet support multiple media players or multiple button pages.

Both configuration paths support complete YAML and visual editing:

- **MQTT Sync blueprint:** use the automation's visual selectors or edit its `use_blueprint.input` mapping in YAML. [Complete blueprint example](config/examples/panel_blueprint.yaml) includes every input, including action sequences and wake-up triggers. Paste it into a new automation's YAML editor; when maintaining `automations.yaml` directly, add it as a list item. Use Home Assistant's automation editor for subsequent UI changes.
- **Custom integration:** start with **Configure using forms** or **Paste YAML configuration**. Configuration schema version 2 is shared by YAML, forms, and runtime. Existing version-1 entries migrate automatically without losing settings; future schema versions are rejected. Setup and Configure both offer **Pages and navigation**, **Buttons page**, and **Copy / paste YAML configuration**. The YAML editor exposes the entire settings mapping, including settings unrelated to pages. Copy it to a file, paste a replacement, then continue editing the same values through forms. Choose **Finish setup** to save and reload. [Complete integration example](config/examples/panel_integration.yaml) includes every setting. This YAML is pasted into the integration editor; it is not a `walldisplay_sync:` block in `configuration.yaml`, and changes to an exported file are not automatically loaded. Omitted keys in a pasted replacement reset to defaults. An existing entry must retain its panel topic; create a new entry to configure a different panel.

Page and grid keys are identical in these two documents. Use `page1` through `page5` for the shared page slots. Existing footer/favourite keys retain their historical naming (`buttonN` / `media_favoriteN` in the blueprint, `footerN` / `favoriteN` in the integration). The integration deliberately exposes standard entities and events: manual automations are required for action-only buttons and wake-up triggers, and those automations can also be authored entirely in YAML or with Home Assistant's automation UI. Do not enable both synchronization paths for the same panel topic.

A minimal integration-editor configuration for a buttons-only panel is:

```yaml
panel_topic: panel/living-room
panel_name: walldisplay-living-room
page1: buttons
page2: none
page3: none
page4: none
page5: none
default_page: buttons
buttons_title: Room controls
grid1_label: Lights
grid1_state_entity: light.living_room
grid2_label: Movie night
```

The buttons page has six independent controls in two columns. `gridN_label` names a control; an empty label hides it. Labels allow 96 UTF-8 bytes and are visually ellipsized when needed. An optional `gridN_state_entity` can be a light, switch, input boolean, or fan: pressing toggles it, `on` highlights it, and `unknown` / `unavailable` disables it. Without a state entity, the blueprint runs `gridN_action`, while the integration emits a `pressed` event through **Buttons Page N**. For the Movie night example, attach a manual automation to **Buttons Page 2** that activates your scene. These controls are independent of the existing five footer buttons.

Initial firmware page settings can also be specified in the `display` section of [panel_config.example.yaml](config/panel_config.example.yaml). The precedence is build-time defaults, then a valid saved runtime layout in NVS, then the selected Home Assistant synchronization path's retained layout. Saving an identical layout does not write NVS again. Page selection and titles survive restarts; button labels and state are restored through retained MQTT messages. Invalid layout input preserves the last working layout and reports an error at `state/config/error`.

## MQTT contract

The firmware, blueprint, custom integration, and Python project release is `1.0.0`; MQTT contract `9`. Both synchronization paths publish retained compatibility metadata to `<base>/set/blueprint_info`:

```json
{"version":"1.0.0","contract":"9"}
```

| Purpose | Topic | Notes |
| --- | --- | --- |
| Availability | `<base>/status` | Retained `online` / `offline`. |
| Display updates | `<base>/set/...` | Matching retained `state/...` inputs also work. |
| Commands | `<base>/cmd/...` | Non-retained buttons, media, sync, configuration, OTA, screenshots, and factory reset. |
| Panel state | `<base>/state/...` | Retained canonical values and diagnostics. |

The discovered **Default Panel Widget** select offers the enabled pages in navigation order. Its value is saved in the panel and applied immediately; it is controlled through `<base>/cmd/config/default_page` and reported at `<base>/state/config/default_page`. Home Assistant's next configuration publish restores its configured startup page, so edit the synchronization configuration for a lasting change. The discovered MQTT device name includes the configured panel name in parentheses, making similarly named panels easy to identify in Home Assistant. Discovery configuration messages use retained QoS 0 to avoid filling the panel's TLS publish queue during reconnect; diagnostic device information is published after connection by the periodic device-info timer.

Contract 6 adds retained `<base>/set/pages` configuration and `<base>/state/config/pages` canonical reporting. Contract 8 adds the `about` page. Contract 9 expands the ordered page layout to five slots (`page1` through `page5`):

```json
{"pages":["weather","media","about"],"default_page":"weather","titles":{"weather":"Weather","media":"Media","buttons":"Buttons","about":"About"}}
```

The payload must be shorter than 1024 bytes. Each `<base>/set/grid/N` (N = 1–6) carries retained JSON such as `{"label":"Lights","state":"on"}`; use `stateless` for action-only controls. Presses publish non-retained `press` to `<base>/cmd/gridN`. Synchronization paths ignore presses for hidden controls or a disabled buttons page. Upgrade firmware and the selected synchronization path together to release 1.0.0 / contract 9; older weather/media topics remain supported, but older peers cannot use the five-slot layout and report a compatibility mismatch.

Display data uses `set/name`, `set/weather`, `set/media`, `set/clock`, `set/date`, `set/chipN`, `set/chipN/color`, `set/buttonN/label`, and `set/buttonN/state`. `set/name` carries the human display label; the firmware derives and applies the Wi-Fi hostname and publishes both `state/name` and `state/hostname`. The label is shown smaller and muted below the clock. The header shows large, unified clock and date labels at the upper left and upper right, respectively, both with 14 px top and outer-side padding. Select 24-hour (default) or 12-hour time in either synchronization path. Its Wi-Fi, MQTT, and Home Assistant indicators form one three-section rounded status control; only its outer corners are rounded. The Home Assistant indicator turns green on the global `homeassistant/status=online` birth message and on the retained clock synchronization published every minute by either synchronization path. This panel-local heartbeat also fixes the amber state when the panel connects after Home Assistant has already started. Discovery publication is best effort: a transient MQTT disconnect during reconnect no longer aborts the firmware, and discovery is retried on the next connection. `set/media` accepts plain text or JSON, for example:

```json
{"state":"playing","title":"Track","artist":"Artist","artwork_url":"https://example.invalid/cover.jpg"}
```

Media commands are `cmd/media/previous`, `play_pause`, `next`, `power_off`, `volume_down`, `volume_up`, `volume`, and `favoriteN`. The media page places the power button at the top-right; configure an optional **Media power switch** to turn off a dedicated `switch` entity, or leave it empty to turn off the selected media player. It places a horizontal blue volume rocker beside the transport controls: its generously sized transparent `−` and `+` touch ends have explicit matching outer insets and support a tap for a small adjustment or press-and-hold repeat, while its central slider publishes `cmd/media/volume` with an integer percentage from 0 to 100 and follows the `volume_level` media-state field. The favourites row uses the full width beneath the controls. The media page uses a 136 px cover-art area and a separate metadata column showing title, artist, album, source, and playback state; favourites publish retained labels and icons, and their play-media JSON targets the selected player. `walldisplay_sync` fetches the selected player's artwork with a 1 MiB download limit and a 16-megapixel decode limit, converts it to a bounded JPEG, and serves it from a random-token panel URL with a content revision; configure an internal or external Home Assistant URL reachable from the panel. Firmware TLS uses dynamic record buffers so artwork HTTPS fetches can coexist with the persistent MQTT session. Failed artwork downloads remain retryable. Newer requests and clear operations supersede old downloads; successful sources are refreshed after a 60-second cache interval on the next media update. Configuration, clock, controls, weather, and media publish independently, so slow or failed artwork cannot block control configuration. MQTT Sync remains a no-custom-code alternative and forwards its media URL directly, so its source must already be a reachable baseline JPEG.

Weather accepts text or JSON with `condition`, `temperature`, `humidity`, `pressure`, `wind_speed`, `rainfall`, `irradiance`, a `trend` of up to 25 historic temperature values, and up to three forecast entries. The current temperature uses a larger bold value; available humidity, pressure, wind, rain, and solar-irradiation readings form a vertically aligned icon-led column on the right. Humidity uses a droplet, rain uses a downpour marker, and wind uses wave marks. The `wind_unit`, `rainfall_unit`, and `irradiance_unit` fields preserve sensor units. The panel renders the last 24 hours of temperature history as a rounded background curve beneath current conditions; it never uses forecast values. Both sync paths request 24 hourly Recorder means for the optional temperature sensor (or the weather entity when no sensor is configured), then append the current value. The trend requires Recorder data and a source entity that provides long-term statistics, normally a temperature sensor with `state_class: measurement`; otherwise the curve remains hidden. Both sync paths use tomorrow through day +3 only for the forecast cards; optional temperature, humidity, pressure, wind-speed, rainfall, and irradiance sensors override their matching weather-entity attributes. Wind falls back to the standard weather `wind_speed`/`wind_speed_unit` attributes; rainfall and irradiance use matching attributes when a provider exposes them. A configured footer state entity is toggled on press; otherwise the integration emits an event and MQTT Sync runs its configured action. `cmd/wake` and any touch restore brightness, while `cmd/sync` requests a complete refresh.

**Capture Screenshot** (or `cmd/screenshot`) stores a 480×480 BMP in SPIFFS and publishes progress at `state/screenshot`, including a LAN URL when complete. Publish an optional alphanumeric, `_`, or `-` name as the command payload to retain a named capture; retrieve it from `http://<panel>:8080/screenshot.bmp?name=<name>` with `Authorization: Bearer <token>`. Provision `security.screenshot_token` in the firmware YAML, or set a 32–64 character alphanumeric token in the local setup portal, and enable **Enable authenticated screenshots** in either Home Assistant synchronization path first. `cmd/page` accepts the ID of an enabled page (`weather`, `media`, `buttons`, or `about`), enabling non-touch visual checks. Requests for disabled pages are rejected. Run `SCREENSHOT_TOKEN=<token> PANEL_TOPIC=<base-topic> PANEL_HOST=<panel-ip> bash tools/capture_screenshots.sh` to publish fixed full/minimal-weather and idle/playing-media fixtures, capture each page, and download BMPs. [`tools/capture_screenshots.sh`](tools/capture_screenshots.sh) also accepts optional `MQTT_HOST`, `MQTT_PORT`, `MQTT_USERNAME`, `MQTT_PASSWORD`, `MQTT_TLS`, `MQTT_CAFILE`, `MQTT_CAPATH`, `OUTPUT_DIR`, `SETTLE_SECONDS`, `CAPTURE_TIMEOUT_SECONDS`, and `RUN_ID` values. `MQTT_TLS=1` uses the system CA directory unless `MQTT_CAFILE` is supplied. Captures can take several seconds while the panel renders; the default per-capture timeout is 45 seconds. It requires `mosquitto_pub`, `mosquitto_sub`, `curl`, and `timeout`; failures print the last panel status messages or explain when no status was received. The HTTP endpoint requires a bearer token and returns `Cache-Control: no-store`. It retains at most four named captures and removes captures on reboot. Captures are disabled by default; without a provisioned token the HTTP server does not start. HTTP is not encrypted: keep this endpoint on an isolated trusted LAN, or use a TLS reverse proxy. The token is never included in MQTT status URLs.

## Architecture

Platform integration—ESP-IDF, FreeRTOS, MQTT, and LVGL entrypoints—remains C. Self-contained components use C++ behind C-compatible headers: `PanelComponent` defines the widget boundary, `MediaWidget` owns media rendering, Unicode-safe media text, and `ArtworkService` owns artwork queuing, HTTP, gap-free JPEG scaling, double-buffered PSRAM artwork, and atomic display swaps. The bundled Noto Sans UI font includes Latin, Greek, Cyrillic, and common punctuation used in media metadata; unsupported scripts safely fall back to `?`. `page_manager.c` owns layout parsing, validation, serialization, and navigation policy independently of LVGL. The UI binds page IDs to LVGL roots, resizes them together, and applies visibility consistently. Weather and the six-button grid now own their rendering state in `WeatherWidget` / `ButtonsWidget` components behind C APIs. Common visual primitives live in `ui_theme.h`. UI callbacks emit typed `ui_action_t` intents; `panel_commands.c` maps them to MQTT independently of rendering. Media controls and the shared header/footer remain in `ui.c`. Home Assistant field definitions, validation, and explicit migrations live in `configuration.py`, shared by forms, YAML, and runtime.

## Hardware and release notes

LVGL uses byte-swapped RGB565 in direct mode with two complete PSRAM frame buffers. UI cards use opaque fills and avoid clipped-corner layers so the software renderer does not allocate large intermediate surfaces on every refresh. The task watchdog timeout is 30 seconds to allow the initial full-screen composition to complete while retaining watchdog protection.

Target hardware: ESP32-S3, 16 MB flash, octal PSRAM, 480×480 ST7701 RGB display, and GT911 touch. LVGL uses byte-swapped RGB565 in direct mode with two complete PSRAM frame buffers. The RGB panel transfers through 40-line internal bounce buffers and releases a frame only after its full transfer completes, so page changes never modify the frame currently being scanned. The normal pixel clock is 10 MHz to leave bandwidth for Wi-Fi, MQTT, and LVGL. The firmware moves executable and read-only data to PSRAM and uses the ESP32-S3 64-byte data-cache line to reduce bounce-buffer refill stalls; recurring RGB DMA restarts remain disabled because they produce a visible flash. Album artwork remains in PSRAM so the internal DMA-capable memory stays available for the RGB bounce buffers. The LVGL task and JPEG artwork decoder use 16 KiB and 12 KiB stacks in PSRAM respectively; this covers the deeper media-page render path without taking memory from the display DMA path. OTA lowers the pixel clock and protects the backlight while flash access competes with display transfers. The partition table provides writable `appcfg`, NVS, factory and OTA slots, and SPIFFS.

The firmware requests the ESP32-S3 driver maximum Wi-Fi transmit power (19.5 dBm in the driver's quarter-dBm units). The access point's regulatory domain can reduce that value, and the actual antenna and local regulations always take precedence. There is no separate software control that increases receive sensitivity; improve reception with a closer access point, less crowded channel, better antenna placement, or a wired/mesh backhaul. A weak link can cause both MQTT commands and HTTPS OTA manifest downloads to time out. TLS uses dynamic record buffers with a 16 KiB inbound limit and a 2 KiB outbound limit. OTA pauses MQTT and artwork transfers while it obtains the manifest and image, leaving enough internal RAM for HTTPS. Firmware is downloaded in independently verified 32 KiB HTTP range requests, which also allows recovery from a truncated long-lived response.

Use the discovered **Panel Update Manifest URL** entity or publish a non-retained HTTPS manifest URL to `<base>/cmd/update`. OTA is available only in builds with ESP-IDF app-signature verification enabled. When an update is accepted, the panel immediately renders an opaque static **Firmware update / Please wait...** screen with a download progress bar into both RGB frame buffers; retained MQTT synchronization continues in the background without redrawing visible page content. During the firmware transfer the RGB pixel clock is reduced and the DMA timing is resynchronized to lower PSRAM bandwidth pressure while keeping the progress screen visible. If the panel driver cannot change its pixel clock, the backlight is disabled as a safe fallback and the transfer progress is available in the serial log. The previous brightness is restored if manifest validation or firmware download fails. Before a verified update reboots, the backlight is disabled and both RGB frame buffers are blanked so stale or partially rendered pixels are not visible during reset. The panel checks the HTTPS manifest SHA-256 and size and lets ESP-IDF verify the signed image before selecting its boot partition. A pending image has 120 seconds to connect to MQTT; otherwise it explicitly reboots for rollback. Large OTA buffers are worker-owned static storage instead of task-stack allocations; logs report stack low-water headroom after each download. OTA release requests use IPv4 while retaining HTTPS hostname verification, avoiding unusable link-local AAAA records sometimes returned by local DNS.

The integration accepts full Semantic Versioning release tags, including dotted prerelease identifiers such as `v1.0.0-rc.14.1`, and compares them correctly when deciding whether to create a Repairs update issue. The panel republishes its retained firmware version periodically; those heartbeats retry a check after startup or temporary API failures (with a five-minute debounce), while an existing issue is kept during a transient release-feed outage. If no issue appears, verify that the configured release API returns a signed `walldisplay-<version>.json` manifest asset and that the integration copy has been reloaded.

## Security and upgrading to 1.0.0

This release changes operational defaults. Upgrade the firmware and the chosen synchronization path together; do not run both paths for a panel.

- **MQTT:** the firmware requires `mqtts://` by default, using the public CA bundle or `mqtt.ca_certificate_file` (a PEM filename placed in `config/`). Configure the broker hostname to match its certificate. The setup portal also accepts an optional PEM CA certificate (up to 3072 bytes), stores it in runtime NVS, and uses it when testing and connecting to an MQTTS broker; clearing the field restores the built-in CA bundle. Protect flash with the device's deployment security configuration when storing secrets. Existing installations on an isolated LAN can explicitly set `mqtt.require_tls: false` to retain `mqtt://`; that mode sends credentials and messages without TLS. Build-time security policy still applies to an MQTT URI supplied through the provisioned NVS partition. A retained Last Will publishes `offline` after an unexpected disconnect; reconnection republishes `online` and canonical state. If portal validation fails, the serial log reports the received broker URI and whether the broker rejected the connection, TLS verification failed, the socket could not be opened, or the ten-second connection test timed out. The portal also rejects an `mqtt://` URI while TLS is selected and reports the URI/security mismatch in the firmware log.
- **Wi-Fi setup AP:** a blank or placeholder Wi-Fi configuration starts the local setup AP automatically. Publish `ON` or `OFF` to `<base>/cmd/config/ap` to enable or stop it on demand. Its SSID is `WallDisplay-<MAC suffix>` and its unique WPA2 password is printed on the serial console and shown on the display's About page while the AP is active; enabling the AP also switches to that page. A local DNS catch-all answers with `192.168.4.1`, so captive connectivity checks reach the portal. Browse to `http://192.168.4.1/` and submit Wi-Fi SSID/password, display name, MQTT URI, MQTT credentials, discovery prefix, base topic, and TLS preference; values are validated and committed atomically, preserving the last known-good configuration on failure. Wi-Fi disconnect logs include the driver reason code to distinguish authentication, security, and signal failures. The AP stops after 15 minutes of inactivity and after successful save/reboot. The retained state is `<base>/state/config/ap`. The AP is intended for local setup/testing.
- The discovered **Factory Reset and Reboot** button publishes a non-retained command that erases runtime and factory NVS configuration, then reboots into the setup AP. It removes Wi-Fi, MQTT, display, layout, and screenshot-token overrides; use it only when the panel should be provisioned again. **Reboot Panel** publishes `<base>/cmd/config/reboot` and restarts the firmware while preserving configuration; the legacy `<base>/cmd/reboot` topic is accepted too.
- The portal is a responsive, phone-friendly page divided into Wi-Fi, display, and optional MQTT sections. It redirects common captive-check paths (`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, and `/ncsi.txt`) to the setup page. `GET /api/status` returns the generated hostname and AP state with `Cache-Control: no-store`. Press **Refresh networks** to perform a manual scan; `GET /api/wifi/scan` returns nearby SSIDs, signal strength, and whether each network is open or secured. Scans are serialized and their result buffer is allocated off the HTTP task stack so repeated refreshes remain safe. The portal HTTP task has an 8 KiB stack to accommodate form parsing and optional PEM certificates. The MQTT topic is generated as `<topic prefix>/<hostname>` from the display name; the generated hostname and complete topic are shown live as the fields are edited. The topic prefix, broker URI, and username inputs disable browser capitalization and autocorrection. Selecting a result fills the SSID while leaving the password field unchanged. Passwords have local show/hide controls and are never returned by the status or scan APIs. The screenshot token field includes a browser-side random token generator. **Test connections** submits asynchronously and displays success or failure inline, preserving all entered values while the exact previous station/AP state is restored; **Save and reboot** validates them again before committing. Candidate Wi-Fi validation uses the same open-network or WPA2 threshold as normal startup.
- Portal saves require the standard `application/x-www-form-urlencoded` content type; percent encoded and `+` escaped field values are decoded before validation. Valid settings, including the optional screenshot token, are persisted with ESP-NVS-compatible keys. Requests are limited to five attempts per minute, and oversized requests are rejected before parsing.
- **Commands:** firmware rejects retained deliveries under `<base>/cmd/`, while retained state/configuration under `set/` remains supported. MQTT text messages are limited to 2047 bytes, must arrive as contiguous fragments, and reject excessive JSON nesting (over 16 levels); OTA manifests use the same nesting bound. The integration also rejects retained media, footer, and grid action deliveries. Home Assistant blueprint MQTT triggers do not expose the retain flag, so the blueprint cannot apply that action-delivery check; publish all action commands non-retained, clear old retained command messages, and restrict broker publishers. Stock topic ACLs do not distinguish the retain flag. This intentional difference does not affect the firmware's protection of update, screenshot, page, and configuration commands.
- **Screenshots:** provision a distinct 32–64-character ASCII alphanumeric `security.screenshot_token` per panel (for example, generate one locally with `python -c 'import secrets; print(secrets.token_hex(24))'`). Enable `screenshots_enabled` through the integration form/YAML or the equivalent blueprint input. Firmware consumes retained `set/screenshots_enabled` payloads `ON`/`OFF` and reports `state/screenshots_enabled`; enabling without a token fails closed. Disabling prevents new captures and downloads. Screenshots and tokens are not intended for exposure over untrusted HTTP networks.
- **OTA:** an ordinary unsigned build rejects update requests. To retain OTA, seed the panel with a signed build and use the same protected signing key for later releases. Migrating from earlier unsigned firmware requires installing that trusted baseline through your normal local flashing workflow. Signed-app verification here does not enable hardware Secure Boot or flash encryption, does not burn eFuses, and does not protect against physical flash modification or extraction. Images signed by the trusted key remain trusted, including older versions; hardware anti-rollback is not enabled.

Start from [the broker TLS example](config/mosquitto.conf.example) and [per-panel ACL example](config/mosquitto-acl.example). Use unique panel credentials and a separate trusted Home Assistant account. The panel account can publish its own actions/state and exact discovery IDs, but cannot publish firmware-update commands or affect another panel. Restrict the Home Assistant account and deployment credentials to the panels they administer. Keep private signing keys, Wi-Fi/MQTT credentials, screenshot tokens, local configuration, and generated firmware outside public artifacts; generated firmware includes provisioned build defaults.

Generate an unencrypted **RSA-3072** private PEM key once; EC keys generated with `openssl ecparam` are not supported by this signing configuration. After activating ESP-IDF, generate the key and build signed firmware:

```sh
# Create the production key once in protected storage outside this repository.
umask 077
python -m espsecure generate-signing-key --version 2 /secure/walldisplay-signing-key.pem
python tools/build_signed_firmware.py --key /secure/walldisplay-signing-key.pem
# Output: build-signed/walldisplay.bin (signed), and matching bootloader/partitions.
python tools/check_signed_firmware.py --image build-signed/walldisplay.bin --key /secure/walldisplay-signing-key.pem
```

Alternatively, generate the key with OpenSSL instead of `espsecure` (choose one method):

```sh
umask 077
openssl genrsa -out /secure/walldisplay-signing-key.pem 3072
```

Replace `/secure/` with an existing protected directory outside this repository. Preserve and securely back up the key, never commit it to Git, and reuse it for subsequent releases; replacing it is a separate trust migration.

For signed releases on Forgejo, create a repository **Actions secret** named `OTA_SIGNING_KEY_PEM`. Paste the entire private PEM file contents, including the `BEGIN`/`END` lines and actual newlines, as its value—not a file path or text containing literal `\n` escapes. Release jobs require a valid unencrypted RSA-3072 key and fail before compilation if it is missing or malformed. The temporary key is removed after building. Ordinary branch/PR jobs build unsigned firmware and do not receive this signing secret.

The signing helper explicitly targets ESP32-S3, creates a separate SDK configuration from committed defaults, and checks that RSA signing and OTA signature verification are enabled without hardware Secure Boot before building. This also works on a clean CI runner without a prior `idf.py set-target` step. If an existing build directory was configured for another chip, use a fresh build directory. It never copies the private key into source. Manifest hashes and sizes must describe the final signed binary, not `walldisplay-unsigned.bin`.

## Test builds and releases

The GitHub HACS workflows pin `hacs/action` to commit `d556e736723344f83838d08488c983a15381059a` (`22.5.0`) while its moving `main` action has a manifest-discovery regression that can report valid manifests as `None`. This repository is mirrored from Forgejo to GitHub. For every `v*` tag received by GitHub, GitHub publishes the Home Assistant integration ZIP: an exact `vMAJOR.MINOR.PATCH` tag creates a normal latest release, while a tag with a prerelease suffix such as `-beta.1` or `-rc.1` creates a prerelease. Non-default branch pushes have separate short-lived `v<project-version>b<id>` preview releases; the base version is read from `pyproject.toml`, so preview tags stay aligned with the canonical project version. Tags are explicitly excluded from that workflow, so an RC tag produces only its matching tagged release. These GitHub releases contain the integration only; Forgejo remains the source for signed firmware releases. The workflows use the GitHub CLI to create a release only when it is missing and upload the integration ZIP separately, so rerunning a tag never tries to rewrite an existing release's target commit. No release branches are required.

When repairing an existing tag, run the HACS workflow manually from a branch containing the current workflow and pass that branch/commit as `ref` together with the existing release tag as `tag`. A tag-triggered run uses the workflow file stored at that tag, so an older tag cannot automatically pick up later workflow fixes. The GitHub Actions workflow must have `contents: write`; if the repository or organization defaults to read-only tokens, enable read/write workflow permissions in GitHub Actions settings.

Before running a Forgejo firmware release, create a repository **Actions variable** named `PUBLIC_URL` containing `https://git.baer.one` (without a trailing `/`). Forgejo reserves names beginning with `FORGEJO_`, so use `PUBLIC_URL` exactly. The workflow uses this value because `forgejo.server_url` can resolve to an internal Docker hostname; it is used for release API access and embedded in OTA manifest download links.

Forgejo is the primary source for firmware builds, signed releases, and OTA assets. The [Forgejo firmware workflow](.forgejo/workflows/release-firmware.yaml) uses the shared build, validation, and packaging scripts. GitHub firmware building and publishing are disabled; do not configure `OTA_SIGNING_KEY_PEM` on GitHub. Its HACS integration releases and integration-only branch previews remain active. After checkout, each job adds its exact workspace path to Git’s `safe.directory` configuration inside the build container, allowing Git commands against the runner-owned checkout without trusting arbitrary directories.

Build behavior:

| Trigger | Result | Signing |
| --- | --- | --- |
| Branch push, PR, or manual run with empty `release_tag` | `dev-<checked-out SHA>` workflow artifacts, retained for 14 days | Unsigned; remote OTA disabled |
| Tag such as `v1.0.0-beta.1` or `v1.0.0-rc.1` | Prerelease with firmware, factory archive, changelog, and OTA manifest | Required |
| Tag such as `v1.0.0` | Stable release with the same assets | Required |

To publish a test build from the current commit:

```sh
git tag -a v1.0.0-rc.1 -m "First release candidate"
git push origin v1.0.0-rc.1
```

Push the tag to your Forgejo remote to publish firmware (`origin` in the example above). The Forgejo mirror must mirror `refs/tags/v*`; once GitHub receives a new tag, its integration workflow publishes the matching GitHub release automatically. Do not create or retain release branches. Normal non-default branches continue to publish short-lived HACS branch previews through the separate branch-preview workflow; those previews are removed when the branch is deleted.

If a tag was already mirrored before the workflow commit, no new GitHub push event occurs. A maintainer can manually run the HACS workflow with `ref` set to the tagged commit and `tag` set to the existing release tag; this is also the recovery path for an existing release. The firmware workflow verifies that its checked-out commit matches an exact tag. Tags use `vMAJOR.MINOR.PATCH` with an optional SemVer prerelease suffix and must fit the firmware's 32-character manifest version limit. The checked-in project and blueprint version remains the canonical `1.0.0`, while each tagged firmware build embeds its exact tag (for example `v1.0.0-rc.8`) in `APP_FW_VERSION`. Home Assistant can therefore order successive release candidates correctly; the stable `v1.0.0` image reports the stable version and is not offered an older RC.

Download ordinary test packages from the workflow run's artifacts. They contain a binary, factory archive (including initial OTA data), and build metadata; they deliberately have no OTA manifest pointing at a nonexistent release. Published manifests contain the checked-out commit SHA and the hash and size of the final signed binary. To test OTA, explicitly select the prerelease manifest URL and use the key already trusted by the panel. A prerelease label does not restrict firmware installation.

Publish a new `rc.2`, `rc.3`, etc. for each revision. Firmware publishing refuses existing firmware assets and never overwrites them; protect `v*` tags against unauthorized creation, movement, and deletion in repository settings. Restrict manual release runs and changes to release workflows to trusted maintainers. Use isolated runners for untrusted PR builds, especially on self-hosted Forgejo. Job conditions separate routine builds from signing but do not replace host-level access controls. Keep the firmware signing secret on Forgejo only.

Forgejo updates the wiki changelog only for stable firmware releases; each prerelease still includes its own changelog. The publisher reuses an existing empty draft release, which recovers a failure before the first asset upload. A published release or a release containing firmware assets is never deleted or overwritten; use a new tag for those cases. No automatic promotion or rolling test OTA channel is configured.

Forgejo uses its supported `upload-artifact@v3` action; see [Forgejo artifact compatibility](https://forgejo.org/docs/v15.0/user/actions/advanced-features/). Run `python -m unittest discover -s tests -p 'test_firmware_release.py'` to check tag validation, source identity, packaging, and overwrite protection locally.

## Development

The repository's Python tools and tests use the dependencies declared in `pyproject.toml`; use `uv sync` to create the reproducible test environment. `pyproject.toml` is the canonical project-version source. After changing its `MAJOR.MINOR.PATCH` value, run `python tools/sync_project_version.py` and commit the generated updates to the firmware header, both integration manifests and compatibility payloads, and the blueprint metadata. Forgejo and GitHub workflows verify these checked-in references before building or packaging and fail if they drift. Tagged Forgejo builds pass the release tag separately to the signed firmware build, so the runtime version can include an RC suffix without changing the checked-in `1.0.0` references. ESP-IDF and its Python environment remain separate prerequisites for firmware builds.

Run `sh tests/run_unit_tests.sh` before merging (power/layout, NVS recovery, MQTT framing, replay/authentication, and artwork-policy tests; requires the managed cJSON dependency downloaded by the firmware build). The Python checks exercise YAML migrations/round trips, Home Assistant forms, footer/grid command routing and replay rejection, matching blueprint/integration payloads, bounded artwork downloads and race handling, and independent synchronization jobs:

```sh
# With the project environment synchronized:
PYTHONPATH=. uv run pytest tests -q
PYTHONPATH=. uv run python tools/acceptance_check.py --help
python tools/sync_custom_component.py --check
# With ESP-IDF active, after building:
python tools/check_ota_stack.py
```

The OTA stack guard requires the firmware download routine to remain a distinct
non-inlined frame; this keeps the static budget check valid in optimized builds.

The release workflow builds firmware before running native tests so the managed cJSON source is available. The Python tests were validated with Home Assistant 2026.9.2. `custom_components/walldisplay_sync` is the canonical integration source; run `python tools/sync_custom_component.py` after changes to refresh `config/custom_components/walldisplay_sync`. See the [security/reliability implementation plan](docs/plans/security-reliability.md), [configurable-pages implementation plan](docs/plans/configurable-pages.md), and [About page implementation plan](docs/plans/about-page.md) for scope and fixed release selections.

Follow [AGENTS.md](AGENTS.md) and [the maintenance workflow](skills/project-maintenance/SKILL.md): keep this README current, select one aligned firmware/blueprint release (and MQTT contract, when needed) from the feature base and retain it through all follow-up commits, validate affected artifacts, and commit verified closed implementation steps with short descriptive messages.

Licensed under [MIT](LICENSE). Weather condition icons derive from [NSPanel-Easy](https://github.com/edwardtfn/NSPanel-Easy/tree/main/hmi/dev/pics/weather/dark) (MIT). Weather measurements use the standard Font Awesome Free droplet, gauge, wind, rain-cloud, and sun symbols (CC BY 4.0); the bundled Noto Sans font uses the SIL Open Font License 1.1.
