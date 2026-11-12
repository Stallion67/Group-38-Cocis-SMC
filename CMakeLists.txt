cmake_minimum_required(VERSION 3.16.0)
# PlatformIO (platformio.ini) builds src/ as the application component.
# Plain ESP-IDF (idf.py build) has no PlatformIO, so add src/ by hand.
# components/ is picked up automatically in both cases.
if(NOT DEFINED ENV{PLATFORMIO_CORE_DIR} AND NOT PLATFORMIO)
    list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/src)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cocis_scms)
