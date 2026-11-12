idf_component_register(SRCS "core.c" "runtime.c" INCLUDE_DIRS "../../include"
    REQUIRES common comms esp_adc esp_driver_gpio esp_driver_ledc esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
