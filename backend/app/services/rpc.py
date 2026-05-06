"""Async JSON-RPC client for Bitcoin Core.

We use httpx instead of python-bitcoinrpc because the app is fully async (FastAPI,
WebSocket, ZMQ fan-out). python-bitcoinrpc is synchronous and would block the event loop.
"""

from __future__ import annotations

import json
from typing import Any

import httpx
from loguru import logger

from app.config import settings


class BitcoinRpcError(Exception):
    def __init__(self, code: int | None, message: str) -> None:
        self.code = code
        self.message = message
        super().__init__(message)


async def rpc_call(method: str, params: list[Any] | None = None) -> Any:
    params = params or []
    payload = {"jsonrpc": "1.0", "id": "btc-esp", "method": method, "params": params}
    try:
        async with httpx.AsyncClient(timeout=30.0) as client:
            r = await client.post(settings.bitcoind_rpc_url, json=payload)
            r.raise_for_status()
            data = r.json()
    except httpx.HTTPError as e:
        logger.warning("RPC HTTP error: {} {}", method, e)
        raise BitcoinRpcError(None, str(e)) from e

    err = data.get("error")
    if err is not None and err is not False:
        code = err.get("code") if isinstance(err, dict) else None
        msg = err.get("message", str(err)) if isinstance(err, dict) else str(err)
        logger.debug("RPC error {}: {}", method, msg)
        raise BitcoinRpcError(code, msg)

    return data.get("result")


async def get_block_count() -> int:
    result = await rpc_call("getblockcount", [])
    if not isinstance(result, int):
        raise BitcoinRpcError(None, f"unexpected getblockcount: {result!r}")
    return result


async def get_raw_transaction_verbose(txid: str) -> dict[str, Any] | None:
    """Return verbose transaction dict or None if not in chain/mempool."""
    try:
        result = await rpc_call("getrawtransaction", [txid, True])
    except BitcoinRpcError as e:
        # -5: not found
        if e.code == -5:
            return None
        raise
    if result is None:
        return None
    if not isinstance(result, dict):
        raise BitcoinRpcError(None, f"unexpected getrawtransaction: {type(result)}")
    return result


async def rpc_ping() -> bool:
    try:
        await rpc_call("getblockchaininfo", [])
        return True
    except Exception:
        return False
