# TinyUSB for the Arduino core (v2.0 USB modem port, lib/hw/src/usb_port.cpp).
#
# scripts/network_dependencies.py copies this file over the downloaded
# esp32-arduino-lib-builder components/arduino_tinyusb/CMakeLists.txt. The upstream file
# depends on a component called "arduino", which this build calls
# framework-arduinoespressif32, and builds a separate static library. This one registers
# the same sources as an ordinary component instead. The Arduino core links it when
# CONFIG_TINYUSB_ENABLED is set (its maybe_add_component(arduino_tinyusb)).
set(srcs)
if(CONFIG_TINYUSB_ENABLED)
    set(srcs
        "src/dcd_esp32sx.c"
        "tinyusb/src/class/cdc/cdc_device.c"
        "tinyusb/src/class/hid/hid_device.c"
        "tinyusb/src/class/midi/midi_device.c"
        "tinyusb/src/class/msc/msc_device.c"
        "tinyusb/src/class/video/video_device.c"
        "tinyusb/src/class/dfu/dfu_rt_device.c"
        "tinyusb/src/class/dfu/dfu_device.c"
        "tinyusb/src/class/vendor/vendor_device.c"
        "tinyusb/src/common/tusb_fifo.c"
        "tinyusb/src/device/usbd_control.c"
        "tinyusb/src/device/usbd.c"
        "tinyusb/src/tusb.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include" "tinyusb/src"
                       PRIV_INCLUDE_DIRS "tinyusb/src/device"
                       REQUIRES esp_rom freertos soc
                       PRIV_REQUIRES esp_hw_support log)

if(CONFIG_TINYUSB_ENABLED)
    idf_component_get_property(freertos_orig_include freertos ORIG_INCLUDE_PATH)
    target_include_directories(${COMPONENT_LIB} PUBLIC "${freertos_orig_include}")
    # Same options as Arduino's own build: the S2 port also drives the S3.
    target_compile_options(${COMPONENT_LIB} PRIVATE
        "-DCFG_TUSB_MCU=OPT_MCU_ESP32S2"
        "-DCFG_TUSB_DEBUG=${CONFIG_TINYUSB_DEBUG_LEVEL}"
        "-Wno-type-limits")
endif()
