from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8", extra="ignore")

    bitcoind_rpc_url: str = "http://user:pass@localhost:18332"
    bitcoind_zmq_hashblock: str = "tcp://127.0.0.1:28332"
    bitcoind_zmq_hashtx: str = "tcp://127.0.0.1:28333"
    price_api_url: str = (
        "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd"
    )
    price_cache_seconds: int = 30
    tx_confirmations_target: int = 1
    log_level: str = "INFO"
    mempool_tip_url: str = "https://mempool.space/api/blocks/tip/height"


settings = Settings()
