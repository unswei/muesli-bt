"""Single-worker TCP service for the frozen Dreamer air-hockey teacher.

The service is intentionally narrow.  It accepts newline-delimited JSON over
localhost TCP, keeps one Dreamer policy carry per episode session, and records
receive, queue, inference, and delivery timestamps.  It does not implement an
authority policy and never calls a robot capability.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import queue
import socket
import socketserver
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

import numpy as np

from .adapters import validate_action, validate_observation

PROTOCOL_VERSION = "airhockey.live_provider.tcp.v1"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class ProviderRuntime(Protocol):
    """Minimal model runtime consumed by the transport."""

    def reset(self, session_id: str) -> None: ...

    def infer(self, request: dict[str, Any]) -> list[float]: ...


class DreamerProviderRuntime:
    """Load one hash-bound Dreamer checkpoint and retain per-session carry."""

    def __init__(
        self,
        *,
        checkpoint: Path,
        expected_agent_sha256: str,
        teacher_config: Path,
        reward_config: Path,
        distribution_config: Path,
        runtime_directory: Path,
        profile: str,
    ) -> None:
        checkpoint = checkpoint.resolve()
        teacher_config = teacher_config.resolve()
        reward_config = reward_config.resolve()
        distribution_config = distribution_config.resolve()
        agent_path = checkpoint / "agent.pkl"
        if not (checkpoint / "done").is_file() or not agent_path.is_file():
            raise FileNotFoundError(f"incomplete Dreamer checkpoint: {checkpoint}")
        if _sha256(agent_path) != expected_agent_sha256:
            raise ValueError("Dreamer agent checkpoint SHA-256 mismatch")

        import elements
        import gymnasium
        import portal
        from airhockey_distill.envs import DirectLaunchTrainingEnv
        from airhockey_distill.teachers import enable_deterministic_dreamer_inference
        from dreamerv3 import agent as dreamer_agent
        from dreamerv3 import main as dreamer_main
        from embodied.envs import from_gymnasium
        from embodied.jax import outs as embodied_outs
        from ruamel import yaml as ruamel_yaml

        environment_id = "MuesliAirHockeyLiveProvider-v0"

        def make_environment(**kwargs: Any) -> DirectLaunchTrainingEnv:
            return DirectLaunchTrainingEnv(
                distribution_config=distribution_config,
                reward_config=reward_config,
                split="engineering",
                sampling_seed=6302,
                minimum_blackout_steps=0,
                maximum_blackout_steps=0,
                action_lock_steps=5,
                **kwargs,
            )

        gymnasium.register(id=environment_id, entry_point=make_environment)
        enable_deterministic_dreamer_inference(embodied_outs.Agg)
        from_gymnasium.elements = elements

        teacher = ruamel_yaml.YAML(typ="safe").load(
            teacher_config.read_text(encoding="utf-8")
        )
        profile_config = teacher.get(profile)
        if not isinstance(profile_config, dict):
            raise TypeError(f"unknown Dreamer profile: {profile}")
        upstream = ruamel_yaml.YAML(typ="safe").load(
            (Path(dreamer_agent.__file__).parent / "configs.yaml").read_text(
                encoding="utf-8"
            )
        )
        config = elements.Config(upstream["defaults"])
        config = config.update(upstream[str(profile_config["model_preset"])])
        runtime_directory.mkdir(parents=True, exist_ok=True)
        config = config.update(
            {
                "task": f"gymnasium_{environment_id}",
                "logdir": str(runtime_directory),
                "seed": int(profile_config["seed"]),
                "batch_size": int(profile_config["batch_size"]),
                "batch_length": int(profile_config["batch_length"]),
                "report_length": int(profile_config["report_length"]),
                "jax.platform": "cuda",
                "jax.prealloc": False,
                "run.envs": 1,
                "run.debug": True,
            }
        )
        portal.setup(
            errfile=False,
            clientkw={"logging_color": "cyan"},
            serverkw={"logging_color": "cyan"},
            ipv6=False,
        )
        self._agent = dreamer_main.make_agent(config)
        loader = elements.Checkpoint()
        loader.agent = self._agent
        loader.load(checkpoint, keys=["agent"])
        self._sessions: dict[str, Any] = {}

    def reset(self, session_id: str) -> None:
        self._sessions[session_id] = self._agent.init_policy(1)

    def infer(self, request: dict[str, Any]) -> list[float]:
        session_id = str(request["session_id"])
        if session_id not in self._sessions:
            raise KeyError(f"provider session is not active: {session_id}")
        observation = validate_observation(request["observation"])
        policy_observation = {
            "image": np.asarray([observation], dtype=np.float32),
            "reward": np.asarray([float(request.get("reward", 0.0))], dtype=np.float32),
            "is_first": np.asarray([bool(request.get("is_first", False))], dtype=bool),
            "is_last": np.asarray([False], dtype=bool),
            "is_terminal": np.asarray([False], dtype=bool),
        }
        carry, actions, _ = self._agent.policy(
            self._sessions[session_id], policy_observation, mode="eval"
        )
        # Converting to NumPy synchronises the JAX result before the service
        # records completion and sends the response.
        action = np.asarray(actions["action"], dtype=np.float32)[0]
        checked = validate_action(action[:2])
        self._sessions[session_id] = carry
        return checked


@dataclass
class _WorkItem:
    request: dict[str, Any]
    received_ns: int
    queue_depth: int
    done: threading.Event = field(default_factory=threading.Event)
    response: dict[str, Any] | None = None


class _ProviderState:
    def __init__(self, runtime: ProviderRuntime) -> None:
        self.runtime = runtime
        self.work: queue.Queue[_WorkItem | None] = queue.Queue()
        self.sessions_lock = threading.Lock()
        self.worker = threading.Thread(
            target=self._run, name="dreamer-provider", daemon=True
        )
        self.worker.start()

    def reset(self, session_id: str) -> None:
        with self.sessions_lock:
            self.runtime.reset(session_id)

    def enqueue(self, request: dict[str, Any], received_ns: int) -> dict[str, Any]:
        item = _WorkItem(
            request=request,
            received_ns=received_ns,
            queue_depth=self.work.qsize(),
        )
        self.work.put(item)
        item.done.wait()
        if item.response is None:
            raise RuntimeError("provider worker completed without a response")
        return item.response

    def close(self) -> None:
        self.work.put(None)
        self.worker.join(timeout=10)

    def _run(self) -> None:
        while True:
            item = self.work.get()
            if item is None:
                return
            started_ns = time.monotonic_ns()
            try:
                action = self.runtime.infer(item.request)
                finished_ns = time.monotonic_ns()
                item.response = {
                    "protocol_version": PROTOCOL_VERSION,
                    "request_id": item.request["request_id"],
                    "session_id": item.request["session_id"],
                    "ok": True,
                    "action": action,
                    "server_received_monotonic_ns": item.received_ns,
                    "server_started_monotonic_ns": started_ns,
                    "server_finished_monotonic_ns": finished_ns,
                    "queue_depth_at_receive": item.queue_depth,
                }
            except Exception as error:  # noqa: BLE001 - service boundary returns structured failure
                finished_ns = time.monotonic_ns()
                item.response = {
                    "protocol_version": PROTOCOL_VERSION,
                    "request_id": item.request.get("request_id", "unknown"),
                    "session_id": item.request.get("session_id", "unknown"),
                    "ok": False,
                    "error": {"type": type(error).__name__, "message": str(error)},
                    "server_received_monotonic_ns": item.received_ns,
                    "server_started_monotonic_ns": started_ns,
                    "server_finished_monotonic_ns": finished_ns,
                    "queue_depth_at_receive": item.queue_depth,
                }
            finally:
                item.done.set()


class _RequestHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        state: _ProviderState = self.server.provider_state  # type: ignore[attr-defined]
        for raw in self.rfile:
            received_ns = time.monotonic_ns()
            try:
                request = json.loads(raw)
                if request.get("protocol_version") != PROTOCOL_VERSION:
                    raise ValueError("provider protocol version mismatch")
                operation = request.get("op")
                if operation == "reset":
                    state.reset(str(request["session_id"]))
                    response = {
                        "protocol_version": PROTOCOL_VERSION,
                        "request_id": request["request_id"],
                        "session_id": request["session_id"],
                        "ok": True,
                        "server_received_monotonic_ns": received_ns,
                        "server_finished_monotonic_ns": time.monotonic_ns(),
                    }
                elif operation == "infer":
                    response = state.enqueue(request, received_ns)
                elif operation == "ping":
                    response = {
                        "protocol_version": PROTOCOL_VERSION,
                        "request_id": request["request_id"],
                        "ok": True,
                        "server_received_monotonic_ns": received_ns,
                        "server_finished_monotonic_ns": time.monotonic_ns(),
                    }
                else:
                    raise ValueError(f"unsupported provider operation: {operation}")
            except Exception as error:  # noqa: BLE001 - transport boundary returns structured failure
                response = {
                    "protocol_version": PROTOCOL_VERSION,
                    "request_id": "unknown",
                    "ok": False,
                    "error": {"type": type(error).__name__, "message": str(error)},
                }
            self.wfile.write(
                json.dumps(response, separators=(",", ":"), sort_keys=True).encode(
                    "utf-8"
                )
                + b"\n"
            )
            self.wfile.flush()


class ThreadedProviderServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address: tuple[str, int], runtime: ProviderRuntime) -> None:
        self.provider_state = _ProviderState(runtime)
        super().__init__(address, _RequestHandler)

    def server_close(self) -> None:
        super().server_close()
        self.provider_state.close()


class LiveProviderClient:
    """Persistent synchronous client; callers may place each session in a thread."""

    def __init__(self, host: str, port: int, *, timeout_seconds: float = 30.0) -> None:
        self._socket = socket.create_connection((host, port), timeout=timeout_seconds)
        self._socket.settimeout(timeout_seconds)
        self._reader = self._socket.makefile("rb")
        self._writer = self._socket.makefile("wb")

    def close(self) -> None:
        self._reader.close()
        self._writer.close()
        self._socket.close()

    def call(self, request: dict[str, Any]) -> dict[str, Any]:
        sent_ns = time.monotonic_ns()
        self._writer.write(
            json.dumps(request, separators=(",", ":"), sort_keys=True).encode("utf-8")
            + b"\n"
        )
        self._writer.flush()
        raw = self._reader.readline()
        received_ns = time.monotonic_ns()
        if not raw:
            raise ConnectionError("live provider closed the connection")
        response = json.loads(raw)
        if response.get("protocol_version") != PROTOCOL_VERSION:
            raise ValueError("live provider response version mismatch")
        if response.get("request_id") != request.get("request_id"):
            raise ValueError("live provider response identity mismatch")
        response["client_sent_monotonic_ns"] = sent_ns
        response["client_received_monotonic_ns"] = received_ns
        return response

    def reset(self, session_id: str, request_id: str) -> dict[str, Any]:
        return self.call(
            {
                "protocol_version": PROTOCOL_VERSION,
                "op": "reset",
                "request_id": request_id,
                "session_id": session_id,
            }
        )


def serve(args: argparse.Namespace) -> None:
    runtime = DreamerProviderRuntime(
        checkpoint=args.checkpoint,
        expected_agent_sha256=args.agent_sha256,
        teacher_config=args.teacher_config,
        reward_config=args.reward_config,
        distribution_config=args.distribution_config,
        runtime_directory=args.runtime_directory,
        profile=args.profile,
    )
    with ThreadedProviderServer(("127.0.0.1", 0), runtime) as server:
        host, port = server.server_address
        readiness = {
            "protocol_version": PROTOCOL_VERSION,
            "host": host,
            "port": port,
            "ready_monotonic_ns": time.monotonic_ns(),
        }
        args.readiness_file.write_text(
            json.dumps(readiness, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        server.serve_forever(poll_interval=0.05)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--agent-sha256", required=True)
    parser.add_argument("--teacher-config", type=Path, required=True)
    parser.add_argument("--reward-config", type=Path, required=True)
    parser.add_argument("--distribution-config", type=Path, required=True)
    parser.add_argument("--runtime-directory", type=Path, required=True)
    parser.add_argument("--readiness-file", type=Path, required=True)
    parser.add_argument("--profile", default="full")
    serve(parser.parse_args())


if __name__ == "__main__":
    main()
