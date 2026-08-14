"""Local Unix-domain socket transport for the air-hockey host protocol."""

from __future__ import annotations

import os
import socketserver
import stat
import threading
from pathlib import Path

from .protocol import MAX_REQUEST_BYTES, ProtocolProcessor


class _RequestHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        raw = self.rfile.readline(MAX_REQUEST_BYTES + 1)
        processor: ProtocolProcessor = self.server.processor  # type: ignore[attr-defined]
        self.wfile.write(processor.process(raw))


class _ThreadingUnixServer(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True
    allow_reuse_address = False


class UnixHostServer:
    """Serve exactly one request/reply exchange per local connection."""

    def __init__(self, socket_path: Path, processor: ProtocolProcessor) -> None:
        if not socket_path.is_absolute():
            raise ValueError("air-hockey host socket path must be absolute")
        self._path = socket_path
        self._processor = processor
        self._server: _ThreadingUnixServer | None = None
        self._thread: threading.Thread | None = None

    @property
    def socket_path(self) -> Path:
        return self._path

    def start(self) -> None:
        if self._server is not None:
            return
        self._path.parent.mkdir(parents=True, exist_ok=True)
        if self._path.exists():
            if not stat.S_ISSOCK(self._path.stat().st_mode):
                raise RuntimeError(
                    f"refuse to replace non-socket host path: {self._path}"
                )
            self._path.unlink()
        server = _ThreadingUnixServer(str(self._path), _RequestHandler)
        server.processor = self._processor  # type: ignore[attr-defined]
        os.chmod(self._path, 0o600)
        self._server = server
        self._thread = threading.Thread(
            target=server.serve_forever,
            name="muesli_air_hockey_host",
            daemon=True,
        )
        self._thread.start()

    def wait(self) -> None:
        if self._thread is not None:
            self._thread.join()

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

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_: object) -> None:
        self.stop()
