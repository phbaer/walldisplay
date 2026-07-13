# Guition Wall Panel Firmware

Firmware for the Guition ESP32-4848S040 wall panel. It uses ESP-IDF, LVGL, MQTT, and Home Assistant MQTT Discovery.

## What it does

- Shows time, date, weather, media, optional measurement chips, and up to five footer buttons.
- Dims its PWM-controlled backlight after inactivity and can switch it off completely; a touch or wake command restores full brightness.
- Receives display state over MQTT and publishes button commands, availability, diagnostics, and retained state.
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

The current firmware and blueprint release is `0.2.1`; the MQTT contract is `2`. During synchronization the blueprint publishes retained metadata to `<base>/set/blueprint_info`:

```json
{"version":"0.2.1","contract":"2"}
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

Use `set/name`, `set/weather`, `set/media`, `set/clock`, `set/date`, `set/chipN`, `set/chipN/color`, `set/buttonN/label`, and `set/buttonN/state` for display data. The panel republishes those values under `state/...`.

The blueprint also publishes retained display-power settings to `set/display_power`, for example `{"dim_after":300,"off_after":600,"dim_percent":20}`. `dim_after` is the idle time before dimming, `off_after` is the additional dimmed time before the backlight turns off, and `dim_percent` is the dimmed brightness. A `dim_after` value of `0` disables display power saving; an `off_after` value of `0` keeps the display dimmed rather than turning it off. The defaults are 5 minutes, 10 additional minutes, and 20% brightness.

Weather can be text or JSON, for example:

```json
{"condition":"sunny","temperature":22.5,"forecast":[{"day":"Sun","condition":"rainy","high":24,"low":15}]}
```

Buttons publish to `<base>/cmd/buttonN`. A configured state entity controls the visible toggle state; without one, the button is action-only. A `sync` command requests a complete blueprint refresh. The discovered **Wake Panel** button publishes to `<base>/cmd/wake`; this and any touchscreen press reset the display timer and restore full brightness. The blueprint's optional **Wake-up triggers** input accepts multiple Home Assistant triggers and publishes the same non-retained wake command. This trigger-selector feature requires Home Assistant 2024.10 or newer.

### Discovered entities

When discovery is enabled, Home Assistant receives weather, media, time, date, chip values and colours, button state, five button controls, a sync button, a wake button, MQTT-topic configuration, OTA-manifest configuration, and diagnostic sensors. Diagnostics include firmware and blueprint versions, contract compatibility, IP/MAC, Wi-Fi details, active partition, reset reason, uptime, and free heap/PSRAM.

## Releases and OTA

The Forgejo release workflow builds every push. It uses Forgejo's checkout action with the event ref, or the requested tag for a manual dispatch, and fetches complete history for changelog calculation. A `v*` tag also creates a release containing the application image, an OTA manifest, a factory archive, and a generated `CHANGELOG.md`; its release notes are calculated from Conventional Commit history since the previous `v*` tag. Changelog entries are configured in [`cliff.toml`](cliff.toml): features, fixes, performance, refactors, tests, documentation, dependencies, and maintenance are grouped separately. Re-running a tagged release (including manual dispatch for that tag) replaces the existing same-tag release with newly built assets. The ESP-IDF build container installs Node.js for the checkout action, and tagged release jobs additionally install `jq`, `curl`, and Git Cliff, so the runner needs package-repository and Python-package access. Use an HTTPS manifest URL with `<base>/cmd/update` or the **Panel Update Manifest URL** entity. Do not retain the command.

The panel downloads the image to its inactive OTA partition and verifies its SHA-256. A new image is accepted only after MQTT connects; otherwise the next reset rolls back.

## Hardware notes

This project targets the ESP32-4848S040: ESP32-S3, 16 MB flash, octal PSRAM, 480×480 ST7701 RGB display, and GT911 touch.

- The display uses byte-swapped RGB565. LVGL renders with `RGB565_SWAPPED`; do not enable the port's in-place byte swap.
- Rendering uses direct mode and two frame buffers to avoid full-screen flashes during updates.
- The ST7701 pixel bus runs at 10 MHz; the backlight is GPIO 38. GT911 uses I²C on GPIO 19 (SDA) and GPIO 45 (SCL).
- The partition table includes read-only `appcfg`, writable NVS, factory and two OTA slots, and SPIFFS.
- The bundled Noto Sans face covers Latin, Greek, and Cyrillic (U+0020–U+052F). Emoji and CJK are not included.

## Third-party assets

Weather icons are adapted from the dark icon set in [NSPanel-Easy](https://github.com/edwardtfn/NSPanel-Easy/tree/main/hmi/dev/pics/weather/dark), used under its [MIT License](https://github.com/edwardtfn/NSPanel-Easy/blob/main/LICENSE). The bundled Noto Sans font is licensed under the SIL Open Font License 1.1.

## Repository maintenance

Follow [AGENTS.md](AGENTS.md) and [the project-maintenance skill](skills/project-maintenance/SKILL.md). Keep this README current, align firmware and blueprint release versions, and validate firmware, blueprint YAML, and the final diff before handoff.

## Tests

Run `sh tests/run_unit_tests.sh` before merging. These host-runnable C unit tests cover display dimming and screen-off timing boundaries without requiring a panel. The Forgejo release workflow runs them before the ESP32-S3 build.
