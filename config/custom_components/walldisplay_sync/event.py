"""Event entities for physical WallDisplay footer-button presses."""
from __future__ import annotations

from homeassistant.components.event import EventEntity
from homeassistant.helpers.entity import DeviceInfo

from .const import DOMAIN, FOOTER_BUTTON_COUNT
from .pages import GRID_COUNT


async def async_setup_entry(hass, entry, async_add_entities) -> None:
    runtime = entry.runtime_data
    entities = [WallDisplayFooterButtonEvent(runtime, slot) for slot in range(1, FOOTER_BUTTON_COUNT + GRID_COUNT + 1)]
    for entity in entities:
        runtime.events[entity.slot] = entity
    async_add_entities(entities)


class WallDisplayFooterButtonEvent(EventEntity):
    _attr_event_types = ["pressed"]
    _attr_has_entity_name = True

    def __init__(self, runtime, slot: int) -> None:
        self.slot = slot
        grid = slot > FOOTER_BUTTON_COUNT
        number = slot - FOOTER_BUTTON_COUNT if grid else slot
        self._attr_name = f"Buttons Page {number}" if grid else f"Footer Button {number}"
        kind = "grid_button" if grid else "footer_button"
        self._attr_unique_id = f"{runtime.identifier}_{kind}_{number}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, runtime.identifier)},
            name=runtime.name,
            manufacturer="Guition",
            model="ESP32-4848S040",
        )

    def press(self) -> None:
        self._trigger_event("pressed")
        self.async_write_ha_state()
