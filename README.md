# Guition Wall Panel Firmware

Firmware for the Guition ESP32-4848S040 wall panel. It uses ESP-IDF, LVGL, MQTT, and Home Assistant MQTT Discovery.

## What it does

- Shows time, date, weather, media controls and favourites, optional measurement chips, and up to five footer buttons.
- Dims its PWM-controlled backlight after inactivity and can switch it off completely; a touch or wake command restores full brightness.
- Receives display state over MQTT and publishes button commands, availability, diagnostics, retained state, and on-demand screenshots.
- Registers its Home Assistant entities through MQTT Discovery.
- Supports HTTPS OTA updates and rollback after a failed boot.

The source lives in `src/`; public headers are in `include/walldisplay/`. `config/panel_config.example.yaml` contains build-time defaults, and `appcfg_nvs.csv` is the provisioning template.

## Build

Install ESP-IDF 6.0.2, then source its environment and build for the ESP32-S3:

```sh
source "$HOME/.espressif/v6.0.2/esp-idf/export.sh"
cp config/panel_config.example.yaml config/panel_config.yaml
# Edit config/panel_config.yaml before building.
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

`dependencies.lock` pins managed components. ESP-IDF downloads them into the ignored `managed_components/` directory on the first online build.

## Configuration

Set Wi-Fi, MQTT, discovery, and base-topic defaults in the ignored `config/panel_config.yaml`. At build time they become fallback values. Provisioned values in the read-only `appcfg` NVS partition take precedence.

Give every panel its own MQTT base topic, for example `panel/guition-4848s040-kitchen`. The default is `panel/guition-4848s040`. The **Panel MQTT Topic** entity can change the topic at runtime; the panel saves the override in normal NVS and restarts. Update the matching Home Assistant automation after a move.

## Home Assistant

Import [the MQTT Sync blueprint](config/blueprints/automation/walldisplay/mqtt_sync.yaml), create an automation from it, and set at least `panel_topic`, `panel_name`, `weather_entity`, and `media_entity`. Chip sensors and footer-button state entities are optional.

An optional custom integration is also included at [`config/custom_components/walldisplay_sync`](config/custom_components/walldisplay_sync). Copy that directory to Home Assistant's `config/custom_components/`, restart Home Assistant, then add **WallDisplay Sync** from the Integrations page. Setup starts with the panel topic, name, and media player, then presents clearly labelled pages for display/weather, media favourites, measurement chips, and footer buttons. Select the number of chips or footer buttons; only the requested fields are shown. Weather, chip sensors, and footer state entities use compatible entity dropdowns. The integration publishes local clock/date updates once per minute and publishes structured current-media state; it maps transport and volume commands to standard `media_player` actions, and invokes `play_media` on its selected player when a favourite is pressed. A configured footer state entity is displayed and toggled directly; an unbound footer button instead produces a WallDisplay footer-button event entity for a user automation. The integration also creates **Wake Panel** and **Sync Panel** button entities. Use those normal entities in automations; this avoids device-automation APIs and keeps user logic outside the integration.

The integration and blueprint are supported alternatives and must remain aligned as the panel evolves. Configure exactly one of them for a panel topic; enabling both would duplicate MQTT publishing and command handling. The blueprint keeps its user-defined footer action sequences and wake-up trigger selector. The integration intentionally uses ordinary Home Assistant event/button entities instead, so those user-specific automations remain visible and editable outside the integration.

When using the custom integration, manual Home Assistant automations are still required for full user-specific functionality: create one for each action-only **Footer Button N** event entity that should perform an action, and create your own triggers that press the integration's **Wake Panel** button when external events should wake the display. No manual automation is needed for media controls, favourites, clock/date, weather, chips, dimming, or a footer button bound to a toggleable state entity.

For those manual behaviors, import [the WallDisplay Integration Actions blueprint](config/blueprints/automation/walldisplay/integration_actions.yaml). Create one automation per desired footer action: add a **State** trigger for its **Footer Button N** event entity, then select the actions to run. Create a separate automation for wake-up behavior: select the external triggers and add a **Button: Press** action targeting the integration's **Wake Panel** entity. This small helper blueprint complements the custom integration; do not use it as an additional synchronization automation.

The integration is also packaged for HACS from the repository-root `custom_components/walldisplay_sync` directory. HACS uses the public GitHub mirror at `github.com/phbaer/walldisplay`; Forgejo manages that mirror, while GitHub Actions validates the HACS layout and publishes matching tag releases. A beta or release-candidate tag, such as `v0.4.0b1`, `v0.4.0rc1`, or `v0.4.0-beta.1`, is published as a GitHub prerelease.

For each non-default branch pushed to the GitHub mirror, **HACS branch preview** automatically validates the branch tip and publishes or replaces an installable prerelease. Its `v<base-version>b<number>` tag uses a deterministic numeric checksum of the branch name, while the release title identifies the branch. It deletes that release and tag when the branch is deleted from the mirror. Set `HACS_PRERELEASE_BASE_VERSION` in [`hacs-branch-preview.yml`](.github/workflows/hacs-branch-preview.yml) to the planned next integration version; it must be greater than the latest stable HACS release. To test an arbitrary branch or commit without pushing it to the mirror, run the **HACS integration** workflow manually with its ref and a unique prerelease tag. Testers enable the repository's HACS prerelease switch before checking for an update; stable releases remain the default.

The HACS validation workflow intentionally ignores the repository-level license and topic checks because those are configured on GitHub rather than committed files. Set a repository license and suitable GitHub topics before submitting the integration to HACS's default repository list.

The current firmware and blueprint release is `0.3.0`; the MQTT contract is `3`. During synchronization the blueprint publishes retained metadata to `<base>/set/blueprint_info`:

```json
{"version":"0.3.0","contract":"3"}
```

The panel compares that contract with its own value and exposes the result through diagnostic entities. Increment the contract in both firmware and blueprint whenever a topic, payload, discovery entity, or its meaning changes.

### MQTT topics

`<base>` is the configured panel topic.

| Purpose | Topic | Notes |
| --- | --- | --- |
| Availability | `<base>/status` | Retained `online` or `offline`. |
| Display updates | `<base>/set/...` | The panel also accepts the matching retained `state/...` topics. |
| Panel commands | `<base>/cmd/...` | Includes `button1`–`button5`, `sync`, configuration, and OTA. |
| Panel state | `<base>/state/...` | Retained canonical values and diagnostics. |

Use `set/name`, `set/weather`, `set/media`, `set/clock`, `set/date`, `set/chipN`, `set/chipN/color`, `set/buttonN/label`, and `set/buttonN/state` for display data. The panel republishes those values under `state/...`. `set/media` accepts either legacy text or JSON such as `{"state":"playing","title":"Track","artist":"Artist","album":"Album","source":"Radio","artwork_url":"https://example.invalid/cover.jpg"}`; unknown fields are retained for forward compatibility.

The Media page publishes non-retained `cmd/media/previous`, `cmd/media/play_pause`, `cmd/media/next`, `cmd/media/volume_down`, and `cmd/media/volume_up` commands. The blueprint maps them to the matching Home Assistant media-player actions. Support depends on the selected player. Configure up to five Media favourites with a label, symbol, and JSON payload for `media_player.play_media`; every favourite uses the selected Media player entity. For example, a web-radio favourite can use `{"media_content_id":"https://radio.example/stream.mp3","media_content_type":"music"}`. The custom integration exposes the same settings in its initial setup and **Configure** dialog. Labels are retained at `set/media/favoriteN/label` and touches publish `cmd/media/favoriteN`.

Media controls use built-in symbols. Each favourite also has a blueprint symbol selector: `none`, `radio`, `music`, `album`, `playlist`, or `podcast`. The selector publishes a retained `set/media/favoriteN/icon` value, which the panel maps to a bundled LVGL glyph; it is safe on the panel font and does not require emoji support. Touch callbacks queue their MQTT commands, keeping network/outbox work out of LVGL's input context. When `artwork_url` is a direct HTTPS baseline-JPEG URL, the panel downloads it asynchronously, downscales it to the media artwork area, and displays it. Protected or relative Home Assistant media-proxy URLs are intentionally not fetched because the panel does not store Home Assistant credentials.

The blueprint also publishes retained display-power settings to `set/display_power`, for example `{"dim_after":300,"off_after":600,"dim_percent":20}`. `dim_after` is the idle time before dimming, `off_after` is the additional dimmed time before the backlight turns off, and `dim_percent` is the dimmed brightness. A `dim_after` value of `0` disables display power saving; an `off_after` value of `0` keeps the display dimmed rather than turning it off. The defaults are 5 minutes, 10 additional minutes, and 20% brightness.

Weather can be text or JSON, for example:

```json
{"condition":"sunny","temperature":22.5,"forecast":[{"day":"Sun","condition":"rainy","high":24,"low":15}]}
```

Buttons publish to `<base>/cmd/buttonN`. In either the blueprint or custom integration, a configured state entity controls the visible toggle state and is toggled by a press; without one, the button is action-only. The integration exposes action-only presses as event entities, while the blueprint runs the configured action sequence. A `sync` command requests a complete refresh; the integration's **Sync Panel** entity sends that command and republishes all configured state. The discovered **Wake Panel** button publishes to `<base>/cmd/wake`; this and any touchscreen press reset the display timer and restore full brightness. The blueprint's optional **Wake-up triggers** input accepts multiple Home Assistant triggers and publishes the same non-retained wake command. This trigger-selector feature requires Home Assistant 2024.10 or newer.

For documentation, the discovered **Capture Screenshot** button (or a non-retained publish to `<base>/cmd/screenshot`) queues a capture of the current framebuffer. The panel writes a 480×480 24-bit BMP to its dedicated SPIFFS partition and publishes retained progress to `<base>/state/screenshot`. A blank, malformed, or unmounted screenshot partition is formatted automatically during screenshot initialization. When complete, the payload includes a local URL such as `http://192.0.2.10/screenshot.bmp`; open it from the same LAN to view or save the image. The endpoint is deliberately read-only and has no authentication, so expose the panel's HTTP port only on a trusted local network. A screenshot-storage or HTTP failure disables only this optional feature; it does not prevent the panel from starting.

### Discovered entities

When discovery is enabled, Home Assistant receives weather, media, time, date, chip values and colours, button state, five button controls, a sync button, a wake button, a screenshot button, MQTT-topic configuration, OTA-manifest configuration, and diagnostic sensors. Diagnostics include firmware and blueprint versions, contract compatibility, IP/MAC, Wi-Fi details, active partition, reset reason, uptime, and free heap/PSRAM.

## Releases and OTA

The Forgejo release workflow builds every push. It uses Forgejo's checkout action with the event ref, or the requested tag for a manual dispatch, and fetches complete history for changelog calculation. A `v*` tag also creates a release containing the application image, an OTA manifest, a factory archive, and a generated `CHANGELOG.md`; its release notes are calculated from Conventional Commit history since the previous `v*` tag. The workflow also replaces the wiki's `Changelog` page with that generated content; it owns that page but leaves all other wiki pages unchanged. Changelog entries are configured in [`cliff.toml`](cliff.toml): features, fixes, performance, refactors, tests, documentation, dependencies, and maintenance are grouped separately. Re-running a tagged release (including manual dispatch for that tag) replaces the existing same-tag release with newly built assets. The ESP-IDF build container installs Node.js for the checkout action, and tagged release jobs additionally install `jq`, `curl`, and Git Cliff in an isolated Python virtual environment, so the runner needs package-repository and Python-package access. Use an HTTPS manifest URL with `<base>/cmd/update` or the **Panel Update Manifest URL** entity. Do not retain the command.

The panel downloads the image to its inactive OTA partition and verifies its SHA-256. A new image is accepted only after MQTT connects; otherwise the next reset rolls back.

## Hardware notes

This project targets the ESP32-4848S040: ESP32-S3, 16 MB flash, octal PSRAM, 480×480 ST7701 RGB display, and GT911 touch.

- The display uses byte-swapped RGB565. LVGL renders with `RGB565_SWAPPED`; do not enable the port's in-place byte swap.
- Rendering uses direct mode and two frame buffers to avoid full-screen flashes during updates.
- The ST7701 pixel bus runs at 10 MHz; the backlight is GPIO 38. GT911 uses I²C on GPIO 19 (SDA) and GPIO 45 (SCL).
- The partition table includes read-only `appcfg`, writable NVS, factory and two OTA slots, and SPIFFS. SPIFFS holds the most recently captured documentation screenshot.
- The bundled Noto Sans face covers Latin, Greek, and Cyrillic (U+0020–U+052F). Emoji and CJK are not included.

## Third-party assets

This project is licensed under the [MIT License](LICENSE).

The HACS validation workflow ignores GitHub's repository-license metadata check because it can lag behind the committed license file. The `LICENSE` file remains the authoritative project license.

Weather icons are adapted from the dark icon set in [NSPanel-Easy](https://github.com/edwardtfn/NSPanel-Easy/tree/main/hmi/dev/pics/weather/dark), used under its [MIT License](https://github.com/edwardtfn/NSPanel-Easy/blob/main/LICENSE). The bundled Noto Sans font is licensed under the SIL Open Font License 1.1.

## Repository maintenance

Follow [AGENTS.md](AGENTS.md) and [the project-maintenance skill](skills/project-maintenance/SKILL.md). Keep this README current, align firmware and blueprint release versions, and validate firmware, blueprint YAML, and the final diff before handoff. Select the release and contract versions once for each cohesive feature branch; subsequent fixes in that branch keep those same values.

## Tests

Run `sh tests/run_unit_tests.sh` before merging. These host-runnable C unit tests cover display dimming and screen-off timing boundaries without requiring a panel. The Forgejo release workflow runs them before the ESP32-S3 build.
