"""Warnings apply to the project's own code only, and the two places that set them agree."""
import configparser
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def _script_flags():
    text = (ROOT / 'scripts/project_warnings.py').read_text(encoding='utf-8')
    body = re.search(r'PROJECT_WARNINGS = \((.*?)\)', text, re.S).group(1)
    return re.findall(r'"(-[^"]+)"', body)


def _cmake_flags():
    text = (ROOT / 'src/CMakeLists.txt').read_text(encoding='utf-8')
    body = re.search(r'target_compile_options\(\$\{COMPONENT_LIB\} PRIVATE\s+(.*?)\)', text, re.S)
    return body.group(1).split()


def _firmware_environment():
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(ROOT / 'platformio.ini', encoding='utf-8')
    return ini['env:s3']


def test_lib_and_src_get_the_same_warnings():
    assert _script_flags() == _cmake_flags()


def test_a_deprecated_framework_api_is_an_error_in_project_code():
    assert '-Werror=deprecated-declarations' in _script_flags()


def test_firmware_build_flags_do_not_reach_the_framework():
    flags = _firmware_environment()['build_flags'].split()
    assert not [flag for flag in flags if flag.startswith('-W')]


def test_lib_warnings_script_runs_after_the_framework_sets_its_flags():
    scripts = _firmware_environment()['extra_scripts'].split()
    assert 'post:scripts/project_warnings.py' in scripts
