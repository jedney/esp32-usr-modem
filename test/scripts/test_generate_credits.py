"""About includes licensing status, with stable ASCII output for serial terminals."""
import importlib.util
from pathlib import Path


def test_about_generation_keeps_used_and_planned_attribution_and_is_stable(tmp_path):
    script = Path(__file__).resolve().parents[2] / "scripts/generate_credits.py"
    spec = importlib.util.spec_from_file_location("generate_credits", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ROOT = tmp_path
    (tmp_path / "credits").mkdir()
    (tmp_path / "lib/config/src").mkdir(parents=True)
    (tmp_path / "credits/LICENSES.md").write_text(
        '| Component | Used for | Author / source | Licence | Obligations | Status |\n'
        '|---|---|---|---|---|---|\n'
        '| Test "tone" | Sounds | Caf\u00e9 | CC0 | Credit | Used |\n'
        '| Future | Transfer | Someone | MIT | Keep header | Planned |\n'
        'Concept credits (no code used): Example\n',
        encoding="utf-8",
    )
    module.generate()
    output = tmp_path / "lib/config/src/credits_generated.h"
    generated = output.read_text(encoding="ascii")
    assert r'Test \"tone\"' in generated
    assert "Cafe" in generated and "CC0" in generated and "Used" in generated
    assert "Future" in generated and "Planned" in generated
    assert "Concept credits" in generated
    timestamp = output.stat().st_mtime_ns
    module.generate()
    assert output.stat().st_mtime_ns == timestamp
