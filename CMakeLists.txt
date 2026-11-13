cmake_minimum_required(VERSION 3.16)
# Separate translated ports from lwIP's own TCP/UDP ephemeral range (49152+).
add_compile_definitions(IP_NAPT_PORT_RANGE_START=32768 IP_NAPT_PORT_RANGE_END=49151)
# v3.0 SPIKE: LittleFS now comes from the managed joltwallet/littlefs component.
# TinyUSB for the USB modem port (v2.0); IDF's own tinyusb component stays off.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/.pio/network-deps/arduino_tinyusb")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(usr_modem)
