from __future__ import annotations

import voluptuous as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import selector

from .const import (
    CONF_MEDIA_ENTITY,
    CONF_PANEL_NAME,
    CONF_PANEL_TOPIC,
    CONF_DIM_AFTER,
    CONF_SCREEN_OFF_AFTER,
    CONF_DIM_BRIGHTNESS,
    CONF_CHIP_OK_COLOR,
    CONF_CHIP_WARN_COLOR,
    CONF_CHIP_ALERT_COLOR,
    CONF_CHIP_NEUTRAL_COLOR,
    CONF_WEATHER_ENTITY,
    DOMAIN,
    CHIP_COUNT,
    chip_sensor_key, chip_warn_key, chip_alert_key,
    FOOTER_BUTTON_COUNT, footer_label_key, footer_state_key,
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
        vol.Optional(CONF_PANEL_NAME, default=defaults.get(CONF_PANEL_NAME, "")): str,
        vol.Required(CONF_MEDIA_ENTITY, default=defaults.get(CONF_MEDIA_ENTITY)): selector.EntitySelector(
            selector.EntitySelectorConfig(domain="media_player")
        ),
        vol.Optional(CONF_WEATHER_ENTITY, default=defaults.get(CONF_WEATHER_ENTITY, "")): str,
        vol.Optional(CONF_DIM_AFTER, default=defaults.get(CONF_DIM_AFTER, 300)): vol.All(vol.Coerce(int), vol.Range(min=0, max=86400)),
        vol.Optional(CONF_SCREEN_OFF_AFTER, default=defaults.get(CONF_SCREEN_OFF_AFTER, 600)): vol.All(vol.Coerce(int), vol.Range(min=0, max=86400)),
        vol.Optional(CONF_DIM_BRIGHTNESS, default=defaults.get(CONF_DIM_BRIGHTNESS, 20)): vol.All(vol.Coerce(int), vol.Range(min=0, max=100)),
        vol.Optional(CONF_CHIP_OK_COLOR, default=defaults.get(CONF_CHIP_OK_COLOR, "ok")): str,
        vol.Optional(CONF_CHIP_WARN_COLOR, default=defaults.get(CONF_CHIP_WARN_COLOR, "warn")): str,
        vol.Optional(CONF_CHIP_ALERT_COLOR, default=defaults.get(CONF_CHIP_ALERT_COLOR, "alert")): str,
        vol.Optional(CONF_CHIP_NEUTRAL_COLOR, default=defaults.get(CONF_CHIP_NEUTRAL_COLOR, "neutral")): str,
    }
    for slot in range(1, MEDIA_FAVORITE_COUNT + 1):
        fields[vol.Optional(favorite_label_key(slot), default=defaults.get(favorite_label_key(slot), ""))] = str
        fields[vol.Optional(favorite_icon_key(slot), default=defaults.get(favorite_icon_key(slot), "radio"))] = selector.SelectSelector(
            selector.SelectSelectorConfig(options=_FAVORITE_ICONS)
        )
        fields[vol.Optional(favorite_payload_key(slot), default=defaults.get(favorite_payload_key(slot), ""))] = str
    for slot in range(1, CHIP_COUNT + 1):
        fields[vol.Optional(chip_sensor_key(slot), default=defaults.get(chip_sensor_key(slot), ""))] = str
        fields[vol.Optional(chip_warn_key(slot), default=defaults.get(chip_warn_key(slot), 0.0))] = vol.Coerce(float)
        fields[vol.Optional(chip_alert_key(slot), default=defaults.get(chip_alert_key(slot), 0.0))] = vol.Coerce(float)
    for slot in range(1, FOOTER_BUTTON_COUNT + 1):
        fields[vol.Optional(footer_label_key(slot), default=defaults.get(footer_label_key(slot), ""))] = str
        fields[vol.Optional(footer_state_key(slot), default=defaults.get(footer_state_key(slot), ""))] = str
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
