"""Repair flows for WallDisplay firmware updates."""
from __future__ import annotations

import voluptuous as vol

from homeassistant.components import mqtt
from homeassistant.components.repairs import RepairsFlow, RepairsFlowResult


class FirmwareUpdateRepairFlow(RepairsFlow):
    """Confirm and start an OTA update for one panel."""

    def __init__(self, data: dict[str, str | int | float | None] | None) -> None:
        super().__init__()
        self._data = data or {}
        self.description_placeholders = {
            "panel_name": str(self._data.get("panel_name", "WallDisplay")),
            "current_version": str(self._data.get("current_version", "unknown")),
            "latest_version": str(self._data.get("latest_version", "unknown")),
        }

    async def async_step_init(self, user_input=None) -> RepairsFlowResult:
        return await self.async_step_confirm()

    async def async_step_confirm(self, user_input=None) -> RepairsFlowResult:
        if user_input is not None:
            topic = self._data.get("topic")
            manifest_url = self._data.get("manifest_url")
            if not isinstance(topic, str) or not isinstance(manifest_url, str):
                return self.async_abort(reason="update_unavailable")
            await mqtt.async_publish(self.hass, f"{topic}/cmd/update", manifest_url, 1, False)
            return self.async_create_entry(data={})
        return self.async_show_form(
            step_id="confirm",
            data_schema=vol.Schema({}),
            description_placeholders=self.description_placeholders,
        )


async def async_create_fix_flow(
    hass: HomeAssistant,
    issue_id: str,
    data: dict[str, str | int | float | None] | None,
) -> RepairsFlow:
    """Create the update confirmation flow."""
    if issue_id.startswith("firmware_update_"):
        return FirmwareUpdateRepairFlow(data)
    raise ValueError(f"unknown repair {issue_id}")
