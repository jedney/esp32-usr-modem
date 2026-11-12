"""GPIO rules for the S3 N16R8 board, and the rev B defaults in .env.example."""
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


rules = load("pin_rules")
ota = load("ota_upload")


def test_rev_b_defaults_pass_every_rule():
    pins = {k: int(v) for k, v in ota.read_env_file(ROOT / ".env.example").items()
            if k.startswith("USR_MODEM_PIN_")}
    assert len(pins) > 20
    for name, number in pins.items():
        rules.check_pin(name, number)


@pytest.mark.parametrize("number", [-1, 49, 22, 23, 24, 25, 26, 30, 37, 19, 20])
def test_unusable_gpio_is_rejected(number):
    with pytest.raises(ValueError, match="USR_MODEM_PIN_RXD"):
        rules.check_pin("USR_MODEM_PIN_RXD", number)


@pytest.mark.parametrize("number", [0, 1, 18, 21, 38, 42, 47, 48])
def test_usable_gpio_is_accepted(number):
    rules.check_pin("USR_MODEM_PIN_RXD", number)


@pytest.mark.parametrize("number", [0, 11, 14, 21, 42])
def test_pot_must_be_on_adc1(number):
    with pytest.raises(ValueError, match="ADC1"):
        rules.check_pin("USR_MODEM_PIN_POT", number)


@pytest.mark.parametrize("number", [1, 5, 10])
def test_pot_on_adc1_is_accepted(number):
    rules.check_pin("USR_MODEM_PIN_POT", number)
