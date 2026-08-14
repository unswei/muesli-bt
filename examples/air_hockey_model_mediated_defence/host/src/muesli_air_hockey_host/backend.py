"""Deterministic, MuJoCo-free implementation of the direct-launch host contract."""

from __future__ import annotations

import threading
from dataclasses import asdict, dataclass, replace
from typing import Any, ClassVar

RESPONSE_SCHEMA = "airhockey.host.response.v1"


@dataclass(frozen=True)
class HostConfiguration:
    """Scenario controls needed to test the protocol state machine."""

    blackout_start_step: int = 8
    blackout_length_steps: int = 4
    timeout_steps: int = 40
    action_lock_steps: int = 0
    replace_track_steps: tuple[int, ...] = ()
    terminate_at_step: int | None = None

    def merged(self, payload: dict[str, Any]) -> HostConfiguration:
        values = asdict(self)
        values.update(payload)
        values["replace_track_steps"] = tuple(values["replace_track_steps"])
        candidate = replace(self, **values)
        candidate.validate()
        return candidate

    def validate(self) -> None:
        blackout_end = self.blackout_start_step + self.blackout_length_steps
        if blackout_end > self.timeout_steps:
            raise ValueError("blackout must end no later than timeout_steps")
        if self.action_lock_steps > self.timeout_steps:
            raise ValueError("action_lock_steps must not exceed timeout_steps")
        if any(step > self.timeout_steps for step in self.replace_track_steps):
            raise ValueError("replace_track_steps must not exceed timeout_steps")
        if (
            self.terminate_at_step is not None
            and self.terminate_at_step > self.timeout_steps
        ):
            raise ValueError("terminate_at_step must not exceed timeout_steps")

    def public_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["replace_track_steps"] = list(self.replace_track_steps)
        return result


class HostOperationError(RuntimeError):
    """A stable semantic error that may cross the socket boundary."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


class FakeDirectLaunchBackend:
    """Pure state-machine backend with deterministic public observations."""

    OPERATIONS: ClassVar[tuple[str, ...]] = (
        "info",
        "configure",
        "reset",
        "observe",
        "act",
        "step",
        "close",
    )

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._configuration = HostConfiguration()
        self._episode_number = 0
        self._track_number = 0
        self._step = 0
        self._seed = 0
        self._mallet = [0.0, 0.0]
        self._pending_action: list[float] | None = None
        self._previous_visible = True
        self._episode_started = False
        self._terminated = False
        self._truncated = False
        self._closed = False

    def handle(self, request: dict[str, Any]) -> dict[str, Any]:
        """Apply one already schema-validated request."""

        with self._lock:
            operation = request["op"]
            request_id = request["request_id"]
            if self._closed and operation != "close":
                return self._error(
                    request_id, operation, "host_closed", "host is closed"
                )
            try:
                result = self._dispatch(operation, request["payload"])
            except HostOperationError as error:
                return self._error(request_id, operation, error.code, error.message)
            except ValueError as error:
                return self._error(
                    request_id,
                    operation,
                    "invalid_configuration",
                    str(error),
                )
            return {
                "schema_version": RESPONSE_SCHEMA,
                "request_id": request_id,
                "op": operation,
                "ok": True,
                "result": result,
            }

    def _dispatch(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        if operation == "info":
            return self._info()
        if operation == "configure":
            return self._configure(payload)
        if operation == "reset":
            return self._reset(payload)
        if operation == "observe":
            self._require_episode()
            return {"state": self._public_state()}
        if operation == "act":
            return self._act(payload)
        if operation == "step":
            return self._advance()
        if operation == "close":
            self._closed = True
            self._episode_started = False
            self._pending_action = None
            return {"closed": True}
        raise AssertionError(f"schema admitted unsupported operation: {operation}")

    @staticmethod
    def _info() -> dict[str, Any]:
        return {
            "protocol_version": "airhockey.host.v1",
            "backend": "fake_direct_launch",
            "operations": list(FakeDirectLaunchBackend.OPERATIONS),
            "observation": {
                "schema": "airhockey.public_observation.v1",
                "dimension": 19,
            },
            "action": {
                "schema": "airhockey.normalised_mallet_target.v1",
                "dimension": 2,
                "minimum": -1.0,
                "maximum": 1.0,
            },
            "control_period_ms": 20,
            "max_deadline_ms": 120,
            "privileged_fields_available": False,
        }

    def _configure(self, payload: dict[str, Any]) -> dict[str, Any]:
        if self._episode_started:
            raise HostOperationError(
                "episode_active",
                "configure is only valid before reset or after an episode completes",
            )
        self._configuration = self._configuration.merged(payload)
        return {"configuration": self._configuration.public_dict()}

    def _reset(self, payload: dict[str, Any]) -> dict[str, Any]:
        self._episode_number += 1
        self._track_number = 1
        self._step = 0
        self._seed = payload.get("seed", 0)
        self._mallet = [0.0, 0.0]
        self._pending_action = None
        self._episode_started = True
        self._terminated = False
        self._truncated = False
        self._previous_visible = self._puck_visible()
        return {"state": self._public_state()}

    def _act(self, payload: dict[str, Any]) -> dict[str, Any]:
        self._require_active_episode()
        self._pending_action = [float(value) for value in payload["action"]]
        return {
            "accepted": True,
            "requested_action": list(self._pending_action),
            "action_locked": self._action_locked(),
        }

    def _advance(self) -> dict[str, Any]:
        self._require_active_episode()
        if self._pending_action is None:
            raise HostOperationError(
                "action_required",
                "act must be accepted before each step",
            )

        if not self._action_locked():
            self._mallet = list(self._pending_action)
        self._pending_action = None
        self._step += 1

        visible = self._puck_visible()
        reacquired = not self._previous_visible and visible
        replaced = self._step in self._configuration.replace_track_steps
        if reacquired or replaced:
            self._track_number += 1
        self._previous_visible = visible

        self._terminated = self._configuration.terminate_at_step == self._step
        self._truncated = (
            not self._terminated and self._step >= self._configuration.timeout_steps
        )
        if self._terminated or self._truncated:
            self._episode_started = False

        return {"state": self._public_state(), "reward": 0.0}

    def _require_episode(self) -> None:
        if self._episode_number == 0:
            raise HostOperationError(
                "episode_not_started", "reset must be called first"
            )

    def _require_active_episode(self) -> None:
        self._require_episode()
        if not self._episode_started:
            raise HostOperationError(
                "episode_complete",
                "episode is complete; call reset before act or step",
            )

    def _action_locked(self) -> bool:
        return (
            self._episode_started and self._step < self._configuration.action_lock_steps
        )

    def _puck_visible(self) -> bool:
        start = self._configuration.blackout_start_step
        end = start + self._configuration.blackout_length_steps
        return not (start <= self._step < end)

    def _public_state(self) -> dict[str, Any]:
        visible = self._puck_visible()
        joint_positions = [
            ((self._step + 2 * index) % 21 - 10) / 10.0 for index in range(7)
        ]
        joint_velocities = [
            ((3 * self._step + index) % 11 - 5) / 10.0 for index in range(7)
        ]
        puck = [
            max(-1.0, 0.8 - 0.04 * self._step),
            ((self._step % 9) - 4) / 10.0,
        ]
        if not visible:
            puck = [0.0, 0.0]
        observation = (
            joint_positions
            + joint_velocities
            + list(self._mallet)
            + puck
            + [1.0 if visible else 0.0]
        )
        episode_id = f"episode-{self._episode_number:06d}"
        return {
            "observation_schema": "airhockey.public_observation.v1",
            "observation": observation,
            "observation_step": self._step,
            "puck_visible": visible,
            "action_locked": self._action_locked(),
            "episode_active": self._episode_started,
            "terminated": self._terminated,
            "truncated": self._truncated,
            "defence_context_id": f"{episode_id}/track-{self._track_number:04d}",
            "episode_id": episode_id,
        }

    @staticmethod
    def _error(
        request_id: str, operation: str, code: str, message: str
    ) -> dict[str, Any]:
        return {
            "schema_version": RESPONSE_SCHEMA,
            "request_id": request_id,
            "op": operation,
            "ok": False,
            "error": {"code": code, "message": message},
        }
