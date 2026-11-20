import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('sdkconfig_refresh',
                                              ROOT / 'scripts/sdkconfig_refresh.py')
sdkconfig_refresh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdkconfig_refresh)


def make_project(tmp_path, defaults='CONFIG_A=y\n'):
    (tmp_path / 'sdkconfig.defaults').write_text(defaults)
    (tmp_path / 'sdkconfig.s3').write_text('generated\n')
    return tmp_path


def test_first_build_regenerates(tmp_path):
    project = make_project(tmp_path)
    assert sdkconfig_refresh.refresh(project, 's3')
    assert not (project / 'sdkconfig.s3').exists()


def test_unchanged_defaults_keep_the_generated_file(tmp_path):
    project = make_project(tmp_path)
    sdkconfig_refresh.refresh(project, 's3')
    (project / 'sdkconfig.s3').write_text('generated\n')
    assert not sdkconfig_refresh.refresh(project, 's3')
    assert (project / 'sdkconfig.s3').exists()


def test_changed_defaults_regenerate(tmp_path):
    project = make_project(tmp_path)
    sdkconfig_refresh.refresh(project, 's3')
    (project / 'sdkconfig.s3').write_text('generated\n')
    (project / 'sdkconfig.defaults').write_text('CONFIG_A=n\n')
    assert sdkconfig_refresh.refresh(project, 's3')
    assert not (project / 'sdkconfig.s3').exists()


def test_each_environment_has_its_own_record(tmp_path):
    project = make_project(tmp_path)
    (project / 'sdkconfig.s3_ota').write_text('generated\n')
    sdkconfig_refresh.refresh(project, 's3')
    assert (project / 'sdkconfig.s3_ota').exists()
    assert sdkconfig_refresh.refresh(project, 's3_ota')
    assert not (project / 'sdkconfig.s3_ota').exists()
