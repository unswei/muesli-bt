"""Fail-closed JSON framing and schema validation for the air-hockey host."""

from __future__ import annotations

import json
import math
import re
from pathlib import Path
from typing import Any, Protocol

from .backend import RESPONSE_SCHEMA

MAX_REQUEST_BYTES = 32 * 1024
_REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9._:-]{1,64}$")
_OPERATIONS = {"info", "configure", "reset", "observe", "act", "step", "close"}


class ProtocolValidationError(ValueError):
    """A schema validation failure with a deterministic message."""


class HostBackend(Protocol):
    """Structural interface implemented by fake and future simulator hosts."""

    def handle(self, request: dict[str, Any]) -> dict[str, Any]: ...


class SchemaRegistry:
    """Load and validate both sides of the versioned wire contract."""

    def __init__(self, schema_directory: Path) -> None:
        try:
            from jsonschema import Draft202012Validator
        except ModuleNotFoundError as error:
            raise RuntimeError(
                "jsonschema is required by the air-hockey host; install the Python test dependencies"
            ) from error

        request_path = schema_directory / "airhockey.host.request.v1.schema.json"
        response_path = schema_directory / "airhockey.host.response.v1.schema.json"
        request_schema = json.loads(request_path.read_text(encoding="utf-8"))
        response_schema = json.loads(response_path.read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(request_schema)
        Draft202012Validator.check_schema(response_schema)
        self._request_validator = Draft202012Validator(request_schema)
        self._response_validator = Draft202012Validator(response_schema)

    def validate_request(self, value: Any) -> None:
        self._validate(self._request_validator, value)

    def validate_response(self, value: Any) -> None:
        self._validate(self._response_validator, value)

    @staticmethod
    def _validate(validator: Any, value: Any) -> None:
        if SchemaRegistry._contains_non_finite(value):
            raise ProtocolValidationError("$: non-finite numbers are not valid JSON")
        errors = sorted(
            validator.iter_errors(value),
            key=lambda error: (
                tuple(str(part) for part in error.absolute_path),
                error.message,
            ),
        )
        if errors:
            error = errors[0]
            path = ".".join(str(part) for part in error.absolute_path) or "$"
            raise ProtocolValidationError(f"{path}: {error.message}")

    @staticmethod
    def _contains_non_finite(value: Any) -> bool:
        if isinstance(value, float):
            return not math.isfinite(value)
        if isinstance(value, dict):
            return any(
                SchemaRegistry._contains_non_finite(item) for item in value.values()
            )
        if isinstance(value, (list, tuple)):
            return any(SchemaRegistry._contains_non_finite(item) for item in value)
        return False


class ProtocolProcessor:
    """Turn one bounded JSON request into one canonical JSON response."""

    def __init__(self, schemas: SchemaRegistry, backend: HostBackend) -> None:
        self._schemas = schemas
        self._backend = backend

    def process(self, raw: bytes) -> bytes:
        if len(raw) > MAX_REQUEST_BYTES:
            return self._encode(
                self._error(
                    "unknown",
                    "error",
                    "request_too_large",
                    "request exceeds 32768 bytes",
                )
            )
        try:
            request = json.loads(
                raw.decode("utf-8"),
                object_pairs_hook=self._reject_duplicate_keys,
                parse_constant=self._reject_non_finite,
            )
        except (UnicodeDecodeError, json.JSONDecodeError, RecursionError, ValueError):
            return self._encode(
                self._error(
                    "unknown", "error", "invalid_json", "request is not strict JSON"
                )
            )

        request_id, operation = self._safe_identity(request)
        try:
            self._schemas.validate_request(request)
        except (ProtocolValidationError, RecursionError) as error:
            return self._encode(
                self._error(request_id, operation, "invalid_schema", str(error))
            )

        try:
            response = self._backend.handle(request)
            self._schemas.validate_response(response)
        except Exception:  # noqa: BLE001 - invalid backend output must fail closed.
            response = self._error(
                request_id,
                operation,
                "internal_error",
                "host could not produce a valid response",
            )
            self._schemas.validate_response(response)
        return self._encode(response)

    def _encode(self, response: dict[str, Any]) -> bytes:
        self._schemas.validate_response(response)
        return (
            json.dumps(
                response,
                allow_nan=False,
                separators=(",", ":"),
                sort_keys=True,
            ).encode("utf-8")
            + b"\n"
        )

    @staticmethod
    def _safe_identity(request: Any) -> tuple[str, str]:
        if not isinstance(request, dict):
            return "unknown", "error"
        request_id = request.get("request_id")
        operation = request.get("op")
        safe_id = (
            request_id
            if isinstance(request_id, str) and _REQUEST_ID_PATTERN.fullmatch(request_id)
            else "unknown"
        )
        safe_operation = (
            operation
            if isinstance(operation, str) and operation in _OPERATIONS
            else "error"
        )
        return safe_id, safe_operation

    @staticmethod
    def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate key: {key}")
            result[key] = value
        return result

    @staticmethod
    def _reject_non_finite(value: str) -> None:
        raise ValueError(f"non-finite number: {value}")

    @staticmethod
    def _error(
        request_id: str, operation: str, code: str, message: str
    ) -> dict[str, Any]:
        return {
            "schema_version": RESPONSE_SCHEMA,
            "request_id": request_id,
            "op": operation,
            "ok": False,
            "error": {"code": code, "message": message[:256]},
        }
