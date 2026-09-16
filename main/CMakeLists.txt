set(REQUIRED_COMPONENTS
    nvs_flash
    esp_event
    common
    pong
    modes
    uat
    traffic
    gdl90_out
    net
    web
    console_cmd
    nmea_ble
)

idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        ${REQUIRED_COMPONENTS}
)
