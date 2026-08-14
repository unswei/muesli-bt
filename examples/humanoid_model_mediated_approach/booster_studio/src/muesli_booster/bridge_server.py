"""Bounded local JSON request/reply bridge for the future C++ dispatcher."""

from __future__ import annotations

import json
import os
import socketserver
import stat
import threading
import time
from pathlib import Path
from typing import Any

from .adapter import AdapterState


MAX_REQUEST_BYTES = 16 * 1024


class _BridgeHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        raw = self.rfile.readline(MAX_REQUEST_BYTES + 1)
        if len(raw) > MAX_REQUEST_BYTES:
            self._write({"accepted": False, "reason": "request_too_large"})
            return
        try:
            request = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._write({"accepted": False, "reason": "invalid_json"})
            return
        if not isinstance(request, dict):
            self._write({"accepted": False, "reason": "invalid_schema"})
            return
        operation = request.get("op")
        state: AdapterState = self.server.adapter_state  # type: ignore[attr-defined]
        now = time.monotonic()
        if operation == "ping":
            response: dict[str, Any] = {
                "schema_version": "humanoid.booster_bridge.v1",
                "ok": True,
            }
        elif operation == "snapshot":
            response = state.snapshot_payload(now)
        elif operation == "dispatch":
            response = state.dispatch_payload(request, now)
        else:
            response = {"accepted": False, "reason": "unsupported_operation"}
        self._write(response)

    def _write(self, response: dict[str, Any]) -> None:
        encoded = json.dumps(response, separators=(",", ":"), sort_keys=True).encode("utf-8")
        self.wfile.write(encoded + b"\n")


class _ThreadingUnixServer(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True
    allow_reuse_address = False


class BridgeServer:
    """Serve one bounded request per Unix-domain socket connection."""

    def __init__(self, socket_path: str, adapter_state: AdapterState) -> None:
        path = Path(socket_path)
        if not path.is_absolute():
            raise ValueError("bridge socket path must be absolute")
        self._path = path
        self._state = adapter_state
        self._server: _ThreadingUnixServer | None = None
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if self._server is not None:
            return
        self._path.parent.mkdir(parents=True, exist_ok=True)
        if self._path.exists():
            mode = self._path.stat().st_mode
            if not stat.S_ISSOCK(mode):
                raise RuntimeError(f"refuse to replace non-socket bridge path: {self._path}")
            self._path.unlink()
        server = _ThreadingUnixServer(str(self._path), _BridgeHandler)
        server.adapter_state = self._state  # type: ignore[attr-defined]
        os.chmod(self._path, 0o600)
        self._server = server
        self._thread = threading.Thread(
            target=server.serve_forever,
            name="muesli_booster_bridge",
            daemon=True,
        )
        self._thread.start()

    def stop(self) -> None:
        server = self._server
        if server is not None:
            server.shutdown()
            server.server_close()
        if self._thread is not None and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        self._server = None
        if self._path.exists() and stat.S_ISSOCK(self._path.stat().st_mode):
            self._path.unlink()
