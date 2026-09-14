"""Shared page configuration and MQTT payloads, independent of Home Assistant."""
PAGE_NAMES = ("weather", "media", "buttons")
GRID_COUNT = 6


def layout_payload(config):
    pages = [config.get(f"page{slot}", default) for slot, default in enumerate(("weather", "media", "none"), 1)]
    pages = [page for page in pages if page != "none"]
    default = config.get("default_page", "weather")
    if not pages or any(page not in PAGE_NAMES for page in pages) or len(set(pages)) != len(pages):
        raise ValueError("Choose at least one page, without duplicates")
    if default not in pages:
        raise ValueError("The default page must be enabled")
    titles = {page: config.get(f"{page}_title", page.title()) for page in PAGE_NAMES}
    if any(not isinstance(title, str) or len(title.encode("utf-8")) > 48 for title in titles.values()):
        raise ValueError("Page titles must be at most 48 UTF-8 bytes")
    return {"pages": pages, "default_page": default, "titles": titles}


def grid_payload(config, slot, state):
    return {"label": config.get(f"grid{slot}_label", ""), "state": state}
