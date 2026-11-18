"""Runtime state shared by WallDisplay Sync platforms."""
from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
from typing import Any, Awaitable, Callable

from homeassistant.components import mqtt
from homeassistant.core import HomeAssistant

# Resolves the newest signed release newer than the panel, or None when the
# panel is current. Raises HomeAssistantError when the answer is unknown.
FindUpdate = Callable[[], Awaitable[Any]]


async def async_publish_update(hass: HomeAssistant, runtime: Any, topic: str, manifest_url: str, version: str) -> bool:
    """Request an OTA install of manifest_url unless one is already running.

    Shared by the Repairs flow and the Install Firmware Update button, so both
    refuse duplicate requests while the panel reports queued, checking, or
    installing. Returns False when a request is already running.
    """
    async with runtime.update_lock:
        if runtime.update_running:
            return False
        runtime.update_running = True
        runtime.update_target = version
    try:
        await mqtt.async_publish(hass, f"{topic}/cmd/update", manifest_url, 1, False)
    except Exception:
        runtime.update_running = False
        runtime.update_target = ""
        raise
    return True


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
    platforms: list[Any] = field(default_factory=list)
    find_update: FindUpdate | None = None

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
