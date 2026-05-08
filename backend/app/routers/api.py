from __future__ import annotations

import time

import httpx
from fastapi import APIRouter
from pydantic import BaseModel
from typing import Optional
from loguru import logger

from app.config import settings
from app.services import rpc
from app.services.price import get_cached_price_usd
from app.services.tx_tracker import hub

router = APIRouter(prefix="/api", tags=["api"])

watched: dict[str, dict] = {}

class WatchRequest(BaseModel):
    txid: str

class TxStatus(BaseModel):
    txid: str
    status: str               # "pending" | "confirmed" | "not_found"
    confirmations: int = 0
    block_height: Optional[int] = None
    block_hash: Optional[str] = None
    block_time: Optional[int] = None

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


@router.post("/tx/watch", status_code=201)
def watch_tx(req: WatchRequest):
    """ESP registra um txid para monitorar."""
    txid = req.txid.strip()
    if len(txid) != 64:
        raise HTTPException(status_code=400, detail="txid inválido")

    watched[txid] = {
        "registered_at": datetime.utcnow().isoformat(),
        "status": "pending",
    }
    return {"message": "monitorando", "txid": txid}


@router.get("/tx/status/{txid}", response_model=TxStatus)
def tx_status(txid: str):
    """ESP consulta o status de um txid (usado no polling)."""
    txid = txid.strip()

    try:
        # verbose=True retorna detalhes completos incluindo blockhash
        tx = rpc.get_raw_transaction_verbose(txid)
    except JSONRPCException as e:
        if "No such mempool" in str(e) or "not found" in str(e).lower():
            return TxStatus(txid=txid, status="not_found")
        raise HTTPException(status_code=502, detail=f"RPC error: {e}")

    if tx is None:
        return TxStatus(txid=txid, status="not_found")
    
    logger.info(f"tx: {tx}")

    if not isinstance(tx, dict) or "confirmations" not in tx.keys():
        return TxStatus(txid=txid, status="pending", confirmations=0)

    # Busca altura do bloco
    block_height = None
    block_time = tx.get("blocktime")
    block_hash = tx.get("blockhash")

    if block_hash:
        block_info = rpc.getblockheader(block_hash)
        block_height = block_info.get("height")

    # Atualiza cache interno
    if txid in watched:
        watched[txid]["status"] = "confirmed"

    return TxStatus(
        txid=txid,
        status="confirmed",
        confirmations=confirmations,
        block_height=block_height,
        block_hash=block_hash,
        block_time=block_time,
    )


@router.get("/tx/list")
def list_watched():
    """Utilitário — lista txids sendo monitorados."""
    return watched