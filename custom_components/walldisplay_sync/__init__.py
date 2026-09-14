"""Synchronize a Home Assistant media player with a WallDisplay panel."""
from __future__ import annotations

import json
import logging
import asyncio
from datetime import datetime, timedelta
from typing import Any

from homeassistant.components import mqtt
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import Event, HomeAssistant
from homeassistant.helpers.event import async_track_state_change_event
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.helpers import issue_registry as ir
from homeassistant.util import dt as dt_util

from .const import (
    CONF_MEDIA_ENTITY,
    CONF_MEDIA_POWER_SWITCH,
    CONF_PANEL_NAME,
    CONF_PANEL_HOSTNAME,
    CONF_PANEL_TOPIC,
    CONF_WEATHER_ENTITY,
    CONF_TEMPERATURE_ENTITY,
    CONF_HUMIDITY_ENTITY, CONF_PRESSURE_ENTITY,
    CONF_WIND_SPEED_ENTITY, CONF_RAINFALL_ENTITY, CONF_IRRADIANCE_ENTITY,
    CONF_DIM_AFTER,
    CONF_SCREEN_OFF_AFTER,
    CONF_TIME_FORMAT,
    CONF_DIM_BRIGHTNESS,
    CONF_UPDATE_API_URL,
    DOMAIN,
    CONF_CHIP_OK_COLOR,
    CONF_CHIP_WARN_COLOR,
    CONF_CHIP_ALERT_COLOR,
    CONF_CHIP_NEUTRAL_COLOR,
    CHIP_COUNT, chip_sensor_key, chip_warn_key, chip_alert_key,
    footer_label_key, footer_state_key,
    FOOTER_BUTTON_COUNT,
    MEDIA_FAVORITE_COUNT,
    favorite_icon_key,
    favorite_label_key,
    favorite_payload_key,
)
from .pages import GRID_COUNT, layout_payload, grid_payload
from .sync_jobs import publish_independently
from .configuration import migrate_configuration
from .runtime import WallDisplayRuntime
from .artwork import ArtworkCache, async_register_cache, async_unregister_cache
from .updates import async_latest_release, is_newer

_LOGGER = logging.getLogger(__name__)
_PLATFORMS = [Platform.BUTTON, Platform.EVENT]
_UPDATE_ISSUE_PREFIX = "firmware_update_"
_UPDATE_CHECK_INTERVAL = timedelta(hours=6)
_UPDATE_RETRY_INTERVAL = timedelta(minutes=5)


def _forecast_day(value: Any) -> str:
    """Return a compact weekday label from a Home Assistant forecast timestamp."""
    if isinstance(value, datetime):
        return value.strftime("%a")
    try:
        return datetime.fromisoformat(str(value).replace("Z", "+00:00")).strftime("%a")
    except ValueError:
        return str(value)[:3]


def _artwork_url(hass: HomeAssistant, value: Any) -> str:
    """Make Home Assistant's tokenized relative media-proxy URLs panel-readable."""
    if not isinstance(value, str) or not value:
        return ""
    if value.startswith(("http://", "https://")):
        return value
    base_url = hass.config.internal_url or hass.config.external_url
    return f"{base_url.rstrip('/')}{value}" if base_url and value.startswith("/") else ""


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    config = migrate_configuration({**entry.data, **entry.options})
    topic = config[CONF_PANEL_TOPIC].rstrip("/")
    panel_name = config.get(CONF_PANEL_NAME, "") or entry.title
    panel_hostname = config.get(CONF_PANEL_HOSTNAME, "")
    entity_id = config.get(CONF_MEDIA_ENTITY, "")
    power_switch = config.get(CONF_MEDIA_POWER_SWITCH, "")
    weather_entity = config.get(CONF_WEATHER_ENTITY, "")
    temperature_entity = config.get(CONF_TEMPERATURE_ENTITY, "")
    humidity_entity = config.get(CONF_HUMIDITY_ENTITY, "")
    pressure_entity = config.get(CONF_PRESSURE_ENTITY, "")
    wind_speed_entity = config.get(CONF_WIND_SPEED_ENTITY, "")
    rainfall_entity = config.get(CONF_RAINFALL_ENTITY, "")
    irradiance_entity = config.get(CONF_IRRADIANCE_ENTITY, "")
    update_api_url = config.get(CONF_UPDATE_API_URL, "")
    entry.runtime_data = WallDisplayRuntime(hass, topic, entry.title, entry.unique_id or entry.entry_id)
    artwork = ArtworkCache(hass, entry.entry_id)
    async_register_cache(hass, artwork)
    entry.async_on_unload(lambda: async_unregister_cache(hass, entry.entry_id))

    current_version = ""
    last_update_check: datetime | None = None
    update_check_lock = asyncio.Lock()
    update_issue_id = f"{_UPDATE_ISSUE_PREFIX}{entry.entry_id}"
    if not update_api_url:
        ir.async_delete_issue(hass, DOMAIN, update_issue_id)

    async def check_for_update(force: bool = False) -> None:
        nonlocal last_update_check
        if not current_version or not update_api_url:
            return
        now = dt_util.now()
        if not force and last_update_check is not None and now - last_update_check < _UPDATE_RETRY_INTERVAL:
            return
        last_update_check = now
        async with update_check_lock:
            release = await async_latest_release(hass, update_api_url)
            # Keep an existing issue when the release API is temporarily
            # unavailable. A later panel heartbeat or scheduled check retries.
            if release is None:
                return
            if not is_newer(current_version, release):
                ir.async_delete_issue(hass, DOMAIN, update_issue_id)
                return
            ir.async_create_issue(
                hass,
                DOMAIN,
                update_issue_id,
                data={
                    "entry_id": entry.entry_id,
                    "topic": topic,
                    "panel_name": panel_name,
                    "current_version": current_version,
                    "latest_version": release.version,
                    "manifest_url": release.manifest_url,
                },
                is_fixable=True,
                is_persistent=True,
                issue_domain=DOMAIN,
                severity=ir.IssueSeverity.WARNING,
                translation_key="firmware_update_available",
                translation_placeholders={
                    "panel_name": panel_name,
                    "current_version": current_version,
                    "latest_version": release.version,
                },
            )

    async def panel_version(message: mqtt.ReceiveMessage) -> None:
        nonlocal current_version
        try:
            payload = json.loads(message.payload)
        except (TypeError, json.JSONDecodeError):
            return
        state = payload.get("state") if isinstance(payload, dict) else None
        version = (payload.get("firmware_version") or payload.get("version")) if isinstance(payload, dict) else None
        runtime = entry.runtime_data
        if state in {"queued", "checking", "installing"}:
            runtime.update_running = True
            if isinstance(version, str) and version:
                runtime.update_target = version
        elif state == "error":
            runtime.update_running = False
            runtime.update_target = ""
        if isinstance(version, str) and version:
            if version != current_version:
                current_version = version
            if (runtime.update_running and runtime.update_target and
                    getattr(message, "topic", "").endswith("/state/device") and version == runtime.update_target):
                runtime.update_running = False
                runtime.update_target = ""
            # The panel publishes retained device information periodically.
            # Retry checks after startup/network failures, with a debounce.
            await check_for_update()

    media_generation = 0
    async def publish_media(_: Event | None = None) -> None:
        nonlocal media_generation
        media_generation += 1
        generation = media_generation
        state = hass.states.get(entity_id) if entity_id else None
        if state is None:
            return
        attrs = state.attributes
        payload: dict[str, Any] = {
            "state": state.state,
            "title": attrs.get("media_title") or ("Media idle" if state.state in {"idle", "off", "standby"} else state.state.title()),
            "artist": attrs.get("media_artist") or "",
            "album": attrs.get("media_album_name") or "",
            "source": attrs.get("source") or "",
            "volume_level": attrs.get("volume_level"),
            "artwork_url": "",
        }
        source = _artwork_url(hass, attrs.get("entity_picture") or attrs.get("media_image_url"))
        base_url = hass.config.internal_url or hass.config.external_url
        if base_url and await artwork.async_update(source):
            payload["artwork_url"] = artwork.url(base_url)
        if generation == media_generation:
            await mqtt.async_publish(hass, f"{topic}/set/media", json.dumps(payload), 1, True)

    async def publish_favorites() -> None:
        for slot in range(1, MEDIA_FAVORITE_COUNT + 1):
            await mqtt.async_publish(hass, f"{topic}/set/media/favorite{slot}/label", config.get(favorite_label_key(slot), ""), 1, True)
            await mqtt.async_publish(hass, f"{topic}/set/media/favorite{slot}/icon", config.get(favorite_icon_key(slot), "radio"), 1, True)

    async def publish_panel_configuration() -> None:
        await mqtt.async_publish(hass, f"{topic}/set/screenshots_enabled", "ON" if config["screenshots_enabled"] else "OFF", 1, True)
        await mqtt.async_publish(hass, f"{topic}/set/pages", json.dumps(layout_payload(config), ensure_ascii=False), 1, True)
        # Keep the compatibility payload aligned with the generated manifest.
        await mqtt.async_publish(hass, f"{topic}/set/blueprint_info", json.dumps({"version": "1.0.0", "contract": "9"}), 1, True)
        await mqtt.async_publish(hass, f"{topic}/set/name", panel_name, 1, True)
        await mqtt.async_publish(
            hass,
            f"{topic}/set/display_power",
            json.dumps({
                "dim_after": config.get(CONF_DIM_AFTER, 300),
                "off_after": config.get(CONF_SCREEN_OFF_AFTER, 600),
                "dim_percent": config.get(CONF_DIM_BRIGHTNESS, 20),
            }),
            1,
            True,
        )

    async def publish_clock(_: datetime | None = None) -> None:
        now = dt_util.now()
        time_format = "%-I:%M %p" if config.get(CONF_TIME_FORMAT, "24h") == "12h" else "%H:%M"
        await mqtt.async_publish(hass, f"{topic}/set/clock", now.strftime(time_format), 1, True)
        await mqtt.async_publish(hass, f"{topic}/set/date", now.strftime("%a, %d %b"), 1, True)

    async def publish_weather(_: Event | None = None) -> None:
        if not weather_entity or (state := hass.states.get(weather_entity)) is None:
            return
        attrs = state.attributes
        forecast = []
        try:
            response = await hass.services.async_call("weather", "get_forecasts", {"type": "daily"}, target={"entity_id": weather_entity}, blocking=True, return_response=True)
            forecast = response.get(weather_entity, {}).get("forecast", [])[1:4]
        except Exception:  # Weather providers may not support forecasts.
            _LOGGER.debug("Unable to retrieve daily weather forecast", exc_info=True)
        def metric(entity: str, attribute: str) -> Any:
            value = hass.states.get(entity).state if entity and hass.states.get(entity) else attrs.get(attribute)
            try: return float(value)
            except (TypeError, ValueError): return None
        def metric_with_unit(entity: str, attribute: str, unit_attribute: str) -> tuple[float | None, str]:
            source = hass.states.get(entity) if entity else None
            value = source.state if source else attrs.get(attribute)
            unit = source.attributes.get("unit_of_measurement") if source else attrs.get(unit_attribute, "")
            try: return float(value), str(unit or "")
            except (TypeError, ValueError): return None, ""
        temperature = metric(temperature_entity, "temperature")
        trend_entity = temperature_entity or weather_entity
        history = []
        try:
            response = await hass.services.async_call("recorder", "get_statistics", {
                "statistic_ids": [trend_entity],
                "start_time": (dt_util.now() - timedelta(hours=24)).isoformat(),
                "end_time": dt_util.now().isoformat(),
                "period": "hour",
                "types": ["mean"],
            }, blocking=True, return_response=True)
            history = response.get("statistics", {}).get(trend_entity, [])
        except Exception:  # Recorder statistics may be unavailable for the source entity.
            _LOGGER.debug("Unable to retrieve temperature history", exc_info=True)
        def number(value: Any) -> float | None:
            try: return float(value)
            except (TypeError, ValueError): return None
        trend = [value for item in history if (value := number(item.get("mean"))) is not None]
        if temperature is not None and (not trend or trend[-1] != temperature): trend.append(temperature)
        trend = trend[-25:]
        wind_speed, wind_unit = metric_with_unit(wind_speed_entity, "wind_speed", "wind_speed_unit")
        rainfall, rainfall_unit = metric_with_unit(rainfall_entity, "precipitation", "precipitation_unit")
        irradiance, irradiance_unit = metric_with_unit(irradiance_entity, "irradiance", "irradiance_unit")
        payload = {"condition": state.state, "temperature": temperature, "humidity": metric(humidity_entity, "humidity"), "pressure": metric(pressure_entity, "pressure"), "wind_speed": wind_speed, "wind_unit": wind_unit, "rainfall": rainfall, "rainfall_unit": rainfall_unit, "irradiance": irradiance, "irradiance_unit": irradiance_unit, "trend": trend, "forecast": [
            {"day": _forecast_day(item.get("datetime", "")), "condition": item.get("condition", "unknown"), "high": item.get("temperature"), "low": item.get("templow", item.get("temperature"))} for item in forecast]}
        await mqtt.async_publish(hass, f"{topic}/set/weather", json.dumps(payload), 1, True)

    async def publish_chip(slot: int) -> None:
        sensor = config.get(chip_sensor_key(slot), "")
        state = hass.states.get(sensor) if sensor else None
        value = state.state if state else ""
        unit = state.attributes.get("unit_of_measurement", "") if state else ""
        try: numeric = float(value)
        except (TypeError, ValueError): numeric = None
        color = (
            config.get(CONF_CHIP_NEUTRAL_COLOR, "neutral") if numeric is None
            else config.get(CONF_CHIP_ALERT_COLOR, "alert") if numeric > config.get(chip_alert_key(slot), 0)
            else config.get(CONF_CHIP_WARN_COLOR, "warn") if numeric > config.get(chip_warn_key(slot), 0)
            else config.get(CONF_CHIP_OK_COLOR, "ok")
        )
        await mqtt.async_publish(hass, f"{topic}/set/chip{slot}", f"{value}{' ' + unit if unit else ''}", 1, True)
        await mqtt.async_publish(hass, f"{topic}/set/chip{slot}/color", color, 1, True)

    async def publish_chips(_: Event | None = None) -> None:
        for slot in range(1, CHIP_COUNT + 1): await publish_chip(slot)

    async def publish_footers(_: Event | None = None) -> None:
        for slot in range(1, FOOTER_BUTTON_COUNT + 1):
            state_entity = config.get(footer_state_key(slot), "")
            state = hass.states.get(state_entity) if state_entity else None
            await mqtt.async_publish(hass, f"{topic}/set/button{slot}/label", config.get(footer_label_key(slot), ""), 1, True)
            await mqtt.async_publish(hass, f"{topic}/set/button{slot}/state", state.state if state else "stateless", 1, True)

    async def play_favorite(slot: int) -> None:
        payload_text = config.get(favorite_payload_key(slot), "").strip()
        if not payload_text:
            _LOGGER.debug("Ignoring unconfigured WallDisplay favourite %s", slot)
            return
        try:
            payload = json.loads(payload_text)
        except json.JSONDecodeError:
            _LOGGER.warning("Ignoring WallDisplay favourite %s: payload is not valid JSON", slot)
            return
        if not isinstance(payload, dict):
            _LOGGER.warning("Ignoring WallDisplay favourite %s: payload must be a JSON object", slot)
            return
        await hass.services.async_call("media_player", "play_media", {**payload, "entity_id": entity_id}, blocking=False)

    async def handle_command(message: mqtt.ReceiveMessage) -> None:
        if getattr(message, "retain", False):
            return
        command = message.topic.rsplit("/", 1)[-1]
        if command == "volume":
            try:
                volume_level = min(100, max(0, float(message.payload))) / 100
            except (TypeError, ValueError):
                _LOGGER.warning("Ignoring invalid WallDisplay volume payload: %r", message.payload)
                return
            await hass.services.async_call("media_player", "volume_set", {"entity_id": entity_id, "volume_level": volume_level}, blocking=False)
            return
        if command == "power_off":
            power_switch_state = hass.states.get(power_switch) if power_switch else None
            if power_switch_state is not None and power_switch_state.state not in {"unknown", "unavailable"}:
                await hass.services.async_call("switch", "turn_off", {"entity_id": power_switch}, blocking=False)
            else:
                await hass.services.async_call("media_player", "turn_off", {"entity_id": entity_id}, blocking=False)
            return
        service = {
            "previous": "media_previous_track",
            "play_pause": "media_play_pause",
            "next": "media_next_track",
            "volume_down": "volume_down",
            "volume_up": "volume_up",
        }.get(command)
        if service:
            await hass.services.async_call("media_player", service, {"entity_id": entity_id}, blocking=False)
            return
        if command.startswith("favorite"):
            try:
                slot = int(command.removeprefix("favorite"))
            except ValueError:
                return
            if 1 <= slot <= MEDIA_FAVORITE_COUNT:
                await play_favorite(slot)
            return
        if command.startswith("button"):
            try:
                slot = int(command.removeprefix("button"))
            except ValueError:
                return
            if 1 <= slot <= FOOTER_BUTTON_COUNT and config.get(footer_label_key(slot), ""):
                state_entity = config.get(footer_state_key(slot), "")
                if state_entity:
                    state = hass.states.get(state_entity)
                    if state is None or state.state in {"unknown", "unavailable"}:
                        return
                    await hass.services.async_call(
                        "homeassistant",
                        "toggle",
                        target={"entity_id": state_entity},
                        blocking=False,
                    )
                else:
                    entry.runtime_data.fire_footer_button(slot)

    async def publish_grid(_: Event | None = None) -> None:
        for slot in range(1, GRID_COUNT + 1):
            entity = config.get(f"grid{slot}_state_entity", "")
            state = hass.states.get(entity) if entity else None
            value = state.state if state else ("unavailable" if entity else "stateless")
            await mqtt.async_publish(hass, f"{topic}/set/grid/{slot}", json.dumps(grid_payload(config, slot, value)), 1, True)

    async def handle_grid(message: mqtt.ReceiveMessage) -> None:
        if getattr(message, "retain", False):
            return
        suffix = message.topic.removeprefix(f"{topic}/cmd/grid")
        if suffix not in {str(i) for i in range(1, GRID_COUNT + 1)} or message.payload != "press":
            return
        slot = int(suffix)
        if not config.get(f"grid{slot}_label") or "buttons" not in layout_payload(config)["pages"]:
            return
        entity = config.get(f"grid{slot}_state_entity", "")
        if entity:
            state = hass.states.get(entity)
            if state and state.state not in {"unknown", "unavailable"}:
                await hass.services.async_call("homeassistant", "toggle", target={"entity_id": entity}, blocking=False)
        else:
            entry.runtime_data.fire_footer_button(FOOTER_BUTTON_COUNT + slot)

    async def publish_all() -> None:
        # Each publisher can fail or wait independently of configuration and controls.
        await publish_independently(
            publish_panel_configuration, publish_clock, publish_favorites,
            publish_chips, publish_footers, publish_grid, publish_media, publish_weather,
        )

    async def panel_status(message: mqtt.ReceiveMessage) -> None:
        if message.payload == "online":
            await publish_all()

    async def panel_sync(_: mqtt.ReceiveMessage) -> None:
        await publish_all()

    await hass.config_entries.async_forward_entry_setups(entry, _PLATFORMS)
    if entity_id:
        entry.async_on_unload(async_track_state_change_event(hass, [entity_id], publish_media))
    grid_entities = [config.get(f"grid{i}_state_entity", "") for i in range(1, GRID_COUNT + 1)]
    if any(grid_entities):
        entry.async_on_unload(async_track_state_change_event(hass, [entity for entity in grid_entities if entity], publish_grid))
    for slot in range(1, GRID_COUNT + 1):
        entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/cmd/grid{slot}", handle_grid, 1))
    entry.async_on_unload(async_track_time_interval(hass, publish_clock, timedelta(minutes=1)))
    weather_sources = [entity for entity in [weather_entity, temperature_entity, humidity_entity, pressure_entity, wind_speed_entity, rainfall_entity, irradiance_entity] if entity]
    if weather_sources:
        entry.async_on_unload(async_track_state_change_event(hass, weather_sources, publish_weather))
    chip_entities = [config.get(chip_sensor_key(slot), "") for slot in range(1, CHIP_COUNT + 1)]
    entry.async_on_unload(async_track_state_change_event(hass, [entity for entity in chip_entities if entity], publish_chips))
    footer_entities = [config.get(footer_state_key(slot), "") for slot in range(1, FOOTER_BUTTON_COUNT + 1)]
    entry.async_on_unload(async_track_state_change_event(hass, [entity for entity in footer_entities if entity], publish_footers))
    for slot in range(1, FOOTER_BUTTON_COUNT + 1):
        entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/cmd/button{slot}", handle_command, 1))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/cmd/media/#", handle_command, 1))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/status", panel_status, 1))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/cmd/sync", panel_sync, 1))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/state/device", panel_version, 1))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/state/update", panel_version, 1))
    if update_api_url:
        entry.async_on_unload(
            async_track_time_interval(hass, lambda _: check_for_update(force=True), _UPDATE_CHECK_INTERVAL)
        )
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    await publish_all()
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, _PLATFORMS)


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)

async def async_migrate_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    if entry.version == 1:
        data = migrate_configuration({**entry.data, **entry.options})
        hass.config_entries.async_update_entry(entry, data=data, options={}, version=2)
        return True
    return entry.version == 2
