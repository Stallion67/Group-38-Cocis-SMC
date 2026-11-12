idf_component_register(
    SRCS "core.c" "kpmenu.c" "mfrc522.c" "display.c" "clock.c" "dashboard.c" "runtime.c"
    INCLUDE_DIRS "../../include"
    REQUIRES common comms auth storage actuator rooms esp_driver_gpio esp_driver_spi esp_driver_i2c
             esp_timer esp_wifi esp_netif esp_event esp_http_server lwip
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
