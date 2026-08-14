"""WP1 contract tests for the MuJoCo-free air-hockey host."""

from __future__ import annotations

import json
import socket
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
HOST_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST_ROOT / "src"))

from muesli_air_hockey_host import (
    FakeDirectLaunchBackend,
    ProtocolProcessor,
    SchemaRegistry,
    UnixHostServer,
)
from muesli_air_hockey_host.protocol import (
    MAX_REQUEST_BYTES,
    ProtocolValidationError,
)

SCHEMA_DIRECTORY = REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1"
REQUEST_SCHEMA = "airhockey.host.request.v1"
PROHIBITED_KEYS = {
    "alias_family_id",
    "outcome",
    "privileged_puck_position",
    "privileged_puck_velocity",
    "shot_id",
    "target_label",
    "target_region",
    "true_puck_position",
    "true_puck_velocity",
}


def request(
    request_id: str, operation: str, payload: dict[str, Any] | None = None
) -> bytes:
    value = {
        "schema_version": REQUEST_SCHEMA,
        "request_id": request_id,
        "op": operation,
        "payload": payload or {},
    }
    return json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")


def decode(response: bytes) -> dict[str, Any]:
    return json.loads(response.decode("utf-8"))


def all_keys(value: Any) -> set[str]:
    if isinstance(value, dict):
        return set(value).union(*(all_keys(item) for item in value.values()))
    if isinstance(value, list):
        return set().union(*(all_keys(item) for item in value))
    return set()


class HostContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.schemas = SchemaRegistry(SCHEMA_DIRECTORY)
        self.processor = ProtocolProcessor(self.schemas, FakeDirectLaunchBackend())

    def exchange(
        self,
        request_id: str,
        operation: str,
        payload: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        return decode(self.processor.process(request(request_id, operation, payload)))

    def act_and_step(
        self, sequence: int, action: list[float] | None = None
    ) -> dict[str, Any]:
        action_response = self.exchange(
            f"act-{sequence}",
            "act",
            {"action": action or [0.0, 0.0]},
        )
        self.assertTrue(action_response["ok"])
        step_response = self.exchange(f"step-{sequence}", "step")
        self.assertTrue(step_response["ok"])
        return step_response["result"]["state"]

    def test_info_reset_and_public_observation_shape(self) -> None:
        info = self.exchange("info-1", "info")
        self.assertTrue(info["ok"])
        self.assertEqual(info["result"]["observation"]["dimension"], 19)
        self.assertEqual(info["result"]["action"]["dimension"], 2)
        self.assertFalse(info["result"]["privileged_fields_available"])

        reset = self.exchange("reset-1", "reset", {"seed": 6302})
        state = reset["result"]["state"]
        self.assertEqual(len(state["observation"]), 19)
        self.assertTrue(all(-1.0 <= value <= 1.0 for value in state["observation"]))
        self.assertEqual(state["observation_step"], 0)
        self.assertEqual(state["defence_context_id"], "episode-000001/track-0001")
        self.assertFalse(PROHIBITED_KEYS & all_keys(reset))

    def test_blackout_onset_preserves_context_and_reacquisition_replaces_it(
        self,
    ) -> None:
        configured = self.exchange(
            "configure",
            "configure",
            {
                "blackout_start_step": 2,
                "blackout_length_steps": 2,
                "timeout_steps": 8,
                "replace_track_steps": [5],
            },
        )
        self.assertTrue(configured["ok"])
        reset = self.exchange("reset", "reset")
        initial = reset["result"]["state"]
        first_context = initial["defence_context_id"]

        step_one = self.act_and_step(1)
        blackout_onset = self.act_and_step(2)
        blackout_continues = self.act_and_step(3)
        reacquired = self.act_and_step(4)
        replaced = self.act_and_step(5)

        self.assertTrue(step_one["puck_visible"])
        self.assertFalse(blackout_onset["puck_visible"])
        self.assertFalse(blackout_continues["puck_visible"])
        self.assertEqual(blackout_onset["defence_context_id"], first_context)
        self.assertEqual(blackout_continues["defence_context_id"], first_context)
        self.assertTrue(reacquired["puck_visible"])
        self.assertEqual(reacquired["defence_context_id"], "episode-000001/track-0002")
        self.assertEqual(replaced["defence_context_id"], "episode-000001/track-0003")

    def test_action_lock_holds_current_mallet_then_applies_bounded_action(self) -> None:
        self.exchange(
            "configure",
            "configure",
            {
                "blackout_start_step": 0,
                "blackout_length_steps": 0,
                "timeout_steps": 4,
                "action_lock_steps": 2,
            },
        )
        self.exchange("reset", "reset")
        locked_one = self.act_and_step(1, [1.0, -1.0])
        locked_two = self.act_and_step(2, [0.8, -0.8])
        applied = self.act_and_step(3, [-0.5, 0.75])

        self.assertEqual(locked_one["observation"][14:16], [0.0, 0.0])
        self.assertEqual(locked_two["observation"][14:16], [0.0, 0.0])
        self.assertEqual(applied["observation"][14:16], [-0.5, 0.75])

    def test_terminal_state_is_observable_and_requires_reset(self) -> None:
        self.exchange(
            "configure",
            "configure",
            {
                "blackout_start_step": 0,
                "blackout_length_steps": 0,
                "timeout_steps": 3,
                "terminate_at_step": 2,
            },
        )
        self.exchange("reset-1", "reset")
        self.act_and_step(1)
        terminal = self.act_and_step(2)
        self.assertTrue(terminal["terminated"])
        self.assertFalse(terminal["truncated"])
        self.assertFalse(terminal["episode_active"])

        rejected = self.exchange("act-after-terminal", "act", {"action": [0.0, 0.0]})
        self.assertFalse(rejected["ok"])
        self.assertEqual(rejected["error"]["code"], "episode_complete")
        observed = self.exchange("observe-terminal", "observe")
        self.assertTrue(observed["result"]["state"]["terminated"])
        reset = self.exchange("reset-2", "reset")
        self.assertEqual(
            reset["result"]["state"]["defence_context_id"],
            "episode-000002/track-0001",
        )

    def test_timeout_truncates_and_step_requires_one_action(self) -> None:
        self.exchange(
            "configure",
            "configure",
            {
                "blackout_start_step": 0,
                "blackout_length_steps": 0,
                "timeout_steps": 2,
            },
        )
        self.exchange("reset", "reset")
        missing_action = self.exchange("step-without-act", "step")
        self.assertEqual(missing_action["error"]["code"], "action_required")
        self.act_and_step(1)
        truncated = self.act_and_step(2)
        self.assertTrue(truncated["truncated"])
        self.assertFalse(truncated["terminated"])

    def test_schema_rejects_action_shape_bounds_and_privileged_extension(self) -> None:
        self.exchange("reset", "reset")
        too_short = self.exchange("short", "act", {"action": [0.0]})
        out_of_bounds = self.exchange("bounds", "act", {"action": [1.01, 0.0]})
        leaked = self.exchange(
            "leak",
            "configure",
            {"target_region": "left"},
        )
        self.assertEqual(too_short["error"]["code"], "invalid_schema")
        self.assertEqual(out_of_bounds["error"]["code"], "invalid_schema")
        self.assertEqual(leaked["error"]["code"], "invalid_schema")

    def test_parser_rejects_malformed_duplicate_non_finite_and_oversized_json(
        self,
    ) -> None:
        malformed = decode(self.processor.process(b"{"))
        duplicate = decode(
            self.processor.process(
                b'{"schema_version":"airhockey.host.request.v1","request_id":"x",'
                b'"request_id":"y","op":"info","payload":{}}'
            )
        )
        non_finite = decode(
            self.processor.process(
                b'{"schema_version":"airhockey.host.request.v1","request_id":"x",'
                b'"op":"act","payload":{"action":[NaN,0]}}'
            )
        )
        unhashable_operation = decode(
            self.processor.process(
                b'{"schema_version":"airhockey.host.request.v1","request_id":"x",'
                b'"op":[],"payload":{}}'
            )
        )
        oversized = decode(self.processor.process(b" " * (MAX_REQUEST_BYTES + 1)))
        self.assertEqual(malformed["error"]["code"], "invalid_json")
        self.assertEqual(duplicate["error"]["code"], "invalid_json")
        self.assertEqual(non_finite["error"]["code"], "invalid_json")
        self.assertEqual(unhashable_operation["error"]["code"], "invalid_schema")
        self.assertEqual(oversized["error"]["code"], "request_too_large")

    def test_response_schema_rejects_privileged_extension(self) -> None:
        response = self.exchange("reset", "reset")
        response["result"]["state"]["true_puck_position"] = [0.1, 0.2]
        with self.assertRaises(ProtocolValidationError):
            self.schemas.validate_response(response)

    def test_request_reply_replay_is_byte_identical(self) -> None:
        sequence = [
            request("01", "info"),
            request(
                "02",
                "configure",
                {
                    "blackout_start_step": 1,
                    "blackout_length_steps": 1,
                    "timeout_steps": 3,
                },
            ),
            request("03", "reset", {"seed": 6302}),
            request("04", "act", {"action": [0.25, -0.5]}),
            request("05", "step"),
            request("06", "observe"),
            request("07", "close"),
        ]

        def replay() -> list[bytes]:
            processor = ProtocolProcessor(self.schemas, FakeDirectLaunchBackend())
            return [processor.process(item) for item in sequence]

        self.assertEqual(replay(), replay())

    def test_invalid_backend_output_fails_closed(self) -> None:
        class InvalidBackend(FakeDirectLaunchBackend):
            def handle(self, request_value: dict[str, Any]) -> dict[str, Any]:
                return {"privileged_puck_position": [0.0, 0.0]}

        processor = ProtocolProcessor(self.schemas, InvalidBackend())
        response = decode(processor.process(request("bad-backend", "info")))
        self.assertEqual(response["error"]["code"], "internal_error")
        self.schemas.validate_response(response)

        class NonFiniteBackend(FakeDirectLaunchBackend):
            def handle(self, request_value: dict[str, Any]) -> dict[str, Any]:
                response_value = super().handle(request_value)
                response_value["result"]["control_period_ms"] = float("nan")
                return response_value

        processor = ProtocolProcessor(self.schemas, NonFiniteBackend())
        response = decode(processor.process(request("nan-backend", "info")))
        self.assertEqual(response["error"]["code"], "internal_error")
        self.schemas.validate_response(response)

    def test_close_is_idempotent_and_other_operations_fail_after_close(self) -> None:
        closed = self.exchange("close-1", "close")
        closed_again = self.exchange("close-2", "close")
        rejected = self.exchange("info-after-close", "info")
        self.assertTrue(closed["result"]["closed"])
        self.assertTrue(closed_again["result"]["closed"])
        self.assertEqual(rejected["error"]["code"], "host_closed")


class UnixSocketTest(unittest.TestCase):
    def setUp(self) -> None:
        schemas = SchemaRegistry(SCHEMA_DIRECTORY)
        self.processor = ProtocolProcessor(schemas, FakeDirectLaunchBackend())
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.socket_path = Path(self.temporary_directory.name) / "air-hockey.sock"
        self.server = UnixHostServer(self.socket_path, self.processor)

    def tearDown(self) -> None:
        self.server.stop()
        self.temporary_directory.cleanup()

    def socket_exchange(self, payload: bytes) -> dict[str, Any]:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(2.0)
            client.connect(str(self.socket_path))
            client.sendall(payload)
            client.shutdown(socket.SHUT_WR)
            response = b""
            while not response.endswith(b"\n"):
                chunk = client.recv(4096)
                if not chunk:
                    break
                response += chunk
        return decode(response)

    def test_socket_round_trip_permissions_and_partial_disconnect(self) -> None:
        self.server.start()
        self.assertEqual(stat.S_IMODE(self.socket_path.stat().st_mode), 0o600)
        partial = self.socket_exchange(b'{"schema_version":')
        self.assertEqual(partial["error"]["code"], "invalid_json")
        valid = self.socket_exchange(request("socket-info", "info") + b"\n")
        self.assertTrue(valid["ok"])
        self.assertEqual(valid["request_id"], "socket-info")

    def test_server_refuses_to_replace_non_socket_path(self) -> None:
        self.socket_path.write_text("do not replace", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "refuse to replace non-socket"):
            self.server.start()
        self.assertEqual(self.socket_path.read_text(encoding="utf-8"), "do not replace")


if __name__ == "__main__":
    unittest.main()
