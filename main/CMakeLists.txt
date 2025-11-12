idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "nvs_flash" "tweetnacl" "base58" "wifi_manager" "solana_rpc" "solana_tx" "solana_wallet" "spl_token" "x402_protocol"
)

