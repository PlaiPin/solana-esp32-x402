idf_component_register(
    SRCS "solana_wallet.c"
    INCLUDE_DIRS "."
    REQUIRES "solana_tx" "solana_rpc"
    PRIV_REQUIRES "tweetnacl" "base58" "esp_http_client" "cjson"
)

