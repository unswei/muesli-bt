"""Run the deterministic WP2 air-hockey scenarios against a fresh fake host."""

from __future__ import annotations

import argparse
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

from muesli_air_hockey_host import (
    FakeDirectLaunchBackend,
    ProtocolProcessor,
    SchemaRegistry,
    UnixHostServer,
)

SCENARIO_CONFIGS = {
    "H1": "h1_current.json",
    "H2a": "h2a_context_baseline.json",
    "H2b": "h2b_context_full.json",
    "H3": "h3_supersession.json",
    "H4": "h4_commit_consume_race.json",
    "H5": "h5_branch_exit.json",
    "H6": "h6_timeout.json",
    "H7": "h7_duplicate.json",
    "H8": "h8_replay.json",
}
CONFIG_KEYS = {
    "schema_version",
    "scenario",
    "acceptance_policy",
    "bt",
    "intervention",
    "expected_predicates",
}


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise TypeError(f"expected JSON object: {path}")
    return value


def load_contracts() -> tuple[dict[str, dict[str, Any]], set[str]]:
    predicate_document = load_json(EXAMPLE_ROOT / "evidence" / "g2_predicates.json")
    if (
        set(predicate_document) != {"schema_version", "predicates"}
        or predicate_document.get("schema_version") != "airhockey.g2.predicates.v1"
    ):
        raise RuntimeError("invalid G2 predicate manifest envelope")
    predicates = predicate_document.get("predicates")
    if (
        not isinstance(predicates, dict)
        or not predicates
        or not all(
            isinstance(name, str) and isinstance(description, str) and description
            for name, description in predicates.items()
        )
    ):
        raise RuntimeError("invalid G2 predicate definitions")

    configurations: dict[str, dict[str, Any]] = {}
    observed_predicates: set[str] = set()
    for scenario, filename in SCENARIO_CONFIGS.items():
        configuration = load_json(EXAMPLE_ROOT / "configs" / filename)
        if set(configuration) != CONFIG_KEYS:
            raise RuntimeError(f"{filename} has an unexpected configuration shape")
        if configuration["schema_version"] != "airhockey.g2.scenario.v1":
            raise RuntimeError(f"{filename} has an unsupported schema version")
        if configuration["scenario"] != scenario:
            raise RuntimeError(
                f"{filename} scenario does not match its filename mapping"
            )
        if configuration["acceptance_policy"] not in {
            "both",
            "deadline_only",
            "invocation_scoped",
        }:
            raise RuntimeError(f"{filename} has an unsupported acceptance policy")
        expected = configuration["expected_predicates"]
        if (
            not isinstance(expected, list)
            or not expected
            or not all(
                isinstance(name, str) and name in predicates for name in expected
            )
        ):
            raise RuntimeError(f"{filename} references an unknown evidence predicate")
        if observed_predicates.intersection(expected):
            raise RuntimeError(
                f"{filename} repeats a predicate owned by another scenario"
            )
        observed_predicates.update(expected)
        tree = EXAMPLE_ROOT / configuration["bt"]
        if not tree.is_file() or EXAMPLE_ROOT not in tree.resolve().parents:
            raise RuntimeError(f"{filename} references a missing or external BT")
        configurations[scenario] = configuration

    if observed_predicates != set(predicates):
        missing = sorted(set(predicates) - observed_predicates)
        raise RuntimeError(f"unowned G2 predicates: {missing}")
    baseline = (EXAMPLE_ROOT / "lisp" / "bt_deadline_only.lisp").read_text(
        encoding="utf-8"
    )
    full = (EXAMPLE_ROOT / "lisp" / "bt_invocation_scoped.lisp").read_text(
        encoding="utf-8"
    )
    if baseline.replace("deadline_only", "<acceptance-policy>") != full.replace(
        "invocation_scoped", "<acceptance-policy>"
    ):
        raise RuntimeError(
            "deadline-only and invocation-scoped BTs differ beyond acceptance policy"
        )
    return configurations, set(predicates)


def run_scenario(
    executable: Path,
    scenario: str,
    configuration: dict[str, Any],
    fixture_path: Path | None,
) -> None:
    schema_directory = REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1"
    schemas = SchemaRegistry(schema_directory)
    with tempfile.TemporaryDirectory(
        prefix=f"muesli-air-hockey-{scenario.lower()}-"
    ) as directory:
        socket_path = Path(directory) / "host.sock"
        processor = ProtocolProcessor(schemas, FakeDirectLaunchBackend())
        with UnixHostServer(socket_path, processor):
            command = [
                str(executable),
                scenario,
                str(socket_path),
                str(EXAMPLE_ROOT / configuration["bt"]),
            ]
            if fixture_path is not None:
                command.append(str(fixture_path))
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
        raise RuntimeError(f"{scenario} runner exited with {completed.returncode}")
    observed = {
        line.removeprefix("PREDICATE ").removesuffix(" PASS")
        for line in completed.stdout.splitlines()
        if line.startswith("PREDICATE ") and line.endswith(" PASS")
    }
    expected = set(configuration["expected_predicates"])
    if observed != expected:
        raise RuntimeError(
            f"{scenario} predicate mismatch: expected {sorted(expected)}, observed {sorted(observed)}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--runner", required=True, type=Path, help="compiled WP2 scenario executable"
    )
    parser.add_argument(
        "--scenario",
        choices=SCENARIO_CONFIGS,
        help="run one scenario; default runs all",
    )
    parser.add_argument(
        "--write-fixture",
        type=Path,
        help="write the selected scenario's canonical events to a new path",
    )
    arguments = parser.parse_args()

    configurations, _ = load_contracts()
    executable = arguments.runner.resolve()
    if not executable.is_file():
        raise RuntimeError(f"scenario runner does not exist: {executable}")
    selected = [arguments.scenario] if arguments.scenario else list(SCENARIO_CONFIGS)
    if arguments.write_fixture is not None:
        if len(selected) != 1:
            raise RuntimeError("--write-fixture requires --scenario")
        fixture = arguments.write_fixture.resolve()
        if fixture.exists():
            raise RuntimeError(f"refuse to replace existing fixture: {fixture}")
        fixture.parent.mkdir(parents=True, exist_ok=True)
    else:
        fixture = None

    for scenario in selected:
        run_scenario(executable, scenario, configurations[scenario], fixture)
    print(f"air-hockey Gate G2 scenarios passed: {', '.join(selected)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
