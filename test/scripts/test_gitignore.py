"""The project .gitignore keeps build output, downloads, generated files and secrets out of git.

A PlatformIO Core that rewrites project templates (the pioarduino fork does, for any template
not named ".optional") can replace it with a 5-line default; `git add -A` then picks up
build/, managed_components/ and web/preview.html. This test fails if that happens.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_gitignore_keeps_generated_files_and_secrets_out():
    text = (ROOT / ".gitignore").read_text(encoding="utf-8")
    entries = {line.strip() for line in text.splitlines()}
    for entry in (".pio/", "/build/", "/sdkconfig.s3*", "/managed_components/", "data/www/",
                  "data/image-id.txt", "web/preview.html", "web/node_modules/", ".env"):
        assert entry in entries, entry
