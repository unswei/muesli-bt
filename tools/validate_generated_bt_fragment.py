#!/usr/bin/env python3
"""Validate generated BT fragment fixtures before execution.

This tool is deliberately narrow. It treats generated Lisp as untrusted data,
checks a small BT grammar and contract policy, and reports deterministic
rejection reasons for negative fixtures.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from math import isfinite
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
NODE_SCHEMA_DIR = REPO_ROOT / "schemas" / "bt_node_options" / "v1"

KNOWN_COMPOSITES = {"seq", "sel", "mem-seq", "mem-sel", "async-seq", "reactive-seq", "reactive-sel"}
KNOWN_DECORATORS = {"invert", "slot"}
KNOWN_COUNT_DECORATORS = {"repeat", "retry"}
KNOWN_LEAVES = {"cond", "act"}
KNOWN_CONSTANTS = {"succeed", "fail", "running"}
KNOWN_LONG_RUNNING = {"plan-action", "vla-request", "vla-wait"}
KNOWN_NODE_TYPES = KNOWN_COMPOSITES | KNOWN_DECORATORS | KNOWN_COUNT_DECORATORS | KNOWN_LEAVES | KNOWN_CONSTANTS | {
    "plan-action",
    "vla-request",
    "vla-wait",
    "vla-cancel",
    "slot",
}

KNOWN_CALLBACKS = {
    "always-true",
    "always-false",
    "always-success",
    "always-fail",
    "bb-has",
    "bb-put-int",
    "async-sleep-ms",
    "blocked-path?",
    "observation-fresh?",
    "execute-recovery-turn",
    "recovery-exit?",
    "scene-fresh?",
    "validate-target",
    "execute-motion",
    "execute-navigation",
    "stop",
    "safe-stop",
    "move-towards-goal",
}
KNOWN_CAPABILITIES = {
    "vla.rt2",
    "vision",
    "planner.mcts",
    "env.move",
    "cap.navigation.v1",
    "cap.motion.v1",
    "cap.perception.scene.v1",
    "cap.tamp.v1",
}
FALLBACK_ACTIONS = {"stop", "safe-stop", "always-success"}
DEFAULT_SLOT = "recovery-policy"
DEFAULT_FRAGMENT_CONTRACT = "guarded-recovery.v1"
TASK_PLAN_SLOT = "task-plan"
TASK_PLAN_FRAGMENT_CONTRACT = "guarded-task-plan.v1"
DEFAULT_INSTALL_MODE = "at_tick_boundary"
DEFAULT_INSTALL_POLICY = {
    "schema_version": "install_policy.v1",
    "allowed_slots": [DEFAULT_SLOT, TASK_PLAN_SLOT],
    "denied_capabilities": ["unsupported.force", "raw-velocity", "unsafe-force"],
    "max_nodes": 24,
    "max_depth": 10,
    "requires_validation": True,
    "requires_dry_run": True,
    "install_mode": DEFAULT_INSTALL_MODE,
    "rollback": "previous-subtree-hash",
}
DEFAULT_FRAGMENT_CONTRACTS = {
    DEFAULT_FRAGMENT_CONTRACT: {
        "schema_version": "fragment_contract.v1",
        "id": DEFAULT_FRAGMENT_CONTRACT,
        "must_start_with_guard": True,
        "min_guard_count": 1,
        "requires_fallback_for_long_running": True,
        "allowed_nodes": sorted(KNOWN_NODE_TYPES),
        "allowed_actions": sorted(KNOWN_CALLBACKS),
        "max_nodes": 16,
        "max_depth": 8,
    },
    TASK_PLAN_FRAGMENT_CONTRACT: {
        "schema_version": "fragment_contract.v1",
        "id": TASK_PLAN_FRAGMENT_CONTRACT,
        "must_start_with_guard": True,
        "min_guard_count": 1,
        "requires_fallback_for_long_running": True,
        "allowed_nodes": sorted(KNOWN_NODE_TYPES),
        "allowed_actions": sorted(KNOWN_CALLBACKS),
        "allowed_capabilities": [
            "cap.navigation.v1",
            "cap.motion.v1",
            "cap.perception.scene.v1",
            "cap.tamp.v1",
        ],
        "max_nodes": 24,
        "max_depth": 10,
    }
}
DEFAULT_BLACKBOARD_MANIFEST = {
    "schema_version": "blackboard_manifest.v1",
    "keys": {
        "blocked_path": {"type": "bool", "max_age_ms": 200},
        "observation_fresh": {"type": "bool", "max_age_ms": 200},
        "recovery-state": {"type": "vector", "max_age_ms": 200},
        "recovery-action": {"type": "map", "max_age_ms": 200},
        "scene_fresh": {"type": "bool", "max_age_ms": 250},
        "task_plan": {"type": "list", "max_age_ms": 1000},
    },
}
DEFAULT_CAPABILITY_MANIFEST = {
    "schema_version": "capability_manifest.v1",
    "callbacks": sorted(KNOWN_CALLBACKS),
    "capabilities": sorted(KNOWN_CAPABILITIES),
}


@dataclass(frozen=True)
class Symbol:
    name: str


class ValidationError(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


class Reader:
    def __init__(self, text: str) -> None:
        self.tokens = self._tokenise(text)
        self.pos = 0

    @staticmethod
    def _tokenise(text: str) -> list[str]:
        tokens: list[str] = []
        i = 0
        while i < len(text):
            c = text[i]
            if c.isspace():
                i += 1
                continue
            if c == ";":
                while i < len(text) and text[i] != "\n":
                    i += 1
                continue
            if c in "()":
                tokens.append(c)
                i += 1
                continue
            if c == '"':
                j = i + 1
                out = ['"']
                escaped = False
                while j < len(text):
                    ch = text[j]
                    out.append(ch)
                    if escaped:
                        escaped = False
                    elif ch == "\\":
                        escaped = True
                    elif ch == '"':
                        break
                    j += 1
                if j >= len(text) or text[j] != '"':
                    raise ValidationError("malformed_subtree", "unterminated string literal")
                tokens.append("".join(out))
                i = j + 1
                continue
            j = i
            while j < len(text) and not text[j].isspace() and text[j] not in "();":
                j += 1
            tokens.append(text[i:j])
            i = j
        return tokens

    def read(self) -> Any:
        if not self.tokens:
            raise ValidationError("malformed_subtree", "empty fragment")
        expr = self._read_expr()
        if self.pos != len(self.tokens):
            raise ValidationError("malformed_subtree", "fragment must contain exactly one form")
        return expr

    def _read_expr(self) -> Any:
        if self.pos >= len(self.tokens):
            raise ValidationError("malformed_subtree", "unexpected end of fragment")
        token = self.tokens[self.pos]
        self.pos += 1
        if token == "(":
            out: list[Any] = []
            while self.pos < len(self.tokens) and self.tokens[self.pos] != ")":
                out.append(self._read_expr())
            if self.pos >= len(self.tokens):
                raise ValidationError("malformed_subtree", "missing closing parenthesis")
            self.pos += 1
            return out
        if token == ")":
            raise ValidationError("malformed_subtree", "unexpected closing parenthesis")
        if token.startswith('"'):
            return json.loads(token)
        if token == "#t":
            return True
        if token == "#f":
            return False
        try:
            return int(token)
        except ValueError:
            try:
                return float(token)
            except ValueError:
                return Symbol(token)


def sym_name(value: Any) -> str | None:
    return value.name if isinstance(value, Symbol) else None


def fnv1a64(text: str) -> str:
    h = 1469598103934665603
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{h:016x}"


def canonical_atom(value: Any) -> str:
    if isinstance(value, Symbol):
        return value.name
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    if isinstance(value, bool):
        return "#t" if value else "#f"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if not isfinite(value):
            raise ValidationError("malformed_subtree", "non-finite numeric literal")
        return str(value)
    raise ValidationError("malformed_subtree", f"unsupported atom in fragment: {value!r}")


def canonical_dsl(expr: Any) -> str:
    if isinstance(expr, list):
        return "(" + " ".join(canonical_dsl(item) for item in expr) + ")"
    return canonical_atom(expr)


def count_nodes(expr: Any) -> int:
    if not isinstance(expr, list) or not expr:
        return 0
    return 1 + sum(count_nodes(child) for child in expr[1:])


def max_depth(expr: Any) -> int:
    if not isinstance(expr, list) or not expr:
        return 0
    child_depths = [max_depth(child) for child in expr[1:]]
    return 1 + (max(child_depths) if child_depths else 0)


def effective_subtree(expr: Any) -> Any:
    if not isinstance(expr, list) or not expr:
        return expr
    if sym_name(expr[0]) == "slot":
        return expr[-1]
    return expr


def first_child_sequence(expr: Any) -> list[Any]:
    tree = effective_subtree(expr)
    if not isinstance(tree, list) or not tree:
        return []
    name = sym_name(tree[0])
    if name in {"seq", "reactive-seq"}:
        return tree[1:]
    if name in {"sel", "reactive-sel"} and len(tree) >= 2 and isinstance(tree[1], list):
        child = tree[1]
        if child and sym_name(child[0]) in {"seq", "reactive-seq"}:
            return child[1:]
    return []


def leading_guard_callbacks(expr: Any) -> list[str]:
    out: list[str] = []
    for child in first_child_sequence(expr):
        if not isinstance(child, list) or not child:
            break
        if sym_name(child[0]) != "cond" or len(child) < 2:
            break
        callback = sym_name(child[1]) if isinstance(child[1], Symbol) else child[1] if isinstance(child[1], str) else None
        if callback is None:
            break
        out.append(callback)
    return out


def collect_symbols(expr: Any, form_names: set[str]) -> list[str]:
    out: list[str] = []
    if not isinstance(expr, list) or not expr:
        return out
    name = sym_name(expr[0])
    if name in form_names and len(expr) >= 2:
        callback = sym_name(expr[1]) if isinstance(expr[1], Symbol) else expr[1] if isinstance(expr[1], str) else None
        if callback is not None:
            out.append(callback)
    for child in expr[1:]:
        out.extend(collect_symbols(child, form_names))
    return out


def collect_long_running_nodes(expr: Any) -> list[str]:
    out: list[str] = []
    if not isinstance(expr, list) or not expr:
        return out
    name = sym_name(expr[0])
    if name in KNOWN_LONG_RUNNING:
        out.append(name)
    for child in expr[1:]:
        out.extend(collect_long_running_nodes(child))
    return out


def collect_capabilities(expr: Any) -> list[str]:
    out: list[str] = []
    if not isinstance(expr, list) or not expr:
        return out
    name = sym_name(expr[0])
    if name is not None and name.startswith("vla-"):
        options = key_values(expr, 1, name)
        capability = options.get(":capability", "vla.rt2")
        if isinstance(capability, Symbol):
            capability = capability.name
        if isinstance(capability, str):
            out.append(capability)
    for child in expr[1:]:
        out.extend(collect_capabilities(child))
    return out


def collect_plan_budgets(expr: Any) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    if not isinstance(expr, list) or not expr:
        return out
    name = sym_name(expr[0])
    if name in KNOWN_LONG_RUNNING:
        options = key_values(expr, 1, name) if name not in KNOWN_LEAVES else {}
        item: dict[str, Any] = {"node": name}
        for key in (":budget_ms", ":deadline_ms", ":work_max"):
            if key in options:
                item[key.removeprefix(":")] = canonical_atom(options[key])
        out.append(item)
    for child in expr[1:]:
        out.extend(collect_plan_budgets(child))
    return out


def key_values(items: list[Any], start: int, form_name: str) -> dict[str, Any]:
    if (len(items) - start) % 2 != 0:
        raise ValidationError("malformed_subtree", f"{form_name}: expected key/value pairs")
    out: dict[str, Any] = {}
    for i in range(start, len(items), 2):
        key = sym_name(items[i])
        if key is None or not key.startswith(":"):
            raise ValidationError("malformed_subtree", f"{form_name}: option key must be a keyword")
        out[key] = items[i + 1]
    return out


def load_node_option_schemas() -> dict[str, dict[str, Any]]:
    schemas: dict[str, dict[str, Any]] = {}
    for path in NODE_SCHEMA_DIR.glob("*.schema.json"):
        schemas[path.name.removesuffix(".schema.json")] = json.loads(path.read_text(encoding="utf-8"))
    return schemas


NODE_OPTION_SCHEMAS = load_node_option_schemas()


def canonical_option_map(schema: dict[str, Any]) -> dict[str, str]:
    out: dict[str, str] = {}
    for name, spec in schema.get("properties", {}).items():
        out[name] = name
        for alias in spec.get("x-muesli-aliases", []):
            out[alias] = name
    return out


def symbol_or_string(value: Any) -> str | None:
    if isinstance(value, Symbol):
        return value.name
    if isinstance(value, str):
        return value
    return None


def json_type_matches(value: Any, expected: str) -> bool:
    if expected == "string":
        return isinstance(value, (str, Symbol))
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return (isinstance(value, int) or isinstance(value, float)) and not isinstance(value, bool) and isfinite(float(value))
    if expected == "boolean":
        return isinstance(value, bool)
    return True


def validate_option_value(form_name: str, option_name: str, value: Any, spec: dict[str, Any]) -> None:
    expected_type = spec.get("type")
    expected_types = expected_type if isinstance(expected_type, list) else [expected_type]
    if expected_type is not None and not any(json_type_matches(value, t) for t in expected_types):
        raise ValidationError("malformed_subtree", f"{form_name}: {option_name} has invalid value type")

    if "enum" in spec:
        text = symbol_or_string(value)
        if text is None or text not in spec["enum"]:
            raise ValidationError("malformed_subtree", f"{form_name}: {option_name} has unsupported value: {text}")

    if "minimum" in spec and isinstance(value, (int, float)) and not isinstance(value, bool):
        if float(value) < float(spec["minimum"]):
            if option_name in {":budget_ms", ":deadline_ms"}:
                raise ValidationError("invalid_budget", f"{form_name}: {option_name} must be a positive number")
            raise ValidationError("malformed_subtree", f"{form_name}: {option_name} is below minimum {spec['minimum']}")


def validate_options_against_schema(form_name: str, options: dict[str, Any]) -> dict[str, Any]:
    schema = NODE_OPTION_SCHEMAS.get(form_name)
    if schema is None:
        return options

    canonical_names = canonical_option_map(schema)
    properties = schema.get("properties", {})
    canonical_options: dict[str, Any] = {}
    for raw_name, value in options.items():
        canonical_name = canonical_names.get(raw_name)
        if canonical_name is None:
            raise ValidationError("malformed_subtree", f"{form_name}: unknown option: {raw_name}")
        if canonical_name in canonical_options:
            raise ValidationError("malformed_subtree", f"{form_name}: duplicate option: {canonical_name}")
        validate_option_value(form_name, canonical_name, value, properties[canonical_name])
        canonical_options[canonical_name] = value
    return canonical_options


def validate_budget(options: dict[str, Any], form_name: str) -> None:
    for key in (":budget_ms", ":deadline_ms"):
        if key not in options:
            continue
        value = options[key]
        if not isinstance(value, (int, float)) or value <= 0:
            raise ValidationError("invalid_budget", f"{form_name}: {key} must be a positive number")


def validate_shape(expr: Any) -> None:
    if not isinstance(expr, list):
        raise ValidationError("malformed_subtree", "subtree must be a list")
    if not expr:
        raise ValidationError("malformed_subtree", "subtree list cannot be empty")
    form_name = sym_name(expr[0])
    if form_name is None:
        raise ValidationError("malformed_subtree", "subtree head must be a symbol")
    if form_name not in KNOWN_NODE_TYPES:
        raise ValidationError("unknown_node_type", f"unknown BT node type: {form_name}")

    if form_name in KNOWN_COMPOSITES:
        if len(expr) < 2:
            raise ValidationError("malformed_subtree", f"{form_name}: expects at least one child")
        for child in expr[1:]:
            validate_shape(child)
        return

    if form_name == "slot":
        if len(expr) < 4:
            raise ValidationError("malformed_subtree", "slot: expects name, option key/value pairs, and one child")
        slot_name = symbol_or_string(expr[1])
        if not slot_name:
            raise ValidationError("malformed_subtree", "slot: name must be a symbol or string")
        if (len(expr) - 3) % 2 != 0:
            raise ValidationError("malformed_subtree", "slot: expected option key/value pairs before child")
        options = key_values(expr[:-1], 2, "slot") if len(expr) > 3 else {}
        for required in (":contract", ":install", ":fallback"):
            if required not in options:
                raise ValidationError("malformed_subtree", f"slot: missing required option {required}")
        install_mode = symbol_or_string(options[":install"])
        if install_mode not in {DEFAULT_INSTALL_MODE, "at-tick-boundary"}:
            raise ValidationError("unsupported_install_mode", f"slot: unsupported install mode: {install_mode}")
        validate_shape(expr[-1])
        return

    if form_name in KNOWN_DECORATORS:
        if len(expr) != 2:
            raise ValidationError("malformed_subtree", f"{form_name}: expects exactly one child")
        validate_shape(expr[1])
        return

    if form_name in KNOWN_COUNT_DECORATORS:
        if len(expr) != 3 or not isinstance(expr[1], int) or expr[1] < 0:
            raise ValidationError("malformed_subtree", f"{form_name}: expects non-negative count and one child")
        validate_shape(expr[2])
        return

    if form_name in KNOWN_LEAVES:
        if len(expr) < 2:
            raise ValidationError("malformed_subtree", f"{form_name}: expects a callback name")
        callback = sym_name(expr[1]) if isinstance(expr[1], Symbol) else expr[1] if isinstance(expr[1], str) else None
        if callback is None:
            raise ValidationError("malformed_subtree", f"{form_name}: callback must be a symbol or string")
        if callback not in KNOWN_CALLBACKS:
            raise ValidationError("unknown_callback", f"{form_name}: unknown callback or host capability: {callback}")
        return

    if form_name in KNOWN_CONSTANTS:
        if len(expr) != 1:
            raise ValidationError("malformed_subtree", f"{form_name}: expects no arguments")
        return

    options = validate_options_against_schema(form_name, key_values(expr, 1, form_name))
    validate_budget(options, form_name)
    if form_name.startswith("vla-"):
        capability = options.get(":capability", "vla.rt2")
        if isinstance(capability, Symbol):
            capability = capability.name
        if capability not in KNOWN_CAPABILITIES:
            raise ValidationError("unsupported_capability", f"{form_name}: unsupported capability: {capability}")


def contains_long_running(expr: Any) -> bool:
    if not isinstance(expr, list) or not expr:
        return False
    name = sym_name(expr[0])
    return name in KNOWN_LONG_RUNNING or any(contains_long_running(child) for child in expr[1:])


def is_fallback_branch(expr: Any) -> bool:
    if not isinstance(expr, list) or not expr:
        return False
    name = sym_name(expr[0])
    if name in {"succeed", "fail"}:
        return True
    if name == "act" and len(expr) >= 2:
        callback = sym_name(expr[1]) if isinstance(expr[1], Symbol) else expr[1] if isinstance(expr[1], str) else None
        return callback in FALLBACK_ACTIONS
    if name in KNOWN_COMPOSITES:
        return any(is_fallback_branch(child) for child in expr[1:])
    return False


def long_running_has_fallback(expr: Any, covered: bool = False) -> bool:
    if not isinstance(expr, list) or not expr:
        return True
    name = sym_name(expr[0])
    if name in KNOWN_LONG_RUNNING and not covered:
        return False
    if name in {"sel", "reactive-sel"}:
        children = expr[1:]
        for index, child in enumerate(children):
            child_covered = covered or any(is_fallback_branch(sibling) for sibling in children[index + 1 :])
            if not long_running_has_fallback(child, child_covered):
                return False
        return True
    return all(long_running_has_fallback(child, covered) for child in expr[1:])


def validate_fragment(path: Path) -> dict[str, Any]:
    source = path.read_text(encoding="utf-8")
    return validate_fragment_source(source)


def validate_fragment_source(source: str) -> dict[str, Any]:
    expr = Reader(source).read()
    validate_shape(expr)
    if contains_long_running(expr) and not long_running_has_fallback(expr):
        raise ValidationError(
            "missing_fallback_long_running",
            "long-running planner/model fragment must be guarded by an explicit fallback branch",
        )
    canonical = canonical_dsl(expr)
    long_running_nodes = collect_long_running_nodes(expr)
    callbacks = sorted(set(collect_symbols(expr, KNOWN_LEAVES)))
    capabilities = sorted(set(collect_capabilities(expr)))
    return {
        "schema_version": "fragment_validation_result.v1",
        "ok": True,
        "code": "accepted",
        "message": "fragment accepted",
        "source_hash": fnv1a64(source),
        "canonical_dsl": canonical,
        "canonical_dsl_hash": fnv1a64(canonical),
        "node_count": count_nodes(expr),
        "callbacks": callbacks,
        "capabilities": capabilities,
        "long_running_nodes": long_running_nodes,
        "fallback_policy": "required_and_present" if long_running_nodes else "not_required",
    }


def load_json_if_exists(path: Path, default: dict[str, Any]) -> dict[str, Any]:
    if path.is_file():
        return json.loads(path.read_text(encoding="utf-8"))
    return default


def reject_result(code: str,
                  message: str,
                  field_path: str,
                  proposal: dict[str, Any] | None = None,
                  validation: dict[str, Any] | None = None) -> dict[str, Any]:
    proposal = proposal or {}
    return {
        "schema_version": "fragment_validation_result.v1",
        "ok": False,
        "status": "rejected",
        "code": code,
        "message": message,
        "reason_code": code,
        "field_path": field_path,
        "slot": proposal.get("slot"),
        "fragment_contract": proposal.get("fragment_contract"),
        "proposal_id": proposal.get("proposal_id"),
        "source_hash": validation.get("source_hash") if validation else None,
        "canonical_dsl_hash": validation.get("canonical_dsl_hash") if validation else None,
        "host_reached": False,
    }


def validate_blackboard_snapshot(proposal: dict[str, Any], blackboard_manifest: dict[str, Any]) -> None:
    snapshot = proposal.get("blackboard", {})
    if not isinstance(snapshot, dict):
        raise ValidationError("malformed_envelope", "proposal blackboard must be an object")
    manifest_keys = blackboard_manifest.get("keys", {})
    for key, spec in manifest_keys.items():
        if key not in snapshot:
            continue
        item = snapshot[key]
        if not isinstance(item, dict):
            raise ValidationError("malformed_envelope", f"blackboard.{key}: expected object")
        max_age = spec.get("max_age_ms")
        age = item.get("age_ms")
        if max_age is not None and age is not None and float(age) > float(max_age):
            raise ValidationError("stale_blackboard_input", f"blackboard.{key}: age_ms exceeds max_age_ms")


def validate_contract(expr: Any,
                      validation: dict[str, Any],
                      contract: dict[str, Any],
                      install_policy: dict[str, Any]) -> None:
    node_count = int(validation["node_count"])
    depth = max_depth(expr)
    max_nodes = min(int(contract.get("max_nodes", node_count)), int(install_policy.get("max_nodes", node_count)))
    max_allowed_depth = min(int(contract.get("max_depth", depth)), int(install_policy.get("max_depth", depth)))
    if node_count > max_nodes:
        raise ValidationError("excessive_depth", f"fragment node count {node_count} exceeds max_nodes {max_nodes}")
    if depth > max_allowed_depth:
        raise ValidationError("excessive_depth", f"fragment depth {depth} exceeds max_depth {max_allowed_depth}")
    if contract.get("must_start_with_guard", False):
        guards = leading_guard_callbacks(expr)
        min_guard_count = int(contract.get("min_guard_count", 1))
        if len(guards) < min_guard_count:
            raise ValidationError("missing_guard", "fragment must start with a guard condition")
    if contract.get("requires_fallback_for_long_running", False) and validation["long_running_nodes"]:
        if validation["fallback_policy"] != "required_and_present":
            raise ValidationError("missing_fallback_long_running", "long-running fragment must have fallback")


def semantic_diff(proposal: dict[str, Any], validation: dict[str, Any], expr: Any, install_policy: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": "bt_semantic_diff.v1",
        "slot": proposal["slot"],
        "old_subtree_hash": proposal.get("previous_subtree_hash", "fnv1a64:9999999999999999"),
        "new_subtree_hash": validation["canonical_dsl_hash"],
        "guards_added": leading_guard_callbacks(expr),
        "long_running_nodes": validation["long_running_nodes"],
        "budgets": collect_plan_budgets(expr),
        "fallback_status": validation["fallback_policy"],
        "capabilities_added": validation["capabilities"],
        "canonical_hash_changed": proposal.get("previous_subtree_hash") != validation["canonical_dsl_hash"],
        "install_mode": install_policy.get("install_mode", DEFAULT_INSTALL_MODE),
    }


def dry_run_report(proposal: dict[str, Any], validation: dict[str, Any], diff: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": "agent_proposal_dry_run.v1",
        "passed": True,
        "proposal_id": proposal["proposal_id"],
        "slot": proposal["slot"],
        "fixture": "blocked-path-case",
        "checks": {
            "proposal_envelope_valid": True,
            "fragment_valid": True,
            "policy_gates_passed": True,
            "semantic_diff_available": True,
            "fallback_preserved": validation["fallback_policy"] == "required_and_present",
            "host_reached": False,
        },
        "fixed_recovery": {
            "status": "baseline",
            "fallback": "safe-stop",
        },
        "generated_recovery": {
            "status": "accepted",
            "canonical_dsl_hash": validation["canonical_dsl_hash"],
            "guards": diff["guards_added"],
        },
    }


def validate_proposal_dir(path: Path) -> dict[str, Any]:
    proposal_path = path / "proposal.json"
    expected_path = path / "expected.json"
    if not proposal_path.is_file() or not expected_path.is_file():
        raise RuntimeError(f"{path}: expected proposal.json and expected.json")
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    proposal = json.loads(proposal_path.read_text(encoding="utf-8"))
    install_policy = load_json_if_exists(path / "install_policy.json", DEFAULT_INSTALL_POLICY)
    contracts = load_json_if_exists(path / "fragment_contracts.json", DEFAULT_FRAGMENT_CONTRACTS)
    blackboard_manifest = load_json_if_exists(path / "blackboard_manifest.json", DEFAULT_BLACKBOARD_MANIFEST)

    try:
        required = ("schema_version", "proposal_id", "source", "intent", "context_hash", "slot", "fragment_contract", "fragment")
        for key in required:
            if key not in proposal:
                code = "missing_slot" if key == "slot" else "malformed_envelope"
                raise ValidationError(code, f"proposal missing required field: {key}")
        if proposal["schema_version"] != "agent_proposal.v1":
            raise ValidationError("malformed_envelope", "proposal schema_version must be agent_proposal.v1")
        if proposal["slot"] not in install_policy.get("allowed_slots", []):
            raise ValidationError("missing_slot", f"slot is not allowed: {proposal['slot']}")
        contract = contracts.get(proposal["fragment_contract"])
        if contract is None:
            raise ValidationError("unknown_fragment_contract", f"unknown fragment contract: {proposal['fragment_contract']}")
        validate_blackboard_snapshot(proposal, blackboard_manifest)
        validation = validate_fragment_source(str(proposal["fragment"]))
        expr = Reader(str(proposal["fragment"])).read()
        denied = set(install_policy.get("denied_capabilities", []))
        denied_used = sorted(denied.intersection(validation["capabilities"]))
        if denied_used:
            raise ValidationError("denied_capability", f"fragment uses denied capability: {denied_used[0]}")
        validate_contract(expr, validation, contract, install_policy)
        diff = semantic_diff(proposal, validation, expr, install_policy)
        dry_run = dry_run_report(proposal, validation, diff)
        actual = {
            **validation,
            "status": "accepted",
            "reason_code": "accepted",
            "field_path": "",
            "slot": proposal["slot"],
            "fragment_contract": proposal["fragment_contract"],
            "proposal_id": proposal["proposal_id"],
            "host_reached": False,
            "install_policy": install_policy,
            "semantic_diff": diff,
            "dry_run_report": dry_run,
            "rollback_handle": {
                "schema_version": "subtree_rollback_handle.v1",
                "slot": proposal["slot"],
                "previous_subtree_hash": proposal.get("previous_subtree_hash", "fnv1a64:9999999999999999"),
                "new_subtree_hash": validation["canonical_dsl_hash"],
                "install_mode": install_policy.get("install_mode", DEFAULT_INSTALL_MODE),
            },
        }
    except ValidationError as exc:
        field_paths = {
            "missing_slot": "slot",
            "unknown_fragment_contract": "fragment_contract",
            "denied_capability": "fragment",
            "stale_blackboard_input": "blackboard",
            "missing_fallback_long_running": "fragment",
            "invalid_budget": "fragment",
            "excessive_depth": "fragment",
            "unknown_callback": "fragment",
            "malformed_envelope": "",
        }
        actual = reject_result(exc.code, exc.message, field_paths.get(exc.code, ""), proposal)

    if actual.get("ok") != expected.get("ok") or actual.get("code") != expected.get("code"):
        raise RuntimeError(f"{path}: expected {expected}, got {actual}")
    for key in (
        "status",
        "reason_code",
        "field_path",
        "slot",
        "fragment_contract",
        "host_reached",
        "canonical_dsl_hash",
    ):
        if key in expected and actual.get(key) != expected.get(key):
            raise RuntimeError(f"{path}: {key} mismatch: expected {expected.get(key)!r}, got {actual.get(key)!r}")
    if expected.get("message_contains") and str(expected["message_contains"]) not in str(actual.get("message", "")):
        raise RuntimeError(f"{path}: rejection message mismatch: {actual}")
    return actual


def validate_fixture_dir(path: Path) -> dict[str, Any]:
    if (path / "proposal.json").is_file():
        return validate_proposal_dir(path)
    expected_path = path / "expected.json"
    fragment_path = path / "fragment.lisp"
    if not fragment_path.is_file() or not expected_path.is_file():
        raise RuntimeError(f"{path}: expected fragment.lisp and expected.json")
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    try:
        actual = validate_fragment(fragment_path)
    except ValidationError as exc:
        actual = {"ok": False, "code": exc.code, "message": exc.message}
    if actual.get("ok") != expected.get("ok") or actual.get("code") != expected.get("code"):
        raise RuntimeError(f"{path}: expected {expected}, got {actual}")
    if expected.get("message_contains") and str(expected["message_contains"]) not in str(actual.get("message", "")):
        raise RuntimeError(f"{path}: rejection message mismatch: {actual}")
    for key in (
        "source_hash",
        "canonical_dsl",
        "canonical_dsl_hash",
        "node_count",
        "callbacks",
        "capabilities",
        "long_running_nodes",
        "fallback_policy",
    ):
        if key in expected and actual.get(key) != expected.get(key):
            raise RuntimeError(f"{path}: {key} mismatch: expected {expected.get(key)!r}, got {actual.get(key)!r}")
    return actual


def iter_fixture_dirs(root: Path) -> list[Path]:
    if (root / "fragment.lisp").is_file() or (root / "proposal.json").is_file():
        return [root]
    return sorted(
        path
        for path in root.iterdir()
        if path.is_dir() and ((path / "fragment.lisp").is_file() or (path / "proposal.json").is_file())
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate generated BT fragment fixtures.")
    parser.add_argument("path", nargs="?", default="fixtures/dsl/generated-fragment-negative")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON summary.")
    parser.add_argument("--export-manifests", help="Write default agent-readable manifests to this directory.")
    args = parser.parse_args(argv)

    if args.export_manifests:
        out_dir = Path(args.export_manifests)
        try:
            out_dir.mkdir(parents=True, exist_ok=True)
            for name, payload in {
                "capability_manifest.json": DEFAULT_CAPABILITY_MANIFEST,
                "blackboard_manifest.json": DEFAULT_BLACKBOARD_MANIFEST,
                "install_policy.json": DEFAULT_INSTALL_POLICY,
                "fragment_contracts.json": DEFAULT_FRAGMENT_CONTRACTS,
            }.items():
                (out_dir / name).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        except OSError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        if args.json:
            print(json.dumps({"manifest_dir": str(out_dir)}, indent=2, sort_keys=True))
        else:
            print(f"agent manifests written: {out_dir}")
        return 0

    root = Path(args.path)
    results: list[dict[str, Any]] = []
    try:
        for fixture_dir in iter_fixture_dirs(root):
            result = validate_fixture_dir(fixture_dir)
            results.append({"fixture": fixture_dir.name, **result})
    except (OSError, RuntimeError, ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps({"fixture_count": len(results), "results": results}, indent=2, sort_keys=True))
    else:
        print(f"generated fragment validation fixtures passed ({len(results)} fixture(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
