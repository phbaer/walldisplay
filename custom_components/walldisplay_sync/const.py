DOMAIN = "walldisplay_sync"
CONF_PANEL_TOPIC = "panel_topic"
CONF_PANEL_NAME = "panel_name"
CONF_TIME_FORMAT = "time_format"
CONF_MEDIA_ENTITY = "media_entity"
CONF_MEDIA_POWER_SWITCH = "media_power_switch"
CONF_WEATHER_ENTITY = "weather_entity"
CONF_TEMPERATURE_ENTITY = "temperature_entity"
CONF_HUMIDITY_ENTITY = "humidity_entity"
CONF_PRESSURE_ENTITY = "pressure_entity"
CONF_WIND_SPEED_ENTITY = "wind_speed_entity"
CONF_RAINFALL_ENTITY = "rainfall_entity"
CONF_IRRADIANCE_ENTITY = "irradiance_entity"
CONF_DIM_AFTER = "dim_after"
CONF_SCREEN_OFF_AFTER = "screen_off_after"
CONF_DIM_BRIGHTNESS = "dim_brightness"
CONF_CHIP_OK_COLOR = "chip_ok_color"
CONF_CHIP_WARN_COLOR = "chip_warn_color"
CONF_CHIP_ALERT_COLOR = "chip_alert_color"
CONF_CHIP_NEUTRAL_COLOR = "chip_neutral_color"
MEDIA_FAVORITE_COUNT = 5
FOOTER_BUTTON_COUNT = 5
CHIP_COUNT = 4
def footer_label_key(slot: int) -> str: return f"footer{slot}_label"
def footer_state_key(slot: int) -> str: return f"footer{slot}_state_entity"

def chip_sensor_key(slot: int) -> str: return f"chip{slot}_sensor"
def chip_warn_key(slot: int) -> str: return f"chip{slot}_warn_above"
def chip_alert_key(slot: int) -> str: return f"chip{slot}_alert_above"


def favorite_label_key(slot: int) -> str:
    return f"favorite{slot}_label"


def favorite_icon_key(slot: int) -> str:
    return f"favorite{slot}_icon"


def favorite_payload_key(slot: int) -> str:
    return f"favorite{slot}_payload"
