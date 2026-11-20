# TinyUSB for the Arduino core's USB layer (USB.begin, esp32-hal-tinyusb.c), which the USB
# modem port uses (lib/hw/src/usb_port.cpp).
#
# The Arduino core links a component of this name when CONFIG_TINYUSB_ENABLED is set
# (maybe_add_component(arduino_tinyusb) in its CMakeLists.txt). Arduino's own version lives
# in esp32-arduino-lib-builder and builds a TinyUSB checkout itself; this one takes TinyUSB
# from the component registry (idf_component.yml) and adds only the project's settings
# (Kconfig.projbuild, include/tusb_config.h).
if(NOT CONFIG_TINYUSB_ENABLED)
    idf_component_register()
    return()
endif()

idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES espressif__tinyusb)

# TinyUSB's own sources read tusb_config.h too, so they get the same include folder.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
