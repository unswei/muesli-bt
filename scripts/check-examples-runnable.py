#!/usr/bin/env python3
"""Smoke-test beginner examples after the dev build exists."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MUSLISP = ROOT / "build/dev/muslisp"
EXAMPLES = [
    ("lisp basics", ROOT / "examples/repl_scripts/lisp-basics.lisp", ["12.5", "#t", "(a b c)", "11"]),
    ("hello bt", ROOT / "examples/bt/hello_bt.lisp", ["running", "success", "tick_end"]),
    ("minimal real bt", ROOT / "examples/bt/minimal_real_bt.lisp", ["success", "1", "0", "tick_end"]),
]


def main() -> int:
    if not MUSLISP.exists():
        print(f"missing {MUSLISP}; build first with cmake --preset dev", file=sys.stderr)
        return 1
    errors: list[str] = []
    for name, script, needles in EXAMPLES:
        proc = subprocess.run(
            [str(MUSLISP), str(script.relative_to(ROOT))],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if proc.returncode != 0:
            errors.append(f"{name}: exited {proc.returncode}\n{proc.stdout}")
            continue
        for needle in needles:
            if needle not in proc.stdout:
                errors.append(f"{name}: expected {needle!r} in output\n{proc.stdout}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("beginner examples ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
