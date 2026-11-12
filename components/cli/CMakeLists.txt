idf_component_register(SRCS "runtime.c" INCLUDE_DIRS "../../include"
    REQUIRES common comms auth storage entrance rooms esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
