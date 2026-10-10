"""Runtime state shared by WallDisplay Sync platforms."""
from __future__ import annotations

import asyncio
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
    update_lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    update_running: bool = False
    update_target: str = ""

    @property
    def device_identifiers(self) -> set[tuple[str, str]]:
        """Use the firmware MQTT Discovery identity for the physical panel."""
        topic_id = ''.join(chr(b) if (65 <= b <= 90 or 97 <= b <= 122 or 48 <= b <= 57) else '_'
                           for b in self.topic.encode('utf-8'))
        return {("mqtt", f"walldisplay_{topic_id}_device"[:143])}

    def fire_footer_button(self, slot: int) -> None:
        event = self.events.get(slot)
        if event is not None:
            event.press()
