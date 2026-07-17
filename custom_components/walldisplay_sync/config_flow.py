from __future__ import annotations

import voluptuous as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import selector

from .const import (
    CHIP_COUNT,
    CONF_CHIP_ALERT_COLOR,
    CONF_CHIP_NEUTRAL_COLOR,
    CONF_CHIP_OK_COLOR,
    CONF_CHIP_WARN_COLOR,
    CONF_DIM_AFTER,
    CONF_DIM_BRIGHTNESS,
    CONF_MEDIA_ENTITY,
    CONF_PANEL_NAME,
    CONF_PANEL_TOPIC,
    CONF_SCREEN_OFF_AFTER,
    CONF_TIME_FORMAT,
    CONF_WEATHER_ENTITY,
    CONF_TEMPERATURE_ENTITY,
    CONF_HUMIDITY_ENTITY,
    CONF_PRESSURE_ENTITY,
    CONF_WIND_SPEED_ENTITY,
    CONF_RAINFALL_ENTITY,
    CONF_IRRADIANCE_ENTITY,
    DOMAIN,
    FOOTER_BUTTON_COUNT,
    MEDIA_FAVORITE_COUNT,
    chip_alert_key,
    chip_sensor_key,
    chip_warn_key,
    favorite_icon_key,
    favorite_label_key,
    favorite_payload_key,
    footer_label_key,
    footer_state_key,
)

_FAVORITE_ICONS = ["none", "radio", "music", "album", "playlist", "podcast"]


def _basic_schema(defaults: dict[str, object]) -> vol.Schema:
    return vol.Schema({
        vol.Required(CONF_PANEL_TOPIC, default=defaults.get(CONF_PANEL_TOPIC, "")): str,
        vol.Optional(CONF_PANEL_NAME, default=defaults.get(CONF_PANEL_NAME, "")): str,
        vol.Required(CONF_MEDIA_ENTITY, default=defaults.get(CONF_MEDIA_ENTITY)): selector.EntitySelector(
            selector.EntitySelectorConfig(domain="media_player")
        ),
    })


def _display_schema(defaults: dict[str, object]) -> vol.Schema:
    return vol.Schema({
        vol.Optional(CONF_WEATHER_ENTITY, default=defaults.get(CONF_WEATHER_ENTITY, "")): selector.EntitySelector(
            selector.EntitySelectorConfig(domain="weather")
        ),
        vol.Optional(CONF_TEMPERATURE_ENTITY, default=defaults.get(CONF_TEMPERATURE_ENTITY, "")): selector.EntitySelector(selector.EntitySelectorConfig(domain="sensor")),
        vol.Optional(CONF_HUMIDITY_ENTITY, default=defaults.get(CONF_HUMIDITY_ENTITY, "")): selector.EntitySelector(selector.EntitySelectorConfig(domain="sensor")),
        vol.Optional(CONF_PRESSURE_ENTITY, default=defaults.get(CONF_PRESSURE_ENTITY, "")): selector.EntitySelector(selector.EntitySelectorConfig(domain="sensor")),
        vol.Optional(CONF_WIND_SPEED_ENTITY, default=defaults.get(CONF_WIND_SPEED_ENTITY, "")): selector.EntitySelector(selector.EntitySelectorConfig(domain="sensor")),
        vol.Optional(CONF_RAINFALL_ENTITY, default=defaults.get(CONF_RAINFALL_ENTITY, "")): selector.EntitySelector(selector.EntitySelectorConfig(domain="sensor")),
        vol.Optional(CONF_IRRADIANCE_ENTITY, default=defaults.get(CONF_IRRADIANCE_ENTITY, "")): selector.EntitySelector(selector.EntitySelectorConfig(domain="sensor")),
        vol.Optional(CONF_DIM_AFTER, default=defaults.get(CONF_DIM_AFTER, 300)): vol.All(vol.Coerce(int), vol.Range(min=0, max=86400)),
        vol.Optional(CONF_SCREEN_OFF_AFTER, default=defaults.get(CONF_SCREEN_OFF_AFTER, 600)): vol.All(vol.Coerce(int), vol.Range(min=0, max=86400)),
        vol.Optional(CONF_DIM_BRIGHTNESS, default=defaults.get(CONF_DIM_BRIGHTNESS, 20)): vol.All(vol.Coerce(int), vol.Range(min=0, max=100)),
        vol.Optional(CONF_TIME_FORMAT, default=defaults.get(CONF_TIME_FORMAT, "24h")): selector.SelectSelector(
            selector.SelectSelectorConfig(options=["24h", "12h"])
        ),
    })


def _favorites_schema(defaults: dict[str, object]) -> vol.Schema:
    fields: dict[vol.Marker, object] = {}
    for slot in range(1, MEDIA_FAVORITE_COUNT + 1):
        fields[vol.Optional(favorite_label_key(slot), default=defaults.get(favorite_label_key(slot), ""))] = str
        fields[vol.Optional(favorite_icon_key(slot), default=defaults.get(favorite_icon_key(slot), "radio"))] = selector.SelectSelector(
            selector.SelectSelectorConfig(options=_FAVORITE_ICONS)
        )
        fields[vol.Optional(favorite_payload_key(slot), default=defaults.get(favorite_payload_key(slot), ""))] = str
    return vol.Schema(fields)


def _count_schema(key: str, maximum: int, count: int) -> vol.Schema:
    return vol.Schema({
        vol.Required(key, default=str(count)): selector.SelectSelector(
            selector.SelectSelectorConfig(options=[str(value) for value in range(maximum + 1)])
        )
    })


def _chip_schema(defaults: dict[str, object], count: int) -> vol.Schema:
    fields: dict[vol.Marker, object] = {
        vol.Optional(CONF_CHIP_OK_COLOR, default=defaults.get(CONF_CHIP_OK_COLOR, "ok")): str,
        vol.Optional(CONF_CHIP_WARN_COLOR, default=defaults.get(CONF_CHIP_WARN_COLOR, "warn")): str,
        vol.Optional(CONF_CHIP_ALERT_COLOR, default=defaults.get(CONF_CHIP_ALERT_COLOR, "alert")): str,
        vol.Optional(CONF_CHIP_NEUTRAL_COLOR, default=defaults.get(CONF_CHIP_NEUTRAL_COLOR, "neutral")): str,
    }
    for slot in range(1, count + 1):
        fields[vol.Optional(chip_sensor_key(slot), default=defaults.get(chip_sensor_key(slot), ""))] = selector.EntitySelector(
            selector.EntitySelectorConfig(domain="sensor")
        )
        fields[vol.Optional(chip_warn_key(slot), default=defaults.get(chip_warn_key(slot), 0.0))] = vol.Coerce(float)
        fields[vol.Optional(chip_alert_key(slot), default=defaults.get(chip_alert_key(slot), 0.0))] = vol.Coerce(float)
    return vol.Schema(fields)


def _footer_schema(defaults: dict[str, object], count: int) -> vol.Schema:
    fields: dict[vol.Marker, object] = {}
    for slot in range(1, count + 1):
        fields[vol.Optional(footer_label_key(slot), default=defaults.get(footer_label_key(slot), ""))] = str
        fields[vol.Optional(footer_state_key(slot), default=defaults.get(footer_state_key(slot), ""))] = selector.EntitySelector(
            selector.EntitySelectorConfig()
        )
    return vol.Schema(fields)


def _configured_count(defaults: dict[str, object], maximum: int, key) -> int:
    return max((slot for slot in range(1, maximum + 1) if defaults.get(key(slot))), default=0)


def _clear_unselected(data: dict[str, object], maximum: int, count: int, keys: tuple) -> None:
    for slot in range(count + 1, maximum + 1):
        for key in keys:
            data[key(slot)] = 0.0 if key in (chip_warn_key, chip_alert_key) else ""


class WallDisplayFlowSteps:
    async def async_step_configure(self, user_input=None):
        return self.async_show_menu(
            step_id="configure",
            menu_options={
                "basic": "Panel and media player",
                "display": "Display and weather",
                "favorites": "Media favourites",
                "chips": "Measurement chips",
                "footer": "Footer buttons",
                "finish": "Finish setup",
            },
        )

    async def async_step_basic(self, user_input=None):
        if user_input is not None:
            self._data.update(user_input)
            return await self.async_step_configure()
        return self.async_show_form(step_id="basic", data_schema=_basic_schema(self._data))

    async def async_step_display(self, user_input=None):
        if user_input is not None:
            self._data.update(user_input)
            return await self.async_step_configure()
        return self.async_show_form(step_id="display", data_schema=_display_schema(self._data))

    async def async_step_favorites(self, user_input=None):
        if user_input is not None:
            self._data.update(user_input)
            return await self.async_step_configure()
        return self.async_show_form(step_id="favorites", data_schema=_favorites_schema(self._data))

    async def async_step_chips(self, user_input=None):
        if user_input is not None:
            self._chip_count = int(user_input["chip_count"])
            return await self.async_step_chip_fields()
        count = _configured_count(self._data, CHIP_COUNT, chip_sensor_key)
        return self.async_show_form(step_id="chips", data_schema=_count_schema("chip_count", CHIP_COUNT, count))

    async def async_step_chip_fields(self, user_input=None):
        if user_input is not None:
            self._data.update(user_input)
            _clear_unselected(self._data, CHIP_COUNT, self._chip_count, (chip_sensor_key, chip_warn_key, chip_alert_key))
            return await self.async_step_configure()
        return self.async_show_form(step_id="chip_fields", data_schema=_chip_schema(self._data, self._chip_count))

    async def async_step_footer(self, user_input=None):
        if user_input is not None:
            self._footer_count = int(user_input["footer_count"])
            return await self.async_step_footer_fields()
        count = _configured_count(self._data, FOOTER_BUTTON_COUNT, footer_label_key)
        return self.async_show_form(step_id="footer", data_schema=_count_schema("footer_count", FOOTER_BUTTON_COUNT, count))

    async def async_step_footer_fields(self, user_input=None):
        if user_input is not None:
            self._data.update(user_input)
            _clear_unselected(self._data, FOOTER_BUTTON_COUNT, self._footer_count, (footer_label_key, footer_state_key))
            return await self.async_step_configure()
        return self.async_show_form(step_id="footer_fields", data_schema=_footer_schema(self._data, self._footer_count))


class WallDisplayConfigFlow(WallDisplayFlowSteps, config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        if user_input is not None:
            await self.async_set_unique_id(user_input[CONF_PANEL_TOPIC].rstrip("/"))
            self._abort_if_unique_id_configured()
            self._data = user_input
            return await self.async_step_configure()
        return self.async_show_form(step_id="user", data_schema=_basic_schema({}))

    async def async_step_finish(self, user_input=None):
        return self.async_create_entry(title=self._data[CONF_PANEL_TOPIC], data=self._data)

    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return WallDisplayOptionsFlow()


class WallDisplayOptionsFlow(WallDisplayFlowSteps, config_entries.OptionsFlow):
    async def async_step_init(self, user_input=None):
        self._data = {**self.config_entry.data, **self.config_entry.options}
        return await self.async_step_configure()

    async def async_step_finish(self, user_input=None):
        return self.async_create_entry(title="", data=self._data)
