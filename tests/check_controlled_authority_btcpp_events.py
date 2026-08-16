#!/usr/bin/env python3

"""Validate canonical evidence emitted by the BehaviorTree.CPP task runner."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EVENT_SCHEMA = ROOT / "schemas" / "event_log" / "v1" / "mbt.evt.v1.schema.json"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_controlled_authority_btcpp_events.py RUNNER")
    runner = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mbt-btcpp-events-") as temporary:
        output = Path(temporary)
        subprocess.run([str(runner), "--dump-events", str(output)], check=True)
        streams = sorted(output.glob("*.mbt.evt.v1.jsonl"))
        assert [path.name for path in streams] == [
            "task.mbt.evt.v1.jsonl",
            "variant.mbt.evt.v1.jsonl",
        ]
        try:
            import jsonschema
        except ImportError:
            jsonschema = None
        schema = json.loads(EVENT_SCHEMA.read_text(encoding="utf-8"))
        validator = jsonschema.Draft202012Validator(schema) if jsonschema else None
        for stream in streams:
            events = [
                json.loads(line)
                for line in stream.read_text(encoding="utf-8").splitlines()
                if line.strip()
            ]
            assert events and events[0]["type"] == "run_start"
            assert [event["seq"] for event in events] == list(range(1, len(events) + 1))
            if validator:
                for event in events:
                    validator.validate(event)
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "validate_trace.py"),
                    "check",
                    "--profile",
                    "deterministic",
                    str(stream),
                ],
                check=True,
            )
    print("BehaviorTree.CPP controlled-authority evidence valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
