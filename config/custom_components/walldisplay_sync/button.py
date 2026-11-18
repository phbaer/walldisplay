"""Panel command buttons exposed through Home Assistant."""
from __future__ import annotations

from homeassistant.components import mqtt
from homeassistant.components.button import ButtonEntity
from homeassistant.exceptions import HomeAssistantError

from .runtime import async_publish_update


async def async_setup_entry(hass, entry, async_add_entities) -> None:
    runtime = entry.runtime_data
    async_add_entities([WallDisplayCommandButton(runtime, "Wake Panel", "wake", "wake"),
                        WallDisplayCommandButton(runtime, "Sync Panel", "sync", "request"),
                        WallDisplayInstallUpdateButton(runtime)])


class WallDisplayCommandButton(ButtonEntity):
    _attr_has_entity_name = True

    def __init__(self, runtime, name: str, command: str, payload: str) -> None:
        self._runtime = runtime
        self._command = command
        self._payload = payload
        self._attr_name = name
        self._attr_unique_id = f"{runtime.identifier}_{command}"

    async def async_press(self) -> None:
        await mqtt.async_publish(self.hass, f"{self._runtime.topic}/cmd/{self._command}", self._payload, 1, False)


class WallDisplayInstallUpdateButton(ButtonEntity):
    """Start the newest signed firmware install without editing a URL.

    Each press checks the release feed again and then requests the install,
    so a failed download is retried by pressing the button again.
    """

    _attr_has_entity_name = True
    _attr_name = "Install Firmware Update"

    def __init__(self, runtime) -> None:
        self._runtime = runtime
        self._attr_unique_id = f"{runtime.identifier}_install_update"

    async def async_press(self) -> None:
        if self._runtime.find_update is None:
            raise HomeAssistantError("The integration is still starting; try again in a moment")
        release = await self._runtime.find_update()
        if release is None:
            raise HomeAssistantError("The panel already runs the newest signed firmware")
        started = await async_publish_update(self.hass, self._runtime, self._runtime.topic,
                                             release.manifest_url, release.version)
        if not started:
            raise HomeAssistantError("A firmware update is already in progress for this panel")
