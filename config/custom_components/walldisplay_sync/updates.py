"""Find signed WallDisplay firmware releases."""
from __future__ import annotations

from dataclasses import dataclass
import logging
import re
from typing import Any

from aiohttp import ClientError

from homeassistant.helpers.aiohttp_client import async_get_clientsession

_LOGGER = logging.getLogger(__name__)
_SEMVER_RE = re.compile(
    r"^[v]?([0-9]+)\.([0-9]+)\.([0-9]+)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)


@dataclass(frozen=True)
class FirmwareRelease:
    """A release manifest that can be sent to a panel."""

    version: str
    manifest_url: str


def _version(value: Any) -> tuple[int, int, int, int, tuple[tuple[int, int | str], ...]] | None:
    """Parse a SemVer value into a directly comparable key.

    Home Assistant's AwesomeVersion treats multi-part prereleases such as
    ``1.0.0-rc.14.1`` as incomparable with ``1.0.0-rc.16``. Forgejo tags
    generated for test builds can contain that form, so compare the SemVer
    identifiers explicitly here.
    """
    if not isinstance(value, str):
        return None
    value = value.strip()
    match = _SEMVER_RE.fullmatch(value)
    if match is None:
        return None
    major, minor, patch, prerelease = match.groups()
    identifiers: tuple[tuple[int, int | str], ...] = ()
    if prerelease is not None:
        parsed_identifiers: list[tuple[int, int | str]] = []
        for identifier in prerelease.split("."):
            if identifier.isdigit():
                if len(identifier) > 1 and identifier.startswith("0"):
                    return None
                parsed_identifiers.append((0, int(identifier)))
            else:
                parsed_identifiers.append((1, identifier))
        identifiers = tuple(parsed_identifiers)
    # Stable releases sort after prereleases. The identifier tuples use a
    # numeric/string discriminator so Python never compares unlike types.
    return int(major), int(minor), int(patch), int(prerelease is None), identifiers


def _release_candidate(release: Any) -> FirmwareRelease | None:
    if not isinstance(release, dict) or release.get("draft"):
        return None
    tag = release.get("tag_name")
    parsed = _version(tag)
    if parsed is None or not isinstance(tag, str):
        return None
    assets = release.get("assets", [])
    if not isinstance(assets, list):
        return None
    for asset in assets:
        if not isinstance(asset, dict):
            continue
        name = asset.get("name", "")
        url = asset.get("browser_download_url", "")
        if (isinstance(name, str) and name.startswith("walldisplay-") and name.endswith(".json")
                and isinstance(url, str) and url.startswith("https://")):
            return FirmwareRelease(tag.removeprefix("v"), url)
    return None


async def async_latest_release(hass, api_url: str) -> FirmwareRelease | None:
    """Return the highest signed release exposed by the configured API."""
    if not api_url or not api_url.startswith("https://"):
        return None
    session = async_get_clientsession(hass)
    try:
        async with session.get(api_url, timeout=15, headers={"Accept": "application/json"}) as response:
            if response.status != 200:
                _LOGGER.debug("Firmware release API returned HTTP %s", response.status)
                return None
            payload = await response.json(content_type=None)
    except (ClientError, TimeoutError, ValueError):
        _LOGGER.debug("Unable to query firmware release API", exc_info=True)
        return None

    releases = payload if isinstance(payload, list) else [payload]
    candidates = [candidate for item in releases if (candidate := _release_candidate(item)) is not None]
    if not candidates:
        return None
    return max(candidates, key=lambda item: _version(item.version) or (0, 0, 0, 0, ()))


def is_newer(current: str, release: FirmwareRelease) -> bool:
    """Compare a panel's reported version with a release version."""
    current_version = _version(current)
    release_version = _version(release.version)
    return current_version is not None and release_version is not None and release_version > current_version
