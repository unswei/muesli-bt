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
from pathlib import Path
from typing import Any


KNOWN_COMPOSITES = {"seq", "sel", "mem-seq", "mem-sel", "async-seq", "reactive-seq", "reactive-sel"}
KNOWN_DECORATORS = {"invert"}
KNOWN_COUNT_DECORATORS = {"repeat", "retry"}
KNOWN_LEAVES = {"cond", "act"}
KNOWN_CONSTANTS = {"succeed", "fail", "running"}
KNOWN_LONG_RUNNING = {"plan-action", "vla-request", "vla-wait"}
KNOWN_NODE_TYPES = KNOWN_COMPOSITES | KNOWN_DECORATORS | KNOWN_COUNT_DECORATORS | KNOWN_LEAVES | KNOWN_CONSTANTS | {
    "plan-action",
    "vla-request",
    "vla-wait",
    "vla-cancel",
}

KNOWN_CALLBACKS = {
    "always-true",
    "always-false",
    "always-success",
    "always-fail",
    "bb-has",
    "bb-put-int",
    "async-sleep-ms",
    "stop",
    "safe-stop",
    "move-towards-goal",
}
KNOWN_CAPABILITIES = {"vla.rt2", "vision", "planner.mcts", "env.move"}
FALLBACK_ACTIONS = {"stop", "safe-stop", "always-success"}


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
        try:
            return int(token)
        except ValueError:
            try:
                return float(token)
            except ValueError:
                return Symbol(token)


def sym_name(value: Any) -> str | None:
    return value.name if isinstance(value, Symbol) else None


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

    options = key_values(expr, 1, form_name)
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
    expr = Reader(source).read()
    validate_shape(expr)
    if contains_long_running(expr) and not long_running_has_fallback(expr):
        raise ValidationError(
            "missing_fallback_long_running",
            "long-running planner/model fragment must be guarded by an explicit fallback branch",
        )
    return {"ok": True, "code": "accepted", "message": "fragment accepted"}


def validate_fixture_dir(path: Path) -> dict[str, Any]:
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
    return actual


def iter_fixture_dirs(root: Path) -> list[Path]:
    if (root / "fragment.lisp").is_file():
        return [root]
    return sorted(path for path in root.iterdir() if path.is_dir() and (path / "fragment.lisp").is_file())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate generated BT fragment fixtures.")
    parser.add_argument("path", nargs="?", default="fixtures/dsl/generated-fragment-negative")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON summary.")
    args = parser.parse_args(argv)

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
