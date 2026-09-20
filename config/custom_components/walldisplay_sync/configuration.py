"""Shared configuration schema and migrations for forms, YAML and runtime."""
from __future__ import annotations

import voluptuous as vol
from homeassistant.helpers import selector

from .pages import DEFAULT_PAGE_SLOTS, GRID_COUNT, MAX_PAGE_SLOTS, PAGE_NAMES, layout_payload

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
    CONF_PANEL_HOSTNAME,
    CONF_PANEL_TOPIC,
    CONF_SCREEN_OFF_AFTER,
    CONF_TIME_FORMAT,
    CONF_UPDATE_API_URL,
    DEFAULT_UPDATE_API_URL,
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


def _panel_hostname(value: object) -> str:
    """Validate the panel name as the ESP32's single-label hostname."""
    hostname = str(value).strip().lower()
    if not hostname or len(hostname) > 32 or not hostname.isascii():
        raise vol.Invalid("Panel hostname must be 1-32 ASCII characters")
    if hostname[0] == "-" or hostname[-1] == "-":
        raise vol.Invalid("Panel hostname cannot start or end with a hyphen")
    if any(char not in "abcdefghijklmnopqrstuvwxyz0123456789-" for char in hostname):
        raise vol.Invalid("Panel hostname may contain only letters, numbers, and hyphens")
    return hostname


def _hostname_from_display_name(value: object) -> str:
    """Generate the station hostname while preserving the human label."""
    text = str(value).strip().lower()
    if not text or len(text.encode("utf-8")) > 64 or any(ord(char) < 0x20 or ord(char) == 0x7f for char in text):
        raise vol.Invalid("Display name must be 1-64 printable UTF-8 bytes")
    candidate = "".join(char if char.isascii() and char.isalnum() else "-" for char in text)
    while "--" in candidate:
        candidate = candidate.replace("--", "-")
    candidate = candidate[:32].strip("-")
    if not candidate:
        raise vol.Invalid("Display name must contain at least one letter or number")
    return candidate


def _hostname_from_topic(value: object) -> str:
    """Create a safe migration fallback for older human-readable names."""
    topic = str(value).strip().rstrip("/")
    candidate = topic.rsplit("/", 1)[-1].lower()
    candidate = "".join(char if char.isascii() and (char.isalnum() or char == "-") else "-" for char in candidate)
    candidate = candidate[:32].strip("-")
    return candidate or "walldisplay"


def _update_api_url(value):
    value = str(value).strip()
    if value and (not value.startswith("https://") or len(value) > 512):
        raise vol.Invalid("Update API URL must be an HTTPS URL of at most 512 characters")
    return value


def _form_schema(fields):
    """Optional entity selectors must omit empty defaults to remain valid in HA."""
    return vol.Schema({
        vol.Optional(key.schema) if isinstance(validator, selector.EntitySelector) and
        key.default is not vol.UNDEFINED and key.default() == "" else key: validator
        for key, validator in fields.items()
    })


def _merge_form(data, user_input, schema):
    for key, validator in schema.schema.items():
        if isinstance(validator, selector.EntitySelector) and key.schema not in user_input:
            data[key.schema] = ""
    data.update(user_input)


def _basic_schema(defaults: dict[str, object]) -> vol.Schema:
    return _form_schema({
        vol.Required(CONF_PANEL_TOPIC, default=defaults.get(CONF_PANEL_TOPIC, "")): str,
        vol.Optional(CONF_PANEL_NAME, default=defaults.get(CONF_PANEL_NAME, "WallDisplay")): str,
        vol.Optional(CONF_MEDIA_ENTITY, default=defaults.get(CONF_MEDIA_ENTITY, "")): selector.EntitySelector(
            selector.EntitySelectorConfig(domain="media_player")
        ),
        vol.Optional(CONF_MEDIA_POWER_SWITCH, default=defaults.get(CONF_MEDIA_POWER_SWITCH, "")): selector.EntitySelector(
            selector.EntitySelectorConfig(domain="switch")
        ),
    })


def _updates_schema(defaults: dict[str, object]) -> vol.Schema:
    return _form_schema({
        vol.Optional(CONF_UPDATE_API_URL, default=defaults.get(CONF_UPDATE_API_URL, DEFAULT_UPDATE_API_URL)): vol.All(str, _update_api_url),
    })


def _updates_form_schema(defaults: dict[str, object]) -> vol.Schema:
    """Return the UI schema without a custom validator.

    Home Assistant serializes config-flow schemas to send them to the
    frontend. Arbitrary callables inside ``vol.All`` cannot be serialized, so
    validate the submitted value in the flow and keep the frontend schema to a
    standard string field.
    """
    return _form_schema({
        vol.Optional(CONF_UPDATE_API_URL, default=defaults.get(CONF_UPDATE_API_URL, DEFAULT_UPDATE_API_URL)): str,
    })


def _display_schema(defaults: dict[str, object]) -> vol.Schema:
    return _form_schema({
        vol.Optional("screenshots_enabled", default=defaults.get("screenshots_enabled", False)): bool,
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
    return _form_schema(fields)


def _count_schema(key: str, maximum: int, count: int) -> vol.Schema:
    return _form_schema({
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
    return _form_schema(fields)


def _footer_schema(defaults: dict[str, object], count: int) -> vol.Schema:
    fields: dict[vol.Marker, object] = {}
    for slot in range(1, count + 1):
        fields[vol.Optional(footer_label_key(slot), default=defaults.get(footer_label_key(slot), ""))] = str
        fields[vol.Optional(footer_state_key(slot), default=defaults.get(footer_state_key(slot), ""))] = selector.EntitySelector(
            selector.EntitySelectorConfig()
        )
    return _form_schema(fields)


def _configured_count(defaults: dict[str, object], maximum: int, key) -> int:
    return max((slot for slot in range(1, maximum + 1) if defaults.get(key(slot))), default=0)


def _clear_unselected(data: dict[str, object], maximum: int, count: int, keys: tuple) -> None:
    for slot in range(count + 1, maximum + 1):
        for key in keys:
            data[key(slot)] = 0.0 if key in (chip_warn_key, chip_alert_key) else ""


def _pages_schema(defaults):
    fields = {}
    for slot, initial in enumerate(DEFAULT_PAGE_SLOTS, 1):
        key = f"page{slot}"
        fields[vol.Optional(key, default=defaults.get(key, initial))] = selector.SelectSelector(
            selector.SelectSelectorConfig(options=[*PAGE_NAMES, "none"]))
    fields[vol.Optional("default_page", default=defaults.get("default_page", "weather"))] = selector.SelectSelector(
        selector.SelectSelectorConfig(options=list(PAGE_NAMES)))
    for page in PAGE_NAMES:
        fields[vol.Optional(f"{page}_title", default=defaults.get(f"{page}_title", page.title()))] = str
    return _form_schema(fields)


def _grid_schema(defaults):
    fields = {}
    for slot in range(1, GRID_COUNT + 1):
        key = f"grid{slot}"
        fields[vol.Optional(f"{key}_label", default=defaults.get(f"{key}_label", ""))] = str
        fields[vol.Optional(f"{key}_state_entity", default=defaults.get(f"{key}_state_entity", ""))] = selector.EntitySelector(
            selector.EntitySelectorConfig(domain=["light", "switch", "input_boolean", "fan"]))
    return _form_schema(fields)


def _complete_config(value):
    if not isinstance(value, dict):
        raise vol.Invalid("Configuration must be a mapping")
    # `panel_hostname` is derived from the human display name, but accept it
    # in persisted/configuration payloads so older entries and YAML round trips
    # remain loadable. It is normalized below and never trusted as input.
    schema = vol.Schema({vol.Optional("schema_version", default=2): vol.In([2]),
                         vol.Optional(CONF_PANEL_HOSTNAME, default=""): str})
    for part in (_basic_schema({}), _updates_schema({}), _display_schema({}), _favorites_schema({}),
                 _chip_schema({}, CHIP_COUNT), _footer_schema({}, FOOTER_BUTTON_COUNT),
                 _pages_schema({}), _grid_schema({})):
        schema = schema.extend({key: vol.Any("", validator) if isinstance(validator, selector.EntitySelector) else validator
                                for key, validator in part.schema.items()})
    data = schema(value)
    for key, validator in schema.schema.items():
        if isinstance(validator, vol.Any) and key.schema not in data:
            data[key.schema] = ""
    topic = data[CONF_PANEL_TOPIC].rstrip("/")
    if not topic or len(topic) > 128 or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-" for c in topic):
        raise vol.Invalid("Invalid panel topic")
    data[CONF_PANEL_TOPIC] = topic
    data[CONF_PANEL_NAME] = str(data.get(CONF_PANEL_NAME, "WallDisplay")).strip()
    if not data[CONF_PANEL_NAME] or len(data[CONF_PANEL_NAME].encode("utf-8")) > 64:
        raise vol.Invalid("Display name must be 1-64 UTF-8 bytes")
    data[CONF_PANEL_HOSTNAME] = _hostname_from_display_name(data[CONF_PANEL_NAME])
    layout_payload(data)
    for slot in range(1, GRID_COUNT + 1):
        if len(data[f"grid{slot}_label"].encode("utf-8")) > 96:
            raise vol.Invalid("Button labels must be at most 96 UTF-8 bytes")
    return data


def migrate_configuration(value):
    """Explicit v1 → v2 migration; future schemas fail instead of being guessed."""
    data = dict(value)
    version = data.get("schema_version", 1)
    if version == 1:
        data["schema_version"] = 2
    elif version != 2:
        raise vol.Invalid("Unsupported configuration schema version")
    if data.get(CONF_PANEL_NAME):
        try:
            data[CONF_PANEL_HOSTNAME] = _hostname_from_display_name(data[CONF_PANEL_NAME])
        except vol.Invalid:
            # Versions before hostname semantics allowed labels such as
            # "Living room". Keep those entries loadable while steering the
            # saved value to the panel topic's stable hostname.
            data[CONF_PANEL_NAME] = _hostname_from_topic(data.get(CONF_PANEL_TOPIC, ""))
            data[CONF_PANEL_HOSTNAME] = _hostname_from_display_name(data[CONF_PANEL_NAME])
    return _complete_config(data)
