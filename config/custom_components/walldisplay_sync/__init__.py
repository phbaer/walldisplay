"""Synchronize a Home Assistant media player with a WallDisplay panel."""
from __future__ import annotations

import json
import logging
from typing import Any

from homeassistant.components import mqtt
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import Event, HomeAssistant
from homeassistant.helpers.event import async_track_state_change_event

from .const import (
    CONF_MEDIA_ENTITY,
    CONF_PANEL_TOPIC,
    MEDIA_FAVORITE_COUNT,
    favorite_icon_key,
    favorite_label_key,
    favorite_payload_key,
)

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    config = {**entry.data, **entry.options}
    topic = config[CONF_PANEL_TOPIC].rstrip("/")
    entity_id = config[CONF_MEDIA_ENTITY]

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

    entry.async_on_unload(async_track_state_change_event(hass, [entity_id], publish_media))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{topic}/cmd/media/#", handle_command, 1))
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    await publish_media()
    await publish_favorites()
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return True


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)
