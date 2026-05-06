from __future__ import annotations

import time
from typing import Any

import httpx
from loguru import logger

from app.config import settings

_cache: dict[str, Any] = {"usd": None, "ts": 0}


async def get_cached_price_usd() -> tuple[float, int]:
    now = int(time.time())
    if (
        _cache["usd"] is not None
        and now - int(_cache["ts"]) < settings.price_cache_seconds
    ):
        return float(_cache["usd"]), int(_cache["ts"])

    async with httpx.AsyncClient(timeout=15.0) as client:
        r = await client.get(settings.price_api_url)
        r.raise_for_status()
        data = r.json()

    usd = data["bitcoin"]["usd"]
    if not isinstance(usd, (int, float)):
        raise ValueError("invalid price payload")

    _cache["usd"] = float(usd)
    _cache["ts"] = now
    logger.debug("price refreshed: {}", _cache["usd"])
    return float(_cache["usd"]), now
