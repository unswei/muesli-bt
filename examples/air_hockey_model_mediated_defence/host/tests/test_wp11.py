"""Pure and transport checks for the frozen WP11 live-provider study."""

from __future__ import annotations

import sys
import threading
import unittest
from pathlib import Path
from typing import Any

EXAMPLE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(EXAMPLE_ROOT))

from provider.live_dreamer_service import (
    PROTOCOL_VERSION,
    LiveProviderClient,
    ThreadedProviderServer,
)
from run_wp11 import (
    PublicDisplacementContext,
    load_protocol,
    replay_capture,
)


class _FakeProviderRuntime:
    def __init__(self) -> None:
        self.sessions: set[str] = set()

    def reset(self, session_id: str) -> None:
        self.sessions.add(session_id)

    def infer(self, request: dict[str, Any]) -> list[float]:
        if request["session_id"] not in self.sessions:
            raise KeyError("missing session")
        return [float(request["observation"][16]), float(request["observation"][17])]


def _observation(x: float, y: float) -> list[float]:
    return [0.0] * 16 + [x, y, 1.0]


class WP11Test(unittest.TestCase):
    def test_protocol_is_frozen_without_injected_timing_or_scene_changes(self) -> None:
        protocol = load_protocol()
        self.assertFalse(protocol["provider"]["latency_injection"])
        self.assertEqual(protocol["workload"]["scene_change_schedule"], "none")
        self.assertEqual(protocol["workload"]["blackout_length_steps"], 0)

    def test_public_context_changes_only_above_the_frozen_threshold(self) -> None:
        context = PublicDisplacementContext("episode-1", 0.1)
        self.assertFalse(context.update(_observation(0.0, 0.0)))
        self.assertFalse(context.update(_observation(0.06, 0.08)))
        self.assertEqual(context.context_id, "episode-1/context-0001")
        self.assertTrue(context.update(_observation(0.061, 0.08)))
        self.assertEqual(context.context_id, "episode-1/context-0002")

    def test_tcp_service_records_real_queue_and_inference_boundaries(self) -> None:
        server = ThreadedProviderServer(("127.0.0.1", 0), _FakeProviderRuntime())
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        client = LiveProviderClient(host, port)
        try:
            self.assertTrue(client.reset("session-1", "reset-1")["ok"])
            response = client.call(
                {
                    "protocol_version": PROTOCOL_VERSION,
                    "op": "infer",
                    "request_id": "request-1",
                    "session_id": "session-1",
                    "observation": _observation(0.25, -0.4),
                    "reward": 0.0,
                    "is_first": True,
                }
            )
            self.assertTrue(response["ok"])
            self.assertEqual(response["action"], [0.25, -0.4])
            self.assertLessEqual(
                response["server_received_monotonic_ns"],
                response["server_started_monotonic_ns"],
            )
            self.assertLessEqual(
                response["server_started_monotonic_ns"],
                response["server_finished_monotonic_ns"],
            )
            self.assertLessEqual(
                response["client_sent_monotonic_ns"],
                response["client_received_monotonic_ns"],
            )
        finally:
            client.close()
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)

    def test_replay_separates_pre_and_post_admission_authority_loss(self) -> None:
        protocol = load_protocol()
        timeline = [
            {
                "session_id": "episode-0001",
                "observation_step": step,
                "monotonic_ns": timestamp,
                "context_id": context,
                "episode_active": True,
            }
            for step, timestamp, context in (
                (0, 0, "c1"),
                (1, 20_000_000, "c1"),
                (2, 40_000_000, "c2"),
                (3, 60_000_000, "c2"),
                (4, 80_000_000, "c2"),
            )
        ]

        def record(
            request_id: str, delivery_ns: int, context: str, generation: int
        ) -> dict[str, Any]:
            response = {
                "ok": True,
                "request_id": request_id,
                "action": [0.1, -0.2],
                "client_received_monotonic_ns": delivery_ns,
                "server_received_monotonic_ns": delivery_ns - 3_000_000,
                "server_started_monotonic_ns": delivery_ns - 2_000_000,
                "server_finished_monotonic_ns": delivery_ns - 1_000_000,
            }
            return {
                "request_id": request_id,
                "session_id": "episode-0001",
                "generation": generation,
                "request_created_monotonic_ns": 0,
                "captured_context_id": context,
                "source_observation_step": 0,
                "record_sha256": request_id,
                "response": response,
            }

        capture = {
            "context_timeline": timeline,
            "provider_records": [
                record("post-admission", 10_000_000, "c1", 1),
                record("pre-admission", 30_000_000, "c1", 2),
                record("no-change", 41_000_000, "c2", 3),
            ],
        }
        replay = replay_capture(capture, protocol)
        self.assertEqual(replay["natural_authority_loss"]["before_admission"], 1)
        self.assertEqual(replay["natural_authority_loss"]["after_admission"], 1)
        self.assertEqual(replay["policies"]["deadline_only"]["obsolete_dispatches"], 2)
        self.assertEqual(
            replay["policies"]["invocation_scoped_admission_only"][
                "obsolete_dispatches"
            ],
            1,
        )
        self.assertEqual(
            replay["policies"]["invocation_scoped_two_gate"]["obsolete_dispatches"],
            0,
        )
        self.assertEqual(
            replay["policies"]["invocation_scoped_two_gate"][
                "valid_no_change_dispatches"
            ],
            1,
        )


if __name__ == "__main__":
    unittest.main()
