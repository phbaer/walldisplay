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

    def fire_footer_button(self, slot: int) -> None:
        event = self.events.get(slot)
        if event is not None:
            event.press()
