from __future__ import annotations

import voluptuous as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import selector

from .pages import GRID_COUNT, PAGE_NAMES, layout_payload

from .const import (
    CHIP_COUNT,
    CONF_CHIP_ALERT_COLOR,
    CONF_CHIP_NEUTRAL_COLOR,
    CONF_CHIP_OK_COLOR,
    CONF_CHIP_WARN_COLOR,
    CONF_DIM_AFTER,
    CONF_DIM_BRIGHTNESS,
    CONF_MEDIA_ENTITY,
    CONF_MEDIA_POWER_SWITCH,
    CONF_PANEL_NAME,
    CONF_PANEL_TOPIC,
    CONF_SCREEN_OFF_AFTER,
    CONF_TIME_FORMAT,
    CONF_UPDATE_API_URL,
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

from .configuration import (
    _basic_schema, _display_schema, _favorites_schema, _count_schema, _chip_schema,
    _footer_schema, _configured_count, _clear_unselected, _pages_schema, _grid_schema,
    _updates_form_schema, _update_api_url, _complete_config, _merge_form, migrate_configuration,
)

class WallDisplayFlowSteps:
    async def async_step_configure(self, user_input=None):
        return self.async_show_menu(
            step_id="configure",
            menu_options={
                "basic": "Panel and media player",
                "updates": "Firmware updates",
                "display": "Display and weather",
                "favorites": "Media favourites",
                "chips": "Measurement chips",
                "footer": "Footer buttons",
                "pages": "Pages and navigation",
                "grid": "Buttons page",
                "yaml": "Copy / paste YAML configuration",
                "finish": "Finish setup",
            },
        )

    async def async_step_pages(self, user_input=None):
        errors = {}
        if user_input is not None:
            try:
                layout_payload(user_input)
            except ValueError:
                errors["base"] = "invalid_pages"
            else:
                self._data.update(user_input)
                return await self.async_step_configure()
        return self.async_show_form(step_id="pages", data_schema=_pages_schema(user_input or self._data), errors=errors)

    async def async_step_grid(self, user_input=None):
        errors = {}
        if user_input is not None:
            if any(len(user_input.get(f"grid{i}_label", "").encode("utf-8")) > 96 for i in range(1, GRID_COUNT + 1)):
                errors["base"] = "invalid_config"
            else:
                _merge_form(self._data, user_input, _grid_schema(self._data))
                return await self.async_step_configure()
        return self.async_show_form(step_id="grid", data_schema=_grid_schema(user_input or self._data), errors=errors)

    async def async_step_yaml(self, user_input=None):
        errors = {}
        if not hasattr(self, "_data"):
            self._data = {}
        if user_input is not None:
            try:
                data = migrate_configuration(user_input["configuration"])
                if isinstance(self, config_entries.OptionsFlow) and data[CONF_PANEL_TOPIC] != self.config_entry.data[CONF_PANEL_TOPIC].rstrip("/"):
                    raise ValueError("Changing panel identity requires a new entry")
            except (vol.Invalid, ValueError, TypeError):
                errors["base"] = "invalid_config"
            else:
                if isinstance(self, config_entries.ConfigFlow):
                    await self.async_set_unique_id(data[CONF_PANEL_TOPIC])
                    self._abort_if_unique_id_configured()
                self._data = data
                return await self.async_step_configure()
        value = user_input["configuration"] if user_input else self._data
        if not user_input and self._data.get(CONF_PANEL_TOPIC):
            try:
                value = _complete_config(self._data)
            except (vol.Invalid, ValueError, TypeError):
                pass
        return self.async_show_form(step_id="yaml", data_schema=vol.Schema({
            vol.Required("configuration", default=value): selector.ObjectSelector(),
        }), errors=errors)

    async def async_step_basic(self, user_input=None):
        if user_input is not None:
            _merge_form(self._data, user_input, _basic_schema(self._data))
            return await self.async_step_configure()
        return self.async_show_form(step_id="basic", data_schema=_basic_schema(self._data))

    async def async_step_updates(self, user_input=None):
        errors = {}
        if user_input is not None:
            try:
                self._data[CONF_UPDATE_API_URL] = _update_api_url(user_input.get(CONF_UPDATE_API_URL, ""))
            except vol.Invalid:
                errors[CONF_UPDATE_API_URL] = "invalid_update_api_url"
            else:
                return await self.async_step_configure()
        return self.async_show_form(step_id="updates", data_schema=_updates_form_schema(self._data), errors=errors)

    async def async_step_display(self, user_input=None):
        if user_input is not None:
            _merge_form(self._data, user_input, _display_schema(self._data))
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
            _merge_form(self._data, user_input, _chip_schema(self._data, self._chip_count))
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
            _merge_form(self._data, user_input, _footer_schema(self._data, self._footer_count))
            _clear_unselected(self._data, FOOTER_BUTTON_COUNT, self._footer_count, (footer_label_key, footer_state_key))
            return await self.async_step_configure()
        return self.async_show_form(step_id="footer_fields", data_schema=_footer_schema(self._data, self._footer_count))


class WallDisplayConfigFlow(WallDisplayFlowSteps, config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 2

    async def async_step_user(self, user_input=None):
        self._data = {}
        return self.async_show_menu(step_id="user", menu_options=["guided", "yaml"])

    async def async_step_guided(self, user_input=None):
        if user_input is not None:
            await self.async_set_unique_id(user_input[CONF_PANEL_TOPIC].rstrip("/"))
            self._abort_if_unique_id_configured()
            self._data = user_input
            return await self.async_step_configure()
        return self.async_show_form(step_id="guided", data_schema=_basic_schema({}))

    async def async_step_finish(self, user_input=None):
        try:
            self._data = _complete_config(self._data)
        except (vol.Invalid, ValueError, TypeError):
            return await self.async_step_yaml({"configuration": self._data})
        await self.async_set_unique_id(self._data[CONF_PANEL_TOPIC])
        self._abort_if_unique_id_configured()
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
        try:
            self._data = _complete_config(self._data)
            if self._data[CONF_PANEL_TOPIC] != self.config_entry.data[CONF_PANEL_TOPIC].rstrip("/"):
                raise ValueError("Changing panel identity requires a new entry")
        except (vol.Invalid, ValueError, TypeError):
            return await self.async_step_yaml({"configuration": self._data})
        return self.async_create_entry(title="", data=self._data)
