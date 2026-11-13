cmake_minimum_required(VERSION 3.16)
# Separate translated ports from lwIP's own TCP/UDP ephemeral range (49152+).
add_compile_definitions(IP_NAPT_PORT_RANGE_START=32768 IP_NAPT_PORT_RANGE_END=49151)
# TinyUSB for the USB modem port; IDF's own tinyusb component stays off.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/.pio/network-deps/arduino_tinyusb")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(usr_modem)

# IDF turns warnings into errors. Arduino's Esp.cpp includes hal/gpspi_flash_ll.h, whose
# initialisers leave struct members out, and C++ reports each as -Wmissing-field-initializers.
# That one warning is switched off for that one file; nothing else in the framework is touched.
idf_component_get_property(arduino_lib framework-arduinoespressif32 COMPONENT_LIB)
idf_component_get_property(arduino_dir framework-arduinoespressif32 COMPONENT_DIR)
set_source_files_properties("${arduino_dir}/cores/esp32/Esp.cpp"
    TARGET_DIRECTORY ${arduino_lib}
    PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
