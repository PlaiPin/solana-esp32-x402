idf_component_register(
    SRCS "spl_token.c"
    INCLUDE_DIRS "."
    REQUIRES "base58" "tweetnacl"
    PRIV_REQUIRES "mbedtls"
)

