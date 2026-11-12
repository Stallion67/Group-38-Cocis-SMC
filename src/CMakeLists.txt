idf_component_register(SRCS "main.c" INCLUDE_DIRS "../include"
    REQUIRES common comms auth storage actuator rooms entrance cli nvs_flash)
