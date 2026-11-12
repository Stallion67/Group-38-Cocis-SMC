idf_component_register(SRCS "runtime.c" INCLUDE_DIRS "../../include"
    REQUIRES common fatfs sdmmc nvs_flash esp_timer esp_driver_spi esp_driver_sdspi esp_driver_gpio)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
