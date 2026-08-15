"""MuJoCo-free provider startup and MMSP compatibility harness."""

from __future__ import annotations

import json
from collections.abc import Callable
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from .adapters import ProviderError, provider_response

CAPABILITY = "cap.vla.action_chunk.v1"


def capability_descriptor() -> dict[str, Any]:
    """Return the descriptor fields checked by the muesli MMSP v0.2 bridge."""

    return {
        "id": CAPABILITY,
        "mode": "session",
        "input_schema": "airhockey.provider.request.v1",
        "output_schema": "airhockey.provider.response.v1",
        "supports_cancel": True,
        "supports_deadline": True,
        "freshness": {
            "expects_fresh_observation": True,
            "maximum_source_age_steps": 6,
        },
        "replay": {"supported": True},
    }


def validate_capability_descriptor(
    descriptor: dict[str, Any], contract: dict[str, Any]
) -> None:
    """Mirror the C++ bridge's VLA descriptor compatibility requirements."""

    expected = contract["provider_contract"]
    required = {
        "id",
        "mode",
        "input_schema",
        "output_schema",
        "supports_cancel",
        "supports_deadline",
        "freshness",
        "replay",
    }
    if set(descriptor) != required:
        raise ProviderError("provider descriptor has unexpected fields")
    for field in ("capability", "mode", "input_schema", "output_schema"):
        descriptor_field = "id" if field == "capability" else field
        if descriptor[descriptor_field] != expected[field]:
            raise ProviderError(f"provider descriptor {descriptor_field} mismatch")
    if descriptor["supports_cancel"] is not True:
        raise ProviderError("provider capability must support cancellation")
    if descriptor["supports_deadline"] is not True:
        raise ProviderError("provider capability must support deadlines")
    if descriptor["freshness"] != {
        "expects_fresh_observation": True,
        "maximum_source_age_steps": expected["maximum_source_age_steps"],
    }:
        raise ProviderError("provider freshness declaration mismatch")
    if descriptor["replay"].get("supported") is not True:
        raise ProviderError("provider capability must declare replay support")


class MockProviderService:
    """Exercise provider lifecycle and schemas without sockets or MuJoCo."""

    def __init__(self, provider_factory: Callable[[], Any], schema_root: Path) -> None:
        self._provider_factory = provider_factory
        self._request_validator = self._validator(
            schema_root / "airhockey.provider.request.v1.schema.json"
        )
        self._response_validator = self._validator(
            schema_root / "airhockey.provider.response.v1.schema.json"
        )
        self._sessions: dict[str, Any] = {}
        self._next_session = 1

    @staticmethod
    def _validator(path: Path) -> Draft202012Validator:
        schema = json.loads(path.read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        return Draft202012Validator(schema)

    def describe(self) -> dict[str, Any]:
        return {
            "version": "0.2",
            "status": "success",
            "capabilities": [capability_descriptor()],
        }

    def start(self, request: dict[str, Any]) -> str:
        self._request_validator.validate(request)
        provider = self._provider_factory()
        provider.reset()
        session_id = f"air-hockey-session-{self._next_session:04d}"
        self._next_session += 1
        self._sessions[session_id] = provider
        return session_id

    def step(self, session_id: str, request: dict[str, Any]) -> dict[str, Any]:
        self._request_validator.validate(request)
        provider = self._sessions.get(session_id)
        if provider is None:
            raise ProviderError("provider session is not active")
        response = provider_response(request, provider.infer(request["observation"]))
        self._response_validator.validate(response)
        return response

    def cancel(self, session_id: str) -> None:
        if self._sessions.pop(session_id, None) is None:
            raise ProviderError("provider session is not active")

    def close(self, session_id: str) -> None:
        self.cancel(session_id)
