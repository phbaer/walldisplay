DOMAIN = "walldisplay_sync"
CONF_PANEL_TOPIC = "panel_topic"
CONF_MEDIA_ENTITY = "media_entity"
MEDIA_FAVORITE_COUNT = 5


def favorite_label_key(slot: int) -> str:
    return f"favorite{slot}_label"


def favorite_icon_key(slot: int) -> str:
    return f"favorite{slot}_icon"


def favorite_payload_key(slot: int) -> str:
    return f"favorite{slot}_payload"
