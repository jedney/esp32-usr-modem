"""GPIO rules for the ESP32-S3 N16R8 board, used by pin_config.py and its tests."""

MAX_GPIO = 48  # highest ESP32-S3 GPIO
ABSENT = range(22, 26)  # GPIO22-25 do not exist on the S3
FLASH_PSRAM = range(26, 38)  # GPIO26-37: flash and octal PSRAM on the N16R8
USB_MODEM = (19, 20)  # native USB: the USB modem port
POT_ADC1 = range(1, 11)  # the volume pot must be on ADC1, GPIO1-10


def check_pin(name, number):
    """Raise ValueError if the pin number can't be used for this signal."""
    if not 0 <= number <= MAX_GPIO:
        raise ValueError(f"{name}={number} is not an ESP32-S3 GPIO (0-{MAX_GPIO})")
    if number in ABSENT:
        raise ValueError(f"{name}={number}: GPIO22-25 are not on the ESP32-S3")
    if number in FLASH_PSRAM:
        raise ValueError(f"{name}={number}: GPIO26-37 are used by the flash and octal PSRAM")
    if number in USB_MODEM:
        raise ValueError(f"{name}={number}: GPIO19 and GPIO20 are the USB modem port")
    if name.endswith("_POT") and number not in POT_ADC1:
        raise ValueError(f"{name}={number}: the volume pot must be on ADC1 (GPIO1-10)")
