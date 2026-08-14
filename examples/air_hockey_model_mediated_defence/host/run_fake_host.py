"""Run the MuJoCo-free WP1 air-hockey protocol host."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parent / "src"))

from muesli_air_hockey_host import (
    FakeDirectLaunchBackend,
    ProtocolProcessor,
    SchemaRegistry,
    UnixHostServer,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--socket", required=True, type=Path, help="absolute Unix socket path"
    )
    arguments = parser.parse_args()

    schema_directory = REPOSITORY_ROOT / "schemas" / "air_hockey_host" / "v1"
    schemas = SchemaRegistry(schema_directory)
    processor = ProtocolProcessor(schemas, FakeDirectLaunchBackend())
    server = UnixHostServer(arguments.socket, processor)
    try:
        server.start()
        print(f"air-hockey fake host listening on {server.socket_path}", flush=True)
        server.wait()
    except KeyboardInterrupt:
        pass
    finally:
        server.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
