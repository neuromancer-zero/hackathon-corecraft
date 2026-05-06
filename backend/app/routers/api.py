from __future__ import annotations

import time

import httpx
from fastapi import APIRouter
from loguru import logger

from app.config import settings
from app.services import rpc
from app.services.price import get_cached_price_usd
from app.services.tx_tracker import hub

router = APIRouter(prefix="/api", tags=["api"])


@router.get("/price")
async def api_price() -> dict:
    usd, ts = await get_cached_price_usd()
    return {"usd": usd, "ts": ts}


@router.get("/block/tip")
async def api_block_tip() -> dict:
    try:
        height = await rpc.get_block_count()
        return {"height": height}
    except rpc.BitcoinRpcError:
        logger.warning("getblockcount failed, using mempool.space fallback")
        async with httpx.AsyncClient(timeout=15.0) as client:
            r = await client.get(settings.mempool_tip_url)
            r.raise_for_status()
            text = r.text.strip()
            return {"height": int(text)}


@router.get("/health")
async def api_health() -> dict:
    rpc_ok = await rpc.rpc_ping()
    return {"rpc": rpc_ok, "zmq": hub.zmq_connected}
