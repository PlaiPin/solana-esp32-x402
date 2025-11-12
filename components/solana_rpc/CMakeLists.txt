idf_component_register(
    SRCS "solana_rpc.c"
    INCLUDE_DIRS "."
    REQUIRES "esp_http_client"
    PRIV_REQUIRES "mbedtls"
)

