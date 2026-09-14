"""Convert Home Assistant artwork to a compact JPEG for WallDisplay panels."""
from __future__ import annotations

import asyncio
import hmac
import hashlib
import time
from urllib.parse import urlsplit
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
        # Increment before awaiting: older downloads cannot overwrite newer intent.
        self._generation = getattr(self, "_generation", 0) + 1
        generation = self._generation
        if source == self.source and self.jpeg is not None and time.monotonic() < getattr(self, "_expires", 0):
            return True
        if not source:
            self.source, self.jpeg = "", None
            return False
        try:
            parsed = urlsplit(source)
            if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password:
                raise ValueError("Artwork must use an HTTP(S) URL without credentials")
            async with asyncio.timeout(15):
                async with async_get_clientsession(self.hass).get(source) as response:
                    response.raise_for_status()
                    if response.content_length is not None and response.content_length > MAX_ARTWORK_BYTES:
                        raise ValueError("Artwork response too large")
                    data = bytearray()
                    async for chunk in response.content.iter_chunked(65536):
                        if len(data) + len(chunk) > MAX_ARTWORK_BYTES:
                            raise ValueError("Artwork response too large")
                        data.extend(chunk)
            jpeg = await self.hass.async_add_executor_job(_jpeg, bytes(data))
            if generation != self._generation:
                return False
            self.source, self.jpeg = source, jpeg
            self._expires = time.monotonic() + 60
        except (ClientError, OSError, UnidentifiedImageError, ValueError, Image.DecompressionBombError, asyncio.TimeoutError) as err:
            if generation == self._generation:
                self.source, self.jpeg = "", None
            _LOGGER.debug("Unable to convert panel artwork: %s", type(err).__name__)
            return False
        return True

    def url(self, base_url: str) -> str:
        revision = hashlib.sha256(self.jpeg or b"").hexdigest()[:16]
        return f"{base_url.rstrip('/')}/api/walldisplay_sync/artwork/{self.entry_id}/{self.token}?v={revision}"


MAX_ARTWORK_BYTES = 1024 * 1024
MAX_ARTWORK_PIXELS = 4096 * 4096


def _jpeg(data: bytes) -> bytes:
    with Image.open(BytesIO(data)) as image:
        if image.width * image.height > MAX_ARTWORK_PIXELS:
            raise ValueError("Artwork dimensions too large")
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
