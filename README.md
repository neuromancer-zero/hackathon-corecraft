# hackathon-corecraft

ESP8266 NodeMCU firmware (PlatformIO / Arduino) + FastAPI backend that proxies **Bitcoin Core JSON-RPC** and **ZMQ** for lightweight IoT clients.

## Architecture

- **`firmware/`**: polls `GET /api/price` and `GET /api/block/tip`, shows alternating values on a **MAX7219 8-digit** display, hosts a tiny **HTTP** UI (`/` + `/track` + `/status`), and opens a **WebSocket** to the backend to follow a tx until confirmed.
- **`backend/`**: FastAPI app exposing REST + `WS /ws/tx/{txid}`; talks to `bitcoind` over RPC and subscribes to ZMQ `hashblock` / `hashtx`. CoinGecko provides BTC/USD with a short cache.

## Prerequisites

- Docker + Docker Compose (for `bitcoind` + backend)
- Python 3.11+ (optional local backend without Docker)
- PlatformIO (`pip install platformio`) for building/flashing the ESP8266

## Backend (FastAPI)

```bash
cd backend
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env   # edit RPC/ZMQ URLs if needed
python3 -m uvicorn app.main:app --reload --host 0.0.0.0 --port 3000
```

JSON-RPC uses **async `httpx`** (instead of `python-bitcoinrpc`) so WebSocket + ZMQ fan-out never block the event loop.

### API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/price` | `{"usd": float, "ts": unix}` (cached `PRICE_CACHE_SECONDS`) |
| GET | `/api/block/tip` | `{"height": int}` via RPC `getblockcount`, mempool.space fallback |
| GET | `/api/health` | `{"rpc": bool, "zmq": bool}` |
| WS | `/ws/tx/{txid}` | JSON stream: `accepted`, `mempool`, `confirmed`, `done`, `error` |

## Docker Compose (bitcoind testnet + backend)

```bash
docker compose up --build
```

- **bitcoind** (testnet): RPC `18332`, ZMQ `28332` (`hashblock`) and `28333` (`hashtx`). Default credentials `user` / `pass` (change for anything beyond local dev).
- **backend**: `http://localhost:8000`

First testnet sync can take a long time; the `/api/block/tip` endpoint falls back to mempool.space if RPC is unavailable.

## Firmware (ESP8266)

### Wiring (MAX7219 “FC-16” style 8 digits)

| MAX7219 | NodeMCU |
|---------|---------|
| DIN | D7 / GPIO13 (HW MOSI) |
| CLK | D5 / GPIO14 (HW SCK) |
| CS | D8 / GPIO15 |
| VCC | 5V (or 3.3 V if your module supports it) |
| GND | GND |

### Configure WiFi / backend

```bash
cd firmware
cp include/secrets.h.example include/secrets.h
# edit WIFI_SSID, WIFI_PASS, BACKEND_HOST (LAN IP of the machine running the backend)
```

Optional: override `BACKEND_HTTP_PORT` / `BACKEND_WS_PORT` in `include/config.h` (defaults `8000`).

### Build & flash

```bash
cd firmware
pio run -e nodemcuv2
pio run -t upload
```

Open `http://<esp-ip>/` in a browser, paste a **64-hex character** testnet txid, submit **Acompanhar**. Status is polled from `/status` every 2 seconds. The display shows `tr` + last 4 hex nibbles of the txid + confirmations; on `done` it blinks **`donE !!`** for ~5 seconds.

### Obtaining a testnet txid

1. Ensure `bitcoind` is synced enough to broadcast/receive on testnet (or use a public faucet + explorer).
2. Create/receive a transaction on **testnet** and copy its **txid** (64 hex chars) from your wallet or a block explorer.
3. Paste into the ESP-hosted page; the backend follows it until `TX_CONFIRMATIONS_TARGET` (default **1** confirmation).

## Development notes

- **Secrets**: never commit real WiFi or RPC credentials. `firmware/include/secrets.h` ships with placeholders so `pio run` works; replace locally.
- **WebSocket library** on the ESP uses `ws://` — point `BACKEND_HOST` at a host reachable on the LAN; for TLS you’d need `wss://` + cert handling (not included).
