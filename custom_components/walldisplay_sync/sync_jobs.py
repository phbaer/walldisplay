"""Run independent publishers without coupling slow/failing data sources."""
import asyncio
import logging
_LOGGER = logging.getLogger(__name__)


async def publish_independently(*publishers):
    async def publish_one(publisher):
        try:
            await publisher()
        except Exception:
            _LOGGER.exception("WallDisplay synchronization failed: %s", publisher.__name__)
    await asyncio.gather(*(publish_one(publisher) for publisher in publishers))
