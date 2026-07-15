"""Panel command buttons exposed through Home Assistant."""
from __future__ import annotations
from homeassistant.components import mqtt
from homeassistant.components.button import ButtonEntity
from homeassistant.helpers.entity import DeviceInfo
from .const import DOMAIN

async def async_setup_entry(hass, entry, async_add_entities) -> None:
    runtime = entry.runtime_data
    async_add_entities([WallDisplayCommandButton(runtime, "Wake Panel", "wake", "wake"), WallDisplayCommandButton(runtime, "Sync Panel", "sync", "request")])

class WallDisplayCommandButton(ButtonEntity):
    _attr_has_entity_name = True
    def __init__(self, runtime, name: str, command: str, payload: str) -> None:
        self._runtime, self._command, self._payload = runtime, command, payload
        self._attr_name, self._attr_unique_id = name, f"{runtime.identifier}_{command}"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, runtime.identifier)}, name=runtime.name, manufacturer="Guition", model="ESP32-4848S040")
    async def async_press(self) -> None:
        await mqtt.async_publish(self.hass, f"{self._runtime.topic}/cmd/{self._command}", self._payload, 1, False)
