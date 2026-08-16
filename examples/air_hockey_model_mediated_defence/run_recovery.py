"""Run the frozen current-context recovery arm against the pure fake host."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

EXAMPLE_ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = EXAMPLE_ROOT.parents[1]
HOST_ROOT = EXAMPLE_ROOT / "host"
sys.path.insert(0, str(HOST_ROOT / "src"))

from muesli_air_hockey_host import (  # noqa: E402
    FakeDirectLaunchBackend,
    ProtocolProcessor,
    SchemaRegistry,
    UnixHostServer,
)

PROTOCOL_PATH = EXAMPLE_ROOT / "configs" / "wp8_recovery_protocol.json"
EXPECTED_PREDICATES = {
    "p8_current_context_recovery_target",
    "p8_zero_obsolete_dispatch",
    "p8_recovery_episode_completed",
}


def load_protocol() -> dict[str, Any]:
    protocol = json.loads(PROTOCOL_PATH.read_text(encoding="utf-8"))
    if not isinstance(protocol, dict):
        raise RuntimeError("WP8 recovery protocol must be a JSON object")
    if protocol.get("status") != "frozen_before_recovery_campaign":
        raise RuntimeError("WP8 recovery protocol is not frozen")
    if protocol.get("paired_trial", {}).get("treatments") != [
        "deadline_only",
        "invocation_scoped_hold",
        "invocation_scoped_current_context_recovery",
    ]:
        raise RuntimeError("WP8 recovery treatment order changed")
    recovery = protocol.get("recovery_policy", {})
    if recovery.get("privileged_state") or recovery.get("external_inference"):
        raise RuntimeError("WP8 recovery must remain public-observation-only")
    parent = EXAMPLE_ROOT / protocol["parent_wp7_protocol"]
    parent_hash = hashlib.sha256(parent.read_bytes()).hexdigest()
    if parent_hash != protocol["parent_wp7_protocol_sha256"]:
        raise RuntimeError("WP8 parent WP7 protocol hash changed")
    for treatment in protocol["treatments"].values():
        tree = EXAMPLE_ROOT / treatment["tree"]
        if not tree.is_file() or EXAMPLE_ROOT not in tree.resolve().parents:
            raise RuntimeError("WP8 treatment tree must be a checked-in local tree")
        tree_hash = hashlib.sha256(tree.read_bytes()).hexdigest()
        if tree_hash != treatment["tree_sha256"]:
            raise RuntimeError(f"WP8 treatment tree hash changed: {tree}")
    return protocol


def run_check(executable: Path) -> None:
    protocol = load_protocol()
    trial = protocol["paired_trial"]
    schema_directory = REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1"
    schemas = SchemaRegistry(schema_directory)
    tree = EXAMPLE_ROOT / protocol["treatments"][
        "invocation_scoped_current_context_recovery"
    ]["tree"]
    with tempfile.TemporaryDirectory(prefix="muesli-air-hockey-recovery-") as directory:
        root = Path(directory)
        socket_path = root / "host.sock"
        events_path = root / "events.jsonl"
        processor = ProtocolProcessor(schemas, FakeDirectLaunchBackend())
        with UnixHostServer(socket_path, processor):
            command = [
                str(executable),
                "P8-current-context-recovery",
                str(socket_path),
                str(tree),
                str(events_path),
                "p8-local-recovery",
                "0.80",
                "-0.40",
                str(trial["blackout_start_step"]),
                str(trial["blackout_length_steps"]),
                str(trial["timeout_steps"]),
                "0",
                str(trial["completion_delay_ms"][1]),
                "live",
            ]
            completed = subprocess.run(
                command,
                check=False,
                capture_output=True,
                text=True,
                timeout=20,
            )
    if completed.stdout:
        print(completed.stdout, end="")
    if completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)
    if completed.returncode != 0:
        raise RuntimeError(f"P8 recovery runner exited with {completed.returncode}")
    observed = {
        line.removeprefix("PREDICATE ").removesuffix(" PASS")
        for line in completed.stdout.splitlines()
        if line.startswith("PREDICATE ") and line.endswith(" PASS")
    }
    if observed != EXPECTED_PREDICATES:
        raise RuntimeError(
            f"P8 predicate mismatch: expected {sorted(EXPECTED_PREDICATES)}, "
            f"observed {sorted(observed)}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    arguments = parser.parse_args()
    executable = arguments.runner.resolve()
    if not executable.is_file():
        raise RuntimeError(f"scenario runner does not exist: {executable}")
    load_protocol()
    run_check(executable)
    print("air-hockey current-context recovery check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
