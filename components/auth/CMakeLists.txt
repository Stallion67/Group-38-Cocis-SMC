idf_component_register(SRCS "session.c" "runtime.c" INCLUDE_DIRS "../../include"
    REQUIRES common mbedtls nvs_flash)
