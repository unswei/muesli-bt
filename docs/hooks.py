from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
RENDER_SCRIPT = ROOT / "scripts" / "render-doc-diagrams.py"


def on_pre_build(config, **kwargs):
    subprocess.run(["python3", str(RENDER_SCRIPT)], check=True)
