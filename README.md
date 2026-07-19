# Guition Wall Panel Firmware

Firmware for the ESP32-4848S040 wall panel, built with ESP-IDF, LVGL, MQTT, and Home Assistant MQTT Discovery. It shows time, weather, media, optional measurement chips, and up to five footer buttons; supports display dimming, HTTPS OTA with rollback, diagnostics, and screenshots.

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

`config/panel_config.yaml` provides build-time defaults; provisioned values in the read-only `appcfg` NVS partition take precedence. Give each panel a distinct base topic, such as `panel/guition-4848s040-kitchen`. Changing the discovered **Panel MQTT Topic** entity persists the new topic and restarts the panel; update the matching Home Assistant configuration to publish to the new topic.

## Home Assistant

Choose exactly one synchronization path per panel topic:

- Import [MQTT Sync](config/blueprints/automation/walldisplay/mqtt_sync.yaml) and set `panel_topic`, `panel_name`, `weather_entity`, and `media_entity`. It also owns configured footer-action sequences and optional wake-up triggers.
- Or install [`walldisplay_sync`](custom_components/walldisplay_sync) as a custom integration (the equivalent deployable copy is under `config/custom_components/`). Its native Setup and Configure flows manage panel settings and reload the integration after saving; it provides standard entities and events, so create normal automations for action-only **Footer Button N** event entities and for external events that press **Wake Panel**.

Both paths publish time, weather, media, favourites, chips, and footer state; map media commands to the selected media player. Do not enable both for one panel, or they will duplicate state and commands. The integration is HACS-packaged from the repository-root custom-component directory.

## MQTT contract

The firmware and blueprint release is `0.5.0`; MQTT contract `5`. MQTT Sync publishes retained compatibility metadata to `<base>/set/blueprint_info`:

```json
{"version":"0.5.0","contract":"5"}
```

| Purpose | Topic | Notes |
| --- | --- | --- |
| Availability | `<base>/status` | Retained `online` / `offline`. |
| Display updates | `<base>/set/...` | Matching retained `state/...` inputs also work. |
| Commands | `<base>/cmd/...` | Buttons, media, sync, configuration, OTA, and screenshots. |
| Panel state | `<base>/state/...` | Retained canonical values and diagnostics. |

The discovered **Default Panel Widget** select chooses `weather` or `media` as the page shown after startup. Its value is saved in the panel and applied immediately; it is controlled through `<base>/cmd/config/default_page` and reported at `<base>/state/config/default_page`.

Display data uses `set/name`, `set/weather`, `set/media`, `set/clock`, `set/date`, `set/chipN`, `set/chipN/color`, `set/buttonN/label`, and `set/buttonN/state`. The header shows large, unified clock and date labels at the upper left and upper right, respectively, both with 14 px top and outer-side padding; the panel name is smaller and muted below the clock. Select 24-hour (default) or 12-hour time in either synchronization path. Its Wi-Fi, MQTT, and Home Assistant indicators form one three-section rounded status control; only its outer corners are rounded. `set/media` accepts plain text or JSON, for example:

```json
{"state":"playing","title":"Track","artist":"Artist","artwork_url":"https://example.invalid/cover.jpg"}
```

Media commands are `cmd/media/previous`, `play_pause`, `next`, `volume_down`, `volume_up`, `volume`, and `favoriteN`. The media page places a horizontal blue volume rocker immediately to the right of previous, play/pause, and next: its generously sized transparent `−` and `+` touch ends have explicit matching outer insets and support a tap for a small adjustment or press-and-hold repeat, while its central slider publishes `cmd/media/volume` with an integer percentage from 0 to 100 and follows the `volume_level` media-state field. The favourites row uses the full width beneath the controls. The media page uses a 136 px cover-art area and a separate metadata column; favourites publish retained labels and icons, and their play-media JSON targets the selected player. `walldisplay_sync` fetches the selected player's artwork, converts it to a bounded JPEG, and serves it from a random-token panel URL; configure an internal or external Home Assistant URL reachable from the panel. MQTT Sync remains a no-custom-code alternative and forwards its media URL directly, so its source must already be a reachable baseline JPEG.

Weather accepts text or JSON with `condition`, `temperature`, `humidity`, `pressure`, `wind_speed`, `rainfall`, `irradiance`, a `trend` of up to 25 historic temperature values, and up to three forecast entries. The current temperature uses a larger bold value; available humidity, pressure, wind, rain, and solar-irradiation readings form a vertically aligned icon-led column on the right. Humidity uses a droplet, rain uses a downpour marker, and wind uses wave marks. The `wind_unit`, `rainfall_unit`, and `irradiance_unit` fields preserve sensor units. The panel renders the last 24 hours of temperature history as a rounded background curve beneath current conditions; it never uses forecast values. Both sync paths request 24 hourly Recorder means for the optional temperature sensor (or the weather entity when no sensor is configured), then append the current value. The trend requires Recorder data and a source entity that provides long-term statistics, normally a temperature sensor with `state_class: measurement`; otherwise the curve remains hidden. Both sync paths use tomorrow through day +3 only for the forecast cards; optional temperature, humidity, pressure, wind-speed, rainfall, and irradiance sensors override their matching weather-entity attributes. Wind falls back to the standard weather `wind_speed`/`wind_speed_unit` attributes; rainfall and irradiance use matching attributes when a provider exposes them. A configured footer state entity is toggled on press; otherwise the integration emits an event and MQTT Sync runs its configured action. `cmd/wake` and any touch restore brightness, while `cmd/sync` requests a complete refresh.

**Capture Screenshot** (or `cmd/screenshot`) stores a 480×480 BMP in SPIFFS and publishes progress at `state/screenshot`, including a LAN URL when complete. Publish an optional alphanumeric, `_`, or `-` name as the command payload to retain a named capture; retrieve it from `http://<panel>/screenshot.bmp?name=<name>`. `cmd/page` accepts `weather` or `media`, enabling non-touch visual checks. Run `PANEL_TOPIC=<base-topic> PANEL_HOST=<panel-ip> bash tools/capture_screenshots.sh` to publish fixed full/minimal-weather and idle/playing-media fixtures, capture each page, and download BMPs. [`tools/capture_screenshots.sh`](tools/capture_screenshots.sh) also accepts optional `MQTT_HOST`, `MQTT_PORT`, `OUTPUT_DIR`, and `RUN_ID` values. It requires `mosquitto_pub`, `mosquitto_sub`, `curl`, and `timeout`. The HTTP endpoint is read-only but unauthenticated; keep it on a trusted network.

## Architecture

Platform integration—ESP-IDF, FreeRTOS, MQTT, and LVGL entrypoints—remains C. Self-contained components use C++ behind C-compatible headers: `PanelComponent` defines the widget boundary, `MediaWidget` owns media rendering, Unicode-safe media text, and `ArtworkService` owns artwork queuing, HTTP, gap-free JPEG scaling, double-buffered PSRAM artwork, and atomic display swaps. The bundled Noto Sans UI font includes Latin, Greek, Cyrillic, and common punctuation used in media metadata; unsupported scripts safely fall back to `?`. Future widgets, such as weather, should follow this pattern rather than expand global UI state.

## Hardware and release notes

Target hardware: ESP32-S3, 16 MB flash, octal PSRAM, 480×480 ST7701 RGB display, and GT911 touch. LVGL uses byte-swapped RGB565 with direct rendering and two frame buffers. The partition table provides read-only `appcfg`, NVS, factory and OTA slots, and SPIFFS.

Use the discovered **Panel Update Manifest URL** entity or publish a non-retained HTTPS manifest URL to `<base>/cmd/update`. The panel verifies its SHA-256 and rolls back if the new image does not connect to MQTT.

## Development

Run `sh tests/run_unit_tests.sh` before merging. Follow [AGENTS.md](AGENTS.md) and [the maintenance workflow](skills/project-maintenance/SKILL.md): keep this README current, select one aligned firmware/blueprint release (and MQTT contract, when needed) from the feature base and retain it through all follow-up commits, validate affected artifacts, and commit verified closed implementation steps with short descriptive messages.

Licensed under [MIT](LICENSE). Weather condition icons derive from [NSPanel-Easy](https://github.com/edwardtfn/NSPanel-Easy/tree/main/hmi/dev/pics/weather/dark) (MIT). Weather measurements use the standard Font Awesome Free droplet, gauge, wind, rain-cloud, and sun symbols (CC BY 4.0); the bundled Noto Sans font uses the SIL Open Font License 1.1.
