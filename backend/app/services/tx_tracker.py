"""Track transactions across WebSocket clients; fan-out ZMQ + RPC updates."""

from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass, field
from typing import Any

from fastapi import WebSocket
from loguru import logger

from app.config import settings
from app.services import rpc


def zmq_hash_to_rpc_txid(body: bytes) -> str:
    """Bitcoin ZMQ sends 32-byte hash; RPC txid is the same bytes reversed as hex."""
    return body[::-1].hex()


def normalize_txid(txid: str) -> str:
    return txid.strip().lower()


def is_valid_txid(txid: str) -> bool:
    t = normalize_txid(txid)
    if len(t) != 64:
        return False
    try:
        int(t, 16)
    except ValueError:
        return False
    return True


@dataclass
class ClientSession:
    websocket: WebSocket
    txid: str
    last_confirmations: int = -1
    mempool_sent: bool = False
    created: float = field(default_factory=time.monotonic)

    def age_seconds(self) -> float:
        return time.monotonic() - self.created


class TxTrackerHub:
    """Single hub: ZMQ events trigger RPC checks for all active txids."""

    def __init__(self) -> None:
        self._lock = asyncio.Lock()
        self._sessions: dict[str, list[ClientSession]] = {}
        self._zmq_connected = False

    def set_zmq_connected(self, ok: bool) -> None:
        self._zmq_connected = ok

    @property
    def zmq_connected(self) -> bool:
        return self._zmq_connected

    async def register(self, websocket: WebSocket, txid: str) -> ClientSession:
        t = normalize_txid(txid)
        async with self._lock:
            lst = self._sessions.setdefault(t, [])
            sess = ClientSession(websocket=websocket, txid=t)
            lst.append(sess)
            return sess

    async def unregister(self, websocket: WebSocket, txid: str) -> None:
        t = normalize_txid(txid)
        async with self._lock:
            lst = self._sessions.get(t)
            if not lst:
                return
            self._sessions[t] = [s for s in lst if s.websocket is not websocket]
            if not self._sessions[t]:
                del self._sessions[t]

    async def _send_json(self, sess: ClientSession, payload: dict[str, Any]) -> None:
        try:
            await sess.websocket.send_json(payload)
        except Exception as e:
            logger.debug("send_json failed: {}", e)

    async def _broadcast_tx(self, txid: str, payload: dict[str, Any]) -> None:
        async with self._lock:
            sessions = list(self._sessions.get(txid, []))
        for s in sessions:
            await self._send_json(s, payload)

    async def on_hashtx(self, body: bytes) -> None:
        txid = zmq_hash_to_rpc_txid(body)
        async with self._lock:
            if txid not in self._sessions:
                return
            sessions = list(self._sessions[txid])
        for s in sessions:
            if not s.mempool_sent:
                s.mempool_sent = True
                await self._send_json(s, {"event": "mempool"})

    async def refresh_tx(self, txid: str) -> None:
        """After new block or on timer: poll RPC for all listeners of txid."""
        try:
            raw = await rpc.get_raw_transaction_verbose(txid)
        except rpc.BitcoinRpcError as e:
            logger.warning("refresh_tx RPC failed for {}: {}", txid, e.message)
            return

        async with self._lock:
            sessions = list(self._sessions.get(txid, []))

        if raw is None:
            for s in sessions:
                if not s.mempool_sent:
                    # still waiting — no event yet
                    pass
            return

        confs = int(raw.get("confirmations", 0))
        block_height: int | None = None
        if confs > 0 and "blockhash" in raw and raw["blockhash"]:
            try:
                # block height from verbose tx when available
                block_height = raw.get("height")
                if block_height is None:
                    bh = await rpc.rpc_call("getblockheader", [raw["blockhash"]])
                    if isinstance(bh, dict) and "height" in bh:
                        block_height = int(bh["height"])
            except Exception:
                block_height = None

        for s in sessions:
            if confs == 0 and not s.mempool_sent:
                s.mempool_sent = True
                await self._send_json(s, {"event": "mempool"})

            if confs > 0 and confs != s.last_confirmations:
                s.last_confirmations = confs
                await self._send_json(
                    s,
                    {
                        "event": "confirmed",
                        "confirmations": confs,
                        "block": block_height,
                    },
                )

            if confs >= settings.tx_confirmations_target:
                await self._send_json(
                    s,
                    {"event": "done", "confirmations": confs},
                )
                try:
                    await s.websocket.close()
                except Exception:
                    pass

    async def on_hashblock(self, _body: bytes) -> None:
        async with self._lock:
            txids = list(self._sessions.keys())
        for txid in txids:
            await self.refresh_tx(txid)

    async def prune_stale_sessions(self, max_age_s: float = 1800.0) -> None:
        async with self._lock:
            to_close: list[ClientSession] = []
            new_map: dict[str, list[ClientSession]] = {}
            for txid, lst in self._sessions.items():
                kept: list[ClientSession] = []
                for s in lst:
                    if s.age_seconds() > max_age_s:
                        to_close.append(s)
                    else:
                        kept.append(s)
                if kept:
                    new_map[txid] = kept
            self._sessions = new_map

        for s in to_close:
            await self._send_json(s, {"event": "error", "reason": "timeout"})
            try:
                await s.websocket.close()
            except Exception:
                pass


hub = TxTrackerHub()
