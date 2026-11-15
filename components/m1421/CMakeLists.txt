idf_component_register(
    SRCS "src/m1421.c"
    INCLUDE_DIRS "include"
    REQUIRES driver common traffic uat json
    PRIV_REQUIRES esp_timer mbedtls
)
