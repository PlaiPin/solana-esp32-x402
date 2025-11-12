idf_component_register(
    SRCS "tweetnacl.c" "tweetnacl_esp32.c"
    INCLUDE_DIRS "."
)

# TweetNaCl is external code with some string initialization patterns
# that trigger warnings in newer GCC. Disable specific warnings for this component.
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-error=unterminated-string-initialization
    -Wno-unterminated-string-initialization
)

