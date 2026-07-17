"""Convert Home Assistant artwork to a compact JPEG for WallDisplay panels."""
from __future__ import annotations

import asyncio
import hmac
import logging
import secrets
from io import BytesIO

from aiohttp import ClientError, web
from PIL import Image, UnidentifiedImageError

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)
_DATA_CACHES = "artwork_caches"
_DATA_VIEW = "artwork_view"


class ArtworkCache:
    """Cache one normalized JPEG for one panel entry."""

    def __init__(self, hass: HomeAssistant, entry_id: str) -> None:
        self.hass = hass
        self.entry_id = entry_id
        self.token = secrets.token_urlsafe(24)
        self.source = ""
        self.jpeg: bytes | None = None

    async def async_update(self, source: str) -> bool:
        if source == self.source:
            return self.jpeg is not None
        self.source = source
        self.jpeg = None
        if not source:
            return False
        try:
            async with asyncio.timeout(15):
                async with async_get_clientsession(self.hass).get(source) as response:
                    response.raise_for_status()
                    data = await response.read()
            self.jpeg = await self.hass.async_add_executor_job(_jpeg, data)
        except (ClientError, OSError, UnidentifiedImageError, asyncio.TimeoutError) as err:
            _LOGGER.debug("Unable to convert panel artwork: %s", err)
        return self.jpeg is not None

    def url(self, base_url: str) -> str:
        return f"{base_url.rstrip('/')}/api/walldisplay_sync/artwork/{self.entry_id}/{self.token}"


def _jpeg(data: bytes) -> bytes:
    with Image.open(BytesIO(data)) as image:
        image.thumbnail((480, 480))
        output = BytesIO()
        image.convert("RGB").save(output, "JPEG", quality=85, optimize=True)
        return output.getvalue()


class ArtworkView(HomeAssistantView):
    url = "/api/walldisplay_sync/artwork/{entry_id}/{token}"
    name = "api:walldisplay_sync:artwork"
    requires_auth = False

    def __init__(self, caches: dict[str, ArtworkCache]) -> None:
        self._caches = caches

    async def get(self, request: web.Request, entry_id: str, token: str) -> web.Response:
        cache = self._caches.get(entry_id)
        if cache is None or not hmac.compare_digest(cache.token, token) or cache.jpeg is None:
            raise web.HTTPNotFound()
        return web.Response(body=cache.jpeg, content_type="image/jpeg", headers={"Cache-Control": "no-store"})


def async_register_cache(hass: HomeAssistant, cache: ArtworkCache) -> None:
    data = hass.data.setdefault(DOMAIN, {})
    caches = data.setdefault(_DATA_CACHES, {})
    caches[cache.entry_id] = cache
    if _DATA_VIEW not in data:
        data[_DATA_VIEW] = ArtworkView(caches)
        hass.http.register_view(data[_DATA_VIEW])


def async_unregister_cache(hass: HomeAssistant, entry_id: str) -> None:
    hass.data.get(DOMAIN, {}).get(_DATA_CACHES, {}).pop(entry_id, None)
