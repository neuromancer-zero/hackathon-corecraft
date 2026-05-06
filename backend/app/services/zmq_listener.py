"""Background ZMQ subscriber for bitcoind hashblock / hashtx."""

from __future__ import annotations

import asyncio

import zmq.asyncio
from loguru import logger

from app.config import settings
from app.services.tx_tracker import hub

_zmq_task: asyncio.Task | None = None


async def _run_zmq() -> None:
    ctx = zmq.asyncio.Context.instance()
    sock = ctx.socket(zmq.SUB)
    try:
        sock.setsockopt(zmq.RCVTIMEO, 5000)
        sock.connect(settings.bitcoind_zmq_hashblock)
        sock.subscribe(b"hashblock")
    except Exception as e:
        logger.error("ZMQ connect failed (hashblock endpoint): {}", e)
        hub.set_zmq_connected(False)
        return

    sock2 = ctx.socket(zmq.SUB)
    try:
        sock2.connect(settings.bitcoind_zmq_hashtx)
        sock2.subscribe(b"hashtx")
    except Exception as e:
        logger.warning("ZMQ second socket (hashtx) failed: {}", e)
        sock2.close()

    hub.set_zmq_connected(True)
    logger.info("ZMQ subscriber connected")

    poller = zmq.asyncio.Poller()
    poller.register(sock, zmq.POLLIN)
    if not sock2.closed:
        poller.register(sock2, zmq.POLLIN)

    try:
        while True:
            try:
                events = dict(await poller.poll(timeout=1000))
            except asyncio.CancelledError:
                raise
            except Exception as e:
                logger.warning("ZMQ poll error: {}", e)
                hub.set_zmq_connected(False)
                await asyncio.sleep(1)
                continue

            hub.set_zmq_connected(True)

            if sock in events and events[sock] == zmq.POLLIN:
                try:
                    parts = await sock.recv_multipart()
                except Exception as e:
                    logger.debug("ZMQ recv hashblock: {}", e)
                    continue
                await _handle_multipart(parts, is_hashtx=False)

            if not sock2.closed and sock2 in events and events[sock2] == zmq.POLLIN:
                try:
                    parts = await sock2.recv_multipart()
                except Exception as e:
                    logger.debug("ZMQ recv hashtx: {}", e)
                    continue
                await _handle_multipart(parts, is_hashtx=True)
    finally:
        sock.close(linger=0)
        if not sock2.closed:
            sock2.close(linger=0)
        hub.set_zmq_connected(False)


def _extract_body_32(parts: list[bytes]) -> bytes | None:
    """Bitcoin ZMQ may send [topic, seq?, body32]. Use last 32-byte chunk."""
    for p in reversed(parts):
        if len(p) == 32:
            return p
    return None


async def _handle_multipart(parts: list[bytes], *, is_hashtx: bool) -> None:
    if not parts:
        return
    topic = parts[0]
    body = _extract_body_32(parts)
    if body is None:
        return
    if topic == b"hashblock":
        await hub.on_hashblock(body)
    elif topic == b"hashtx" or is_hashtx:
        await hub.on_hashtx(body)


async def zmq_loop_with_fallback() -> None:
    """Reconnect forever if bitcoind or ZMQ is temporarily unavailable."""
    backoff = 1.0
    while True:
        try:
            await _run_zmq()
        except asyncio.CancelledError:
            raise
        except Exception as e:
            logger.warning("ZMQ runner crashed: {}", e)
        hub.set_zmq_connected(False)
        await asyncio.sleep(backoff)
        backoff = min(backoff * 2, 30.0)


async def prune_loop() -> None:
    while True:
        await asyncio.sleep(60)
        await hub.prune_stale_sessions(max_age_s=1800.0)


def start_background_tasks() -> list[asyncio.Task]:
    global _zmq_task
    tasks: list[asyncio.Task] = []
    tasks.append(asyncio.create_task(zmq_loop_with_fallback(), name="zmq"))
    tasks.append(asyncio.create_task(prune_loop(), name="prune"))
    return tasks
