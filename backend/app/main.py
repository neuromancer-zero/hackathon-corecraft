from __future__ import annotations

import asyncio
import sys
from contextlib import asynccontextmanager

from fastapi import FastAPI
from loguru import logger

from app.config import settings
from app.routers import api, ws
from app.services.zmq_listener import start_background_tasks

logger.remove()
logger.add(
    sys.stderr,
    level=settings.log_level.upper(),
    serialize=False,
    format="<green>{time:YYYY-MM-DD HH:mm:ss}</green> | <level>{level: <8}</level> | {message}",
)


_bg_tasks: list[asyncio.Task] = []


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _bg_tasks
    _bg_tasks = start_background_tasks()
    yield
    for t in _bg_tasks:
        t.cancel()
    await asyncio.gather(*_bg_tasks, return_exceptions=True)


app = FastAPI(title="BTC ESP Backend", lifespan=lifespan)
app.include_router(api.router)
app.include_router(ws.router)


@app.get("/")
async def root() -> dict:
    return {"service": "btc-esp-backend", "docs": "/docs"}
