idf_component_register(
    SRCS "x402_encoding.c"
         "x402_requirements.c"
         "x402_payment.c"
         "x402_client.c"
    INCLUDE_DIRS "."
    REQUIRES "solana_wallet" "spl_token" "solana_rpc" "esp_http_client"
    PRIV_REQUIRES "mbedtls" "base58" "tweetnacl" "espressif__cjson"
)

