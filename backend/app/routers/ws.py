from __future__ import annotations

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from loguru import logger

from app.config import settings
from app.services import rpc
from app.services.tx_tracker import hub, is_valid_txid, normalize_txid

router = APIRouter(tags=["websocket"])


@router.websocket("/ws/tx/{txid}")
async def websocket_tx(websocket: WebSocket, txid: str) -> None:
    await websocket.accept()
    if not is_valid_txid(txid):
        await websocket.send_json({"event": "error", "reason": "invalid txid"})
        await websocket.close()
        return

    t = normalize_txid(txid)
    sess = await hub.register(websocket, t)
    await websocket.send_json({"event": "accepted"})

    try:
        try:
            raw = await rpc.get_raw_transaction_verbose(t)
        except rpc.BitcoinRpcError as e:
            await websocket.send_json({"event": "error", "reason": f"rpc: {e.message}"})
            await websocket.close()
            return

        if raw is not None:
            confs = int(raw.get("confirmations", 0))
            if confs >= settings.tx_confirmations_target:
                await websocket.send_json({"event": "done", "confirmations": confs})
                await websocket.close()
                return
            if confs == 0:
                await websocket.send_json({"event": "mempool"})
                sess.mempool_sent = True
            else:
                block_height = raw.get("height")
                if block_height is None and raw.get("blockhash"):
                    try:
                        bh = await rpc.rpc_call("getblockheader", [raw["blockhash"]])
                        if isinstance(bh, dict) and "height" in bh:
                            block_height = int(bh["height"])
                    except Exception:
                        block_height = None
                sess.last_confirmations = confs
                await websocket.send_json(
                    {
                        "event": "confirmed",
                        "confirmations": confs,
                        "block": block_height,
                    },
                )
                if confs >= settings.tx_confirmations_target:
                    await websocket.send_json({"event": "done", "confirmations": confs})
                    await websocket.close()
                    return

        while True:
            try:
                await websocket.receive()
            except WebSocketDisconnect:
                break
    finally:
        await hub.unregister(websocket, t)
        logger.debug("ws closed for {}", t)
