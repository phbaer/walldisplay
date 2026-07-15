"""Runtime state shared by WallDisplay Sync platforms."""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Any
from homeassistant.core import HomeAssistant

@dataclass
class WallDisplayRuntime:
    hass: HomeAssistant
    topic: str
    name: str
    identifier: str
    events: dict[int, Any] = field(default_factory=dict)
    def fire_footer_button(self, slot: int) -> None:
        event = self.events.get(slot)
        if event is not None:
            event.async_set_event_type("pressed")
