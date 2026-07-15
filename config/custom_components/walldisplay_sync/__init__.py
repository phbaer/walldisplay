"""Synchronize a Home Assistant media player with a WallDisplay panel."""
from __future__ import annotations

import json
import logging
from datetime import datetime, timedelta
from typing import Any

from homeassistant.components import mqtt
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import Event, HomeAssistant
from homeassistant.helpers.event import async_track_state_change_event
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.util import dt as dt_util

from .const import (
    CONF_MEDIA_ENTITY,
    CONF_PANEL_NAME,
    CONF_PANEL_TOPIC,
    CONF_WEATHER_ENTITY,
    CONF_DIM_AFTER,
    CONF_SCREEN_OFF_AFTER,
    CONF_DIM_BRIGHTNESS,
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
from .runtime import WallDisplayRuntime

_LOGGER = logging.getLogger(__name__)
_PLATFORMS = [Platform.BUTTON, Platform.EVENT]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    config = {**entry.data, **entry.options}
    topic = config[CONF_PANEL_TOPIC].rstrip("/")
    panel_name = config.get(CONF_PANEL_NAME, "") or entry.title
    entity_id = config[CONF_MEDIA_ENTITY]
    weather_entity = config.get(CONF_WEATHER_ENTITY, "")
    entry.runtime_data = WallDisplayRuntime(hass, topic, entry.title, entry.unique_id or entry.entry_id)

    async def publish_media(_: Event | None = None) -> None:
        state = hass.states.get(entity_id)
        if state is None:
            return
        attrs = state.attributes
        payload: dict[str, Any] = {
            "state": state.state,
            "title": attrs.get("media_title") or ("Media idle" if state.state in {"idle", "off", "standby"} else state.state.title()),
            "artist": attrs.get("media_artist") or "",
            "album": attrs.get("media_album_name") or "",
            "source": attrs.get("source") or "",
            "artwork_url": attrs.get("media_image_url") or "",
        }
        await mqtt.async_publish(hass, f"{topic}/set/media", json.dumps(payload), 1, True)

    async def publish_favorites() -> None:
        for slot in range(1, MEDIA_FAVORITE_COUNT + 1):
            await mqtt.async_publish(hass, f"{topic}/set/media/favorite{slot}/label", config.get(favorite_label_key(slot), ""), 1, True)
            await mqtt.async_publish(hass, f"{topic}/set/media/favorite{slot}/icon", config.get(favorite_icon_key(slot), "radio"), 1, True)

    async def publish_panel_configuration() -> None:
        await mqtt.async_publish(hass, f"{topic}/set/name", panel_name, 1, True)
        await mqtt.async_publish(hass, f"{topic}/set/display_power", json.dumps({"dim_after": config.get(CONF_DIM_AFTER, 300), "off_after": config.get(CONF_SCREEN_OFF_AFTER, 600), "dim_percent": config.get(CONF_DIM_BRIGHTNESS, 20)}), 1, True)

    async def publish_clock(_: datetime | None = None) -> None:
        now = dt_util.now()
        await mqtt.async_publish(hass, f"{topic}/set/clock", now.strftime("%H:%M"), 1, True)
        await mqtt.async_publish(hass, f"{topic}/set/date", now.strftime("%a, %d %b"), 1, True)

    async def publish_weather(_: Event | None = None) -> None:
        if not weather_entity or (state := hass.states.get(weather_entity)) is None:
            return
        attrs = state.attributes
        await mqtt.async_publish(hass, f"{topic}/set/weather", json.dumps({"condition": state.state, "temperature": attrs.get("temperature"), "forecast": []}), 1, True)

    async def publish_chip(slot: int) -> None:
        sensor = config.get(chip_sensor_key(slot), ""); state = hass.states.get(sensor) if sensor else None
        value = state.state if state else ""; unit = state.attributes.get("unit_of_measurement", "") if state else ""
        try: numeric = float(value)
        except (TypeError, ValueError): numeric = None
        color = config.get(CONF_CHIP_NEUTRAL_COLOR, "neutral") if numeric is None else (config.get(CONF_CHIP_ALERT_COLOR, "alert") if numeric > config.get(chip_alert_key(slot), 0) else config.get(CONF_CHIP_WARN_COLOR, "warn") if numeric > config.get(chip_warn_key(slot), 0) else config.get(CONF_CHIP_OK_COLOR, "ok"))
        await mqtt.async_publish(hass, f"{topic}/set/chip{slot}", f"{value}{' ' + unit if unit else ''}", 1, True)
        await mqtt.async_publish(hass, f"{topic}/set/chip{slot}/color", color, 1, True)
    async def publish_chips(_: Event | None = None) -> None:
        for slot in range(1, CHIP_COUNT + 1): await publish_chip(slot)
    async def publish_footers(_: Event | None = None) -> None:
        for slot in range(1, FOOTER_BUTTON_COUNT + 1):
            state_entity = config.get(footer_state_key(slot), ""); state = hass.states.get(state_entity) if state_entity else None
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
        command = message.topic.rsplit("/", 1)[-1]
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
            if 1 <= slot <= FOOTER_BUTTON_COUNT:
                state_entity = config.get(footer_state_key(slot), "")
                if state_entity:
                    await hass.services.async_call("homeassistant", "toggle", target={"entity_id": state_entity}, blocking=False)
                else:
                    entry.runtime_data.fire_footer_button(slot)

    async def publish_all() -> None:
        await publish_media(); await publish_panel_configuration(); await publish_clock(); await publish_favorites()
        await publish_weather(); await publish_chips(); await publish_footers()

    async def panel_status(message: mqtt.ReceiveMessage) -> None:
        if message.payload == "online":
            await publish_all()

    async def panel_sync(_: mqtt.ReceiveMessage) -> None:
        await publish_all()

    entry.async_on_unload(async_track_state_change_event(hass, [entity_id], publish_media))
    entry.async_on_unload(async_track_time_interval(hass, publish_clock, timedelta(minutes=1)))
    if weather_entity:
        entry.async_on_unload(async_track_state_change_event(hass, [weather_entity], publish_weather))
    chip_entities = [config.get(chip_sensor_key(slot), "") for slot in range(1, CHIP_COUNT + 1)]
    entry.async_on_unload(async_track_state_change_event(hass, [entity for entity in chip_entities if entity], publish_chips))
    footer_entities = [config.get(footer_state_key(slot), "") for slot in range(1, FOOTER_BUTTON_COUNT + 1)]
    entry.async_on_unload(async_track_state_change_event(hass, [entity for entity in footer_entities if entity], publish_footers))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/cmd/media/#", handle_command, 1))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/status", panel_status, 1))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/cmd/sync", panel_sync, 1))
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    await publish_all()
    await hass.config_entries.async_forward_entry_setups(entry, _PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, _PLATFORMS)


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)
