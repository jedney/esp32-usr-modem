"""Regenerate the build's sdkconfig when sdkconfig.defaults changes.

ESP-IDF writes sdkconfig.<env> once and afterwards takes from sdkconfig.defaults only keys
it has not seen yet, so a changed value there would be ignored and the build would keep
the old CPU, PSRAM, network or USB settings. This deletes the generated file whenever the
defaults' SHA-256 differs from the one recorded at the last build, so IDF writes it again.
"""

import hashlib
from pathlib import Path


def refresh(project, env_name):
    """Delete sdkconfig.<env_name> if sdkconfig.defaults changed since the last call.

    Returns True when it was deleted (or there was nothing recorded yet).
    """
    digest = hashlib.sha256((project / "sdkconfig.defaults").read_bytes()).hexdigest()
    stamp = project / ".pio" / ("sdkconfig-defaults-" + env_name)
    if stamp.exists() and stamp.read_text() == digest:
        return False
    (project / ("sdkconfig." + env_name)).unlink(missing_ok=True)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(digest)
    return True


if "Import" in globals():
    Import("env")  # noqa: F821
    refresh(Path(env.subst("$PROJECT_DIR")), env.subst("$PIOENV"))  # noqa: F821
