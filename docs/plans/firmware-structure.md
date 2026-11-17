# Firmware structure refactor

Status: implemented, hardware acceptance outstanding.

## Fixed feature selection

- Branch: `refactor/firmware-structure`.
- Base: `55c2750` on `fix/setup-portal-hardening` (release `1.3.1`, MQTT contract `12`).
- Selected release: `1.3.2` (refactor; no intended behavior change).
- MQTT contract: unchanged at `12`. Topics, payloads, and discovery entities are unchanged.
- Retain these selected versions for all follow-up fixes on this branch.

The blueprint and the custom integration change only in their generated
version references.

## Changes

| Area | Before | After |
| --- | --- | --- |
| Inbound MQTT routing | `on_mqtt_message` formatted up to about 100 full topics per message and compared each one. | `panel_topics.c` resolves the suffix below the base topic once; three handlers compare suffixes. |
| Subscriptions | About 80 individual subscriptions per connect. | `set/#`, `cmd/#`, 35 legacy `state/...` aliases, and `homeassistant/status`. |
| Configuration access | One mutable global read by several tasks while MQTT and portal handlers wrote it. | Immutable snapshots published by serialized writers; see the README architecture section. |
| Unused configuration setters | Four setters without callers, one of which left memory and NVS inconsistent. | Removed. |
| Setup portal | Station, AP, DNS, HTTP page, form parsing, and MQTT probe in `wifi_manager.c`. | Page, forms, and DNS in `setup_portal.c`; `wifi_manager.c` keeps station, AP, and connection tests. |

## Intended differences

- The broker now delivers the panel's own `cmd/...` publications and any
  unknown `set/...` topic to the panel; both are ignored. The previous
  subscription to `state/hostname` had no handler and is gone.
- After a successful default-page command the panel publishes
  `state/config/pages` before `state/config/default_page` (previously the
  reverse). The values are the same.
- A handled message is logged after it was applied instead of before, with the
  suffix instead of the full topic.

## Verification (2026-11-17)

- ESP-IDF 6.0.2 unsigned firmware build passed at each of the three commits.
- Native unit checks cover suffix resolution, indexed and aliased matching, and
  the exact subscription list; the configuration persistence test runs against
  the snapshot implementation; the Python suite passed.
- Not verified: no panel was flashed, and there is no host test that drives
  `on_mqtt_message` itself. Before release, confirm on a panel that both Home
  Assistant paths still synchronize every page, that footer, grid, media, and
  favourite presses still act, and that the setup portal still works.
