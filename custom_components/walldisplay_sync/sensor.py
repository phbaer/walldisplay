"""Diagnostic sensors for the generated hostname and setup AP state."""
from __future__ import annotations

from homeassistant.components import mqtt
from homeassistant.components.sensor import SensorEntity
from homeassistant.helpers.entity import DeviceInfo

from .const import DOMAIN


async def async_setup_entry(hass, entry, async_add_entities) -> None:
    runtime = entry.runtime_data
    entities = [WallDisplayMqttSensor(runtime, "Panel Hostname", "hostname", "mdi:lan-connect"),
                WallDisplayMqttSensor(runtime, "Setup Access Point", "config/ap", "mdi:access-point-network")]
    async_add_entities(entities)
    for entity in entities:
        await entity.async_subscribe()


class WallDisplayMqttSensor(SensorEntity):
    _attr_has_entity_name = True

    def __init__(self, runtime, name: str, suffix: str, icon: str) -> None:
        self._runtime = runtime
        self._suffix = suffix
        self._attr_name = name
        self._attr_icon = icon
        self._attr_unique_id = f"{runtime.identifier}_{suffix.replace('/', '_')}"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, runtime.identifier)}, name=runtime.name,
                                            manufacturer="Guition", model="ESP32-4848S040")

    async def async_subscribe(self) -> None:
        async def message_received(msg):
            self._attr_native_value = msg.payload
            self.async_write_ha_state()
        self.async_on_remove(await mqtt.async_subscribe(self.hass, f"{self._runtime.topic}/state/{self._suffix}", message_received, 1))
