from __future__ import annotations

import voluptuous as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import selector

from .const import (
    CONF_MEDIA_ENTITY,
    CONF_PANEL_TOPIC,
    DOMAIN,
    MEDIA_FAVORITE_COUNT,
    favorite_icon_key,
    favorite_label_key,
    favorite_payload_key,
)

_FAVORITE_ICONS = ["none", "radio", "music", "album", "playlist", "podcast"]


def _schema(defaults: dict[str, str] | None = None) -> vol.Schema:
    defaults = defaults or {}
    fields: dict[vol.Marker, object] = {
        vol.Required(CONF_PANEL_TOPIC, default=defaults.get(CONF_PANEL_TOPIC, "")): str,
        vol.Required(CONF_MEDIA_ENTITY, default=defaults.get(CONF_MEDIA_ENTITY)): selector.EntitySelector(
            selector.EntitySelectorConfig(domain="media_player")
        ),
    }
    for slot in range(1, MEDIA_FAVORITE_COUNT + 1):
        fields[vol.Optional(favorite_label_key(slot), default=defaults.get(favorite_label_key(slot), ""))] = str
        fields[vol.Optional(favorite_icon_key(slot), default=defaults.get(favorite_icon_key(slot), "radio"))] = selector.SelectSelector(
            selector.SelectSelectorConfig(options=_FAVORITE_ICONS)
        )
        fields[vol.Optional(favorite_payload_key(slot), default=defaults.get(favorite_payload_key(slot), ""))] = str
    return vol.Schema(fields)


class WallDisplayConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        if user_input is not None:
            await self.async_set_unique_id(user_input[CONF_PANEL_TOPIC].rstrip("/"))
            self._abort_if_unique_id_configured()
            return self.async_create_entry(title=user_input[CONF_PANEL_TOPIC], data=user_input)
        return self.async_show_form(step_id="user", data_schema=_schema())

    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return WallDisplayOptionsFlow(config_entry)


class WallDisplayOptionsFlow(config_entries.OptionsFlow):
    def __init__(self, config_entry) -> None:
        self.config_entry = config_entry

    async def async_step_init(self, user_input=None):
        if user_input is not None:
            return self.async_create_entry(title="", data=user_input)
        defaults = {**self.config_entry.data, **self.config_entry.options}
        return self.async_show_form(step_id="init", data_schema=_schema(defaults))
