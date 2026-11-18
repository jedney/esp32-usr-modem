"""About contains short project names and URLs, with stable ASCII output."""
import importlib.util
from pathlib import Path


def test_about_generation_keeps_only_names_and_urls_and_is_stable(tmp_path):
    script = Path(__file__).resolve().parents[2] / "scripts/generate_credits.py"
    spec = importlib.util.spec_from_file_location("generate_credits", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ROOT = tmp_path
    (tmp_path / "credits").mkdir()
    (tmp_path / "lib/config/src").mkdir(parents=True)
    (tmp_path / "CREDITS.md").write_text(
        '| Component | Used for | Author / source | Licence | Obligations | Status | About |\n'
        '|---|---|---|---|---|---|---|\n'
        '| Test "tone" | Sounds | Caf\u00e9 | CC0 | Credit | Used | [Test tone](https://example.org/tone) |\n'
        '| Second use | Sounds | Example | CC0 | Keep header | Used | [Test tone](https://example.org/tone) |\n'
        '| Future | Transfer | Someone | MIT | Keep header | Planned | - |\n'
        'Concept credits (no code used): Example\n',
        encoding="utf-8",
    )
    module.generate()
    output = tmp_path / "lib/config/src/credits_generated.h"
    generated = output.read_text(encoding="ascii")
    assert 'Test tone: https://example.org/tone' in generated
    assert generated.count('Test tone: https://example.org/tone') == 1
    assert 'CC0' not in generated and 'Obligations' not in generated
    assert 'Future' not in generated and 'Concept credits' not in generated
    assert 'https://github.com/jedney/esp32-usr-modem' in generated
    timestamp = output.stat().st_mtime_ns
    module.generate()
    assert output.stat().st_mtime_ns == timestamp
