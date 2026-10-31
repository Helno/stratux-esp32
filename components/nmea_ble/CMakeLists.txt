idf_component_register(
    SRCS "src/nmea_ble.c" "src/nmea_formatter.c"
    INCLUDE_DIRS "include"
    REQUIRES "common" "traffic"
    PRIV_REQUIRES "bt"
)
